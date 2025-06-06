#include "Arduino.h"

#if !defined(_RPAsyncTCP_LOGLEVEL_)
  #define _RPAsyncTCP_LOGLEVEL_     1
#endif

#include "RPAsyncTCP.h"
#include "RPAsyncTCP_Debug.h"

extern "C"
{
  #include "lwip/ip_addr.h"
  #include "lwip/opt.h"
  #include "lwip/tcp.h"
  #include "lwip/inet.h"
  #include "lwip/dns.h"
  #include "lwip/init.h"
}

#include <tcp_axtls.h>

/*
  Async Client Error Return Tracker
*/
// Assumption: callbacks are never called with err == ERR_ABRT; however,
// they may return ERR_ABRT.

ACErrorTracker::ACErrorTracker(AsyncClient *c):
  _client(c)
  , _close_error(ERR_OK)
  , _errored(EE_OK)
#ifdef DEBUG_MORE
  , _error_event_cb(NULL)
  , _error_event_cb_arg(NULL)
#endif
{}

/////////////////////////////////////////////////

#ifdef DEBUG_MORE
/**
   This is not necessary, but a start at gathering some statistics on
   errored out connections. Used from AsyncServer.
*/
void ACErrorTracker::onErrorEvent(AsNotifyHandler cb, void *arg)
{
  _error_event_cb = cb;
  _error_event_cb_arg = arg;
}
#endif

/////////////////////////////////////////////////

void ACErrorTracker::setCloseError(err_t e)
{
  if (e != ERR_OK)
  {
    ATCP_LOGINFO3("setCloseError() to:", _client->errorToString(e), "=>", e);
  }

  if (_errored == EE_OK)
    _close_error = e;
}

/////////////////////////////////////////////////

/**
   Called mainly by callback routines, called when err is not ERR_OK.
   This prevents the possiblity of aborting an already errored out
   connection.
*/
void ACErrorTracker::setErrored(size_t errorEvent)
{
  if (EE_OK == _errored)
    _errored = errorEvent;

#ifdef DEBUG_MORE
  if (_error_event_cb)
    _error_event_cb(_error_event_cb_arg, errorEvent);
#endif
}

/////////////////////////////////////////////////

/**
   Used by callback functions only. Used for proper ERR_ABRT return value
   reporting. ERR_ABRT is only reported/returned once; thereafter ERR_OK
   is always returned.
*/
err_t ACErrorTracker::getCallbackCloseError()
{
  if (EE_OK != _errored)
    return ERR_OK;

  if (ERR_ABRT == _close_error)
    setErrored(EE_ABORTED);

  return _close_error;
}

/////////////////////////////////////////////////
/////////////////////////////////////////////////

/*
  Async TCP Client
*/
#if DEBUG_ESP_ASYNC_TCP
  static size_t _connectionCount = 0;
#endif

#if ASYNC_TCP_SSL_ENABLED
AsyncClient::AsyncClient(tcp_pcb* pcb, SSL_CTX * ssl_ctx):
#else
AsyncClient::AsyncClient(tcp_pcb * pcb):
#endif
  _connect_cb(0)
  , _connect_cb_arg(0)
  , _discard_cb(0)
  , _discard_cb_arg(0)
  , _sent_cb(0)
  , _sent_cb_arg(0)
  , _error_cb(0)
  , _error_cb_arg(0)
  , _recv_cb(0)
  , _recv_cb_arg(0)
  , _pb_cb(0)
  , _pb_cb_arg(0)
  , _timeout_cb(0)
  , _timeout_cb_arg(0)
  , _poll_cb(0)
  , _poll_cb_arg(0)
  , _pcb_busy(false)
#if ASYNC_TCP_SSL_ENABLED
  , _pcb_secure(false)
  , _handshake_done(true)
#endif
  , _pcb_sent_at(0)
  , _close_pcb(false)
  , _ack_pcb(true)
  , _tx_unacked_len(0)
  , _tx_acked_len(0)
  , _tx_unsent_len(0)
  , _rx_ack_len(0)
  , _rx_last_packet(0)
  , _rx_since_timeout(0)
  , _ack_timeout(ASYNC_MAX_ACK_TIME)
  , _connect_port(0)
  , _recv_pbuf_flags(0)
  , _errorTracker(NULL)
  , prev(NULL)
  , next(NULL)
{
  _pcb = pcb;

  if (_pcb)
  {
    _rx_last_packet = millis();
    //tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_setprio(_pcb, TCP_PRIO_NORMAL);
    tcp_arg(_pcb, this);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_err(_pcb, &_s_error);
    tcp_poll(_pcb, &_s_poll, 1);

#if ASYNC_TCP_SSL_ENABLED
    if (ssl_ctx)
    {
      if (tcp_ssl_new_server(_pcb, ssl_ctx) < 0)
      {
        _close();
        return;
      }

      tcp_ssl_arg(_pcb, this);
      tcp_ssl_data(_pcb, &_s_data);
      tcp_ssl_handshake(_pcb, &_s_handshake);
      tcp_ssl_err(_pcb, &_s_ssl_error);

      _pcb_secure = true;
      _handshake_done = false;
    }
#endif
  }

  _errorTracker = std::make_shared<ACErrorTracker>(this);

#if DEBUG_ESP_ASYNC_TCP
  _errorTracker->setConnectionId(++_connectionCount);
#endif
}

/////////////////////////////////////////////////

AsyncClient::~AsyncClient()
{
  if (_pcb)
    _close();

  _errorTracker->clearClient();
}

/////////////////////////////////////////////////

inline void clearTcpCallbacks(tcp_pcb* pcb)
{
  tcp_arg(pcb, NULL);
  tcp_sent(pcb, NULL);
  tcp_recv(pcb, NULL);
  tcp_err(pcb, NULL);
  tcp_poll(pcb, NULL, 0);
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
bool AsyncClient::connect(IPAddress ip, uint16_t port, bool secure)
#else
bool AsyncClient::connect(IPAddress ip, uint16_t port)
#endif
{
  if (_pcb)
  {
    //already connected
    ATCP_LOGDEBUG1("connect: already connected, _pcb =", (uint32_t) _pcb );

    return false;
  }

  ip_addr_t addr;
  addr.addr = ip;

#if LWIP_VERSION_MAJOR == 1
  netif* interface = ip_route(&addr);

  if (!interface)
  {
    //no route to host
    ATCP_LOGDEBUG("connect: no route to host, NULL interface");

    return false;
  }
#endif

  tcp_pcb* pcb = tcp_new();

  if (!pcb)
  {
    //could not allocate pcb
    ATCP_LOGDEBUG("connect: could not allocate pcb");

    return false;
  }

  //tcp_setprio(_pcb, TCP_PRIO_MIN);
  tcp_setprio(_pcb, TCP_PRIO_NORMAL);

#if ASYNC_TCP_SSL_ENABLED
  _pcb_secure = secure;
  _handshake_done = !secure;
#endif

  tcp_arg(pcb, this);
  tcp_err(pcb, &_s_error);
  size_t err = tcp_connect(pcb, &addr, port, (tcp_connected_fn)&_s_connected);

  ATCP_LOGDEBUG1("connect: err =", err);

  return (ERR_OK == err);
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
bool AsyncClient::connect(const char* host, uint16_t port, bool secure)
#else
bool AsyncClient::connect(const char* host, uint16_t port)
#endif
{
  ip_addr_t addr;

  err_t err = dns_gethostbyname(host, &addr, (dns_found_callback)&_s_dns_found, this);

  if (err == ERR_OK)
  {
    bool returnValue;
#if ASYNC_TCP_SSL_ENABLED
    returnValue = connect(IPAddress(addr.addr), port, secure);
#else
    returnValue = connect(IPAddress(addr.addr), port);
#endif

    ATCP_LOGDEBUG3("connect: dns_gethostbyname => IP =", IPAddress(addr.addr), ", returnValue = ", returnValue ? "TRUE" : "FALSE");

    return returnValue;
  }
  else if (err == ERR_INPROGRESS)
  {
#if ASYNC_TCP_SSL_ENABLED
    _pcb_secure = secure;
    _handshake_done = !secure;
#endif
    _connect_port = port;

    ATCP_LOGDEBUG1("connect: OK, _connect_port = ", _connect_port);

    return true;
  }

  ATCP_LOGDEBUG1("connect: error = ", err);

  return false;
}

/////////////////////////////////////////////////

AsyncClient& AsyncClient::operator=(const AsyncClient& other)
{
  if (_pcb)
  {
    _close();
  }

  _errorTracker = other._errorTracker;

  // I am confused when "other._pcb" falls out of scope the destructor will
  // close it? TODO: Look to see where this is used and how it might work.
  _pcb = other._pcb;

  if (_pcb)
  {
    _rx_last_packet = millis();

    //tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_setprio(_pcb, TCP_PRIO_NORMAL);

    tcp_arg(_pcb, this);
    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_err(_pcb, &_s_error);
    tcp_poll(_pcb, &_s_poll, 1);

#if ASYNC_TCP_SSL_ENABLED
    if (tcp_ssl_has(_pcb))
    {
      _pcb_secure = true;
      _handshake_done = false;
      tcp_ssl_arg(_pcb, this);
      tcp_ssl_data(_pcb, &_s_data);
      tcp_ssl_handshake(_pcb, &_s_handshake);
      tcp_ssl_err(_pcb, &_s_ssl_error);
    }
    else
    {
      _pcb_secure = false;
      _handshake_done = true;
    }
#endif
  }

  return *this;
}

/////////////////////////////////////////////////

bool AsyncClient::operator==(const AsyncClient &other) const 
{
  return (_pcb != NULL && other._pcb != NULL && (_pcb->remote_ip.addr == other._pcb->remote_ip.addr) &&
         (_pcb->remote_port == other._pcb->remote_port));
}

/////////////////////////////////////////////////

void AsyncClient::abort()
{
  // Notes:
  // 1) _pcb is set to NULL, so we cannot call tcp_abort() more than once.
  // 2) setCloseError(ERR_ABRT) is only done here!
  // 3) Using this abort() function guarantees only one tcp_abort() call is
  //    made and only one CB returns with ERR_ABORT.
  // 4) After abort() is called from _close(), no callbacks with an err
  //    parameter will be called.  eg. _recv(), _error(), _connected().
  //    _close() will reset there CB handlers before calling.
  // 5) A callback to _error(), will set _pcb to NULL, thus avoiding the
  //    of a 2nd call to tcp_abort().
  // 6) Callbacks to _recv() or _connected() with err set, will result in _pcb
  //    set to NULL. Thus, preventing possible calls later to tcp_abort().

  if (_pcb)
  {
    tcp_abort(_pcb);
    _pcb = NULL;
    setCloseError(ERR_ABRT);
  }

  return;
}

/////////////////////////////////////////////////

void AsyncClient::close(bool now)
{
  if (_pcb)
    tcp_recved(_pcb, _rx_ack_len);

  if (now)
    _close();
  else
    _close_pcb = true;
}

/////////////////////////////////////////////////

void AsyncClient::stop()
{
  close(false);
}

/////////////////////////////////////////////////

bool AsyncClient::free()
{
  if (!_pcb)
    return true;

  if ( (_pcb->state == CLOSED) || (_pcb->state > ESTABLISHED) )
    return true;

  return false;
}

/////////////////////////////////////////////////

size_t AsyncClient::write(const char* data)
{
  if (data == NULL)
    return 0;

  return write(data, strlen(data));
}

/////////////////////////////////////////////////

size_t AsyncClient::write(const char* data, size_t size, uint8_t apiflags)
{
  size_t will_send = add(data, size, apiflags);

  if (!will_send || !send())
    return 0;

  return will_send;
}

/////////////////////////////////////////////////

size_t AsyncClient::add(const char* data, size_t size, uint8_t apiflags)
{
  if (!_pcb || size == 0 || data == NULL)
    return 0;

  size_t room = space();

  if (!room)
    return 0;

#if ASYNC_TCP_SSL_ENABLED
  if (_pcb_secure)
  {
    int sent = tcp_ssl_write(_pcb, (uint8_t*)data, size);

    if (sent >= 0)
    {
      _tx_unacked_len += sent;
      return sent;
    }

    _close();

    return 0;
  }
#endif

  size_t will_send = (room < size) ? room : size;
  err_t err = tcp_write(_pcb, data, will_send, apiflags);

  if (err != ERR_OK)
  {
    return 0;
  }

  _tx_unsent_len += will_send;

  return will_send;
}

/////////////////////////////////////////////////

bool AsyncClient::send()
{
#if ASYNC_TCP_SSL_ENABLED
  if (_pcb_secure)
    return true;
#endif

  err_t err = tcp_output(_pcb);

  if (err == ERR_OK)
  {
    _pcb_busy = true;
    _pcb_sent_at = millis();
    _tx_unacked_len += _tx_unsent_len;
    _tx_unsent_len = 0;

    return true;
  }

  _tx_unsent_len = 0;

  return false;
}

/////////////////////////////////////////////////

size_t AsyncClient::ack(size_t len)
{
  if (len > _rx_ack_len)
    len = _rx_ack_len;

  if (len)
    tcp_recved(_pcb, len);

  _rx_ack_len -= len;

  return len;
}

/////////////////////////////////////////////////

// Private Callbacks

void AsyncClient::_connected(std::shared_ptr<ACErrorTracker>& errorTracker, void* pcb, err_t err)
{
  //RPAsyncTCP_UNUSED(err); // LWIP v1.4 appears to always call with ERR_OK
  // Documentation for 2.1.0 also says:
  //   "err  - An unused error code, always ERR_OK currently ;-)"
  // https://www.nongnu.org/lwip/2_1_x/tcp_8h.html#a939867106bd492caf2d85852fb7f6ae8
  // Based on that wording and emoji lets just handle it now.
  // After all, the API does allow for an err != ERR_OK.

  if (NULL == pcb || ERR_OK != err)
  {
    ATCP_LOGDEBUG3("_connected: ID =", errorTracker->getConnectionId(), ", _pcb =", ((NULL == _pcb) ? "NULL" : "OK") );
    ATCP_LOGDEBUG3("errorToString =", errorToString(err), ", err =", err );

    errorTracker->setCloseError(err);
    errorTracker->setErrored(EE_CONNECTED_CB);
    _pcb = reinterpret_cast<tcp_pcb*>(pcb);

    if (_pcb)
      clearTcpCallbacks(_pcb);

    _pcb = NULL;
    _error(err);

    return;
  }

  _pcb = reinterpret_cast<tcp_pcb*>(pcb);

  if (_pcb)
  {
    _pcb_busy = false;
    _rx_last_packet = millis();

    //tcp_setprio(_pcb, TCP_PRIO_MIN);
    tcp_setprio(_pcb, TCP_PRIO_NORMAL);

    tcp_recv(_pcb, &_s_recv);
    tcp_sent(_pcb, &_s_sent);
    tcp_poll(_pcb, &_s_poll, 1);

#if ASYNC_TCP_SSL_ENABLED
    if (_pcb_secure)
    {
      if (tcp_ssl_new_client(_pcb) < 0)
      {
        _close();

        return;
      }

      tcp_ssl_arg(_pcb, this);
      tcp_ssl_data(_pcb, &_s_data);
      tcp_ssl_handshake(_pcb, &_s_handshake);
      tcp_ssl_err(_pcb, &_s_ssl_error);
    }
  }

  if (!_pcb_secure && _connect_cb)
#else
  }

  if (_connect_cb)
#endif

    _connect_cb(_connect_cb_arg, this);

  return;
}

/////////////////////////////////////////////////

void AsyncClient::_close()
{
  if (_pcb)
  {
#if ASYNC_TCP_SSL_ENABLED
    if (_pcb_secure)
    {
      tcp_ssl_free(_pcb);
    }
#endif

    clearTcpCallbacks(_pcb);
    err_t err = tcp_close(_pcb);

    if (ERR_OK == err)
    {
      setCloseError(err);
    }
    else
    {
      ATCP_LOGDEBUG3("_close : ID =", getConnectionId(), ", abort() called for AsyncClient 0x", uintptr_t(this));

      abort();
    }

    _pcb = NULL;

    if (_discard_cb)
      _discard_cb(_discard_cb_arg, this);
  }

  return;
}

/////////////////////////////////////////////////

void AsyncClient::_error(err_t err)
{
  ATCP_LOGDEBUG3("_error: ID =", getConnectionId(), ", _pcb =", ((NULL == _pcb) ? "NULL" : "OK") );
  ATCP_LOGDEBUG3("errorToString =", errorToString(err), ", err =", err );

  if (_pcb)
  {
#if ASYNC_TCP_SSL_ENABLED
    if (_pcb_secure)
    {
      tcp_ssl_free(_pcb);
    }
#endif

    // At this callback _pcb is possible already freed. Thus, no calls are
    // made to set to NULL other callbacks.
    // KH add, from v1.1.0, to free _pcb
    clearTcpCallbacks(_pcb);
    tcp_close(_pcb);
    //////

    _pcb = NULL;
  }

  if (_error_cb)
    _error_cb(_error_cb_arg, this, err);

  if (_discard_cb)
    _discard_cb(_discard_cb_arg, this);
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
void AsyncClient::_ssl_error(int8_t err)
{
  if (_error_cb)
    _error_cb(_error_cb_arg, this, err + 64);
}
#endif

/////////////////////////////////////////////////

void AsyncClient::_sent(std::shared_ptr<ACErrorTracker>& errorTracker, tcp_pcb* pcb, uint16_t len)
{
  RPAsyncTCP_UNUSED(pcb);

#if ASYNC_TCP_SSL_ENABLED
  if (_pcb_secure && !_handshake_done)
    return;
#endif

  _rx_last_packet  = millis();
  _tx_unacked_len -= len;
  _tx_acked_len   += len;

  ATCP_LOGDEBUG3("_sent: ID =", errorTracker->getConnectionId(), ", len =", len);
  ATCP_LOGDEBUG3("unacked =", _tx_unacked_len, ", acked =", _tx_acked_len);

  if (_tx_unacked_len == 0)
  {
    _pcb_busy = false;
    errorTracker->setCloseError(ERR_OK);

    if (_sent_cb)
    {
      _sent_cb(_sent_cb_arg, this, _tx_acked_len, (millis() - _pcb_sent_at));

      if (!errorTracker->hasClient())
        return;
    }

    _tx_acked_len = 0;
  }

  return;
}

/////////////////////////////////////////////////

void AsyncClient::_recv(std::shared_ptr<ACErrorTracker>& errorTracker, tcp_pcb* pcb, pbuf* pb, err_t err)
{
  // While lwIP v1.4 appears to always call with ERR_OK, 2.x lwIP may present
  // a non-ERR_OK value.
  // https://www.nongnu.org/lwip/2_1_x/tcp_8h.html#a780cfac08b02c66948ab94ea974202e8

  if (NULL == pcb || ERR_OK != err)
  {
    ATCP_LOGDEBUG3("_recv: ID =", errorTracker->getConnectionId(), ", _pcb =", ((NULL == _pcb) ? "NULL" : "OK") );
    ATCP_LOGDEBUG3("errorToString =", errorToString(err), ", err =", err );

    errorTracker->setCloseError(err);
    errorTracker->setErrored(EE_RECV_CB);
    _pcb = pcb;

    if (_pcb)
      clearTcpCallbacks(_pcb);

    _pcb = NULL;

    // I think we are safe from being called from an interrupt context.
    // Best Hint that calling _error() is safe:
    //    https://www.nongnu.org/lwip/2_1_x/group__lwip__nosys.html
    // "Feed incoming packets to netif->input(pbuf, netif) function from
    // mainloop, not from interrupt context. You can allocate a Packet buffers
    // (PBUF) in interrupt context and put them into a queue which is processed
    // from mainloop."
    // And the description of "Mainloop Mode" option 2:
    //    https://www.nongnu.org/lwip/2_1_x/pitfalls.html
    // "2) Run lwIP in a mainloop. ... lwIP is ONLY called from mainloop
    // callstacks here. The ethernet IRQ has to put received telegrams into a
    // queue which is polled in the mainloop. Ensure lwIP is NEVER called from
    // an interrupt, ...!"
    // Based on these comments I am thinking tcp_recv_fn() is called
    // from somebody's mainloop(), which could only have been reached from a
    // delay like function or the Arduino sketch loop() function has returned.
    // What I don't want is for the client sketch to delete the AsyncClient
    // object via _error() while it is in the middle of using it. However,
    // the client sketch must always test that the connection is still up
    // at loop() entry and after the return of any function call, that may
    // have done a delay() or yield().
    _error(err);

    return;
  }

  if (pb == NULL)
  {
    ATCP_LOGDEBUG3("_recv: ID =", errorTracker->getConnectionId(), "pb == NULL! Closing... err =", err );

    _close();

    return;
  }

  _rx_last_packet = millis();
  errorTracker->setCloseError(ERR_OK);

#if ASYNC_TCP_SSL_ENABLED
  if (_pcb_secure)
  {
    ATCP_LOGDEBUG3("_recv: ID =", getConnectionId(), "pb->tot_len =", pb->tot_len);

    int read_bytes = tcp_ssl_read(pcb, pb);

    if (read_bytes < 0)
    {
      if (read_bytes != SSL_CLOSE_NOTIFY)
      {
        ATCP_LOGDEBUG3("_recv: ID =", getConnectionId(), "pb->tot_len =", read_bytes);

        _close();
      }
    }

    return;
  }
#endif

  while (pb != NULL)
  {
    // IF this callback function returns ERR_OK or ERR_ABRT
    // then it is assummed we freed the pbufs.
    // https://www.nongnu.org/lwip/2_1_x/group__tcp__raw.html#ga8afd0b316a87a5eeff4726dc95006ed0

    if (!errorTracker->hasClient())
    {
      while (pb != NULL)
      {
        pbuf *b = pb;
        pb = b->next;
        b->next = NULL;
        pbuf_free(b);
      }

      return;
    }

    //we should not ack before we assimilate the data
    _ack_pcb = true;
    pbuf *b = pb;
    pb = b->next;
    b->next = NULL;

    if (_pb_cb)
    {
      _pb_cb(_pb_cb_arg, this, b);
    }
    else
    {
      if (_recv_cb)
      {
        _recv_pbuf_flags = b->flags;
        _recv_cb(_recv_cb_arg, this, b->payload, b->len);
      }

      if (errorTracker->hasClient())
      {
        if (!_ack_pcb)
          _rx_ack_len += b->len;
        else
          tcp_recved(pcb, b->len);
      }

      pbuf_free(b);
    }
  }

  return;
}

/////////////////////////////////////////////////

void AsyncClient::_poll(std::shared_ptr<ACErrorTracker>& errorTracker, tcp_pcb* pcb)
{
  RPAsyncTCP_UNUSED(pcb);
  errorTracker->setCloseError(ERR_OK);

  // Close requested
  if (_close_pcb)
  {
    _close_pcb = false;
    _close();

    return;
  }

  uint32_t now = millis();

  // ACK Timeout
  if (_pcb_busy && _ack_timeout && (now - _pcb_sent_at) >= _ack_timeout)
  {
    _pcb_busy = false;

    if (_timeout_cb)
      _timeout_cb(_timeout_cb_arg, this, (now - _pcb_sent_at));

    return;
  }

  // RX Timeout
  if (_rx_since_timeout && (now - _rx_last_packet) >= (_rx_since_timeout * 1000))
  {
    _close();

    return;
  }

#if ASYNC_TCP_SSL_ENABLED
  // SSL Handshake Timeout
  if (_pcb_secure && !_handshake_done && (now - _rx_last_packet) >= 2000)
  {
    _close();

    return;
  }
#endif

  // Everything is fine
  if (_poll_cb)
    _poll_cb(_poll_cb_arg, this);

  return;
}

/////////////////////////////////////////////////

#if LWIP_VERSION_MAJOR == 1
void AsyncClient::_dns_found(struct ip_addr *ipaddr)
#else
void AsyncClient::_dns_found(ip_addr_t *p)
#endif
{
  if (p)
  {
#if ASYNC_TCP_SSL_ENABLED
    connect(IPAddress(ipaddr->addr), _connect_port, _pcb_secure);
#else
    connect(IPAddress(p->addr), _connect_port);
#endif
  }
  else
  {
    if (_error_cb)
      _error_cb(_error_cb_arg, this, -55);

    if (_discard_cb)
      _discard_cb(_discard_cb_arg, this);
  }
}

/////////////////////////////////////////////////

// lwIP Callbacks
#if LWIP_VERSION_MAJOR == 1
void AsyncClient::_s_dns_found(const char *name, const ip_addr *ipaddr, void *arg)
#else
void AsyncClient::_s_dns_found(const char *name, ip_addr_t *p, void *arg)
#endif
{
  RPAsyncTCP_UNUSED(name);
  reinterpret_cast<AsyncClient*>(arg)->_dns_found(p);
}

/////////////////////////////////////////////////

err_t AsyncClient::_s_poll(void *arg, struct tcp_pcb *tpcb)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  std::shared_ptr<ACErrorTracker>errorTracker = c->getACErrorTracker();

  c->_poll(errorTracker, tpcb);

  return errorTracker->getCallbackCloseError();
}

/////////////////////////////////////////////////

err_t AsyncClient::_s_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *pb, err_t err)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  auto errorTracker = c->getACErrorTracker();

  c->_recv(errorTracker, tpcb, pb, err);

  return errorTracker->getCallbackCloseError();
}

/////////////////////////////////////////////////

void AsyncClient::_s_error(void *arg, err_t err)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  auto errorTracker = c->getACErrorTracker();

  errorTracker->setCloseError(err);
  errorTracker->setErrored(EE_ERROR_CB);
  c->_error(err);
}

/////////////////////////////////////////////////

err_t AsyncClient::_s_sent(void *arg, struct tcp_pcb *tpcb, uint16_t len)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  auto errorTracker = c->getACErrorTracker();

  c->_sent(errorTracker, tpcb, len);

  return errorTracker->getCallbackCloseError();
}

/////////////////////////////////////////////////

err_t AsyncClient::_s_connected(void* arg, void* tpcb, err_t err)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  auto errorTracker = c->getACErrorTracker();

  c->_connected(errorTracker, tpcb, err);

  return errorTracker->getCallbackCloseError();
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
void AsyncClient::_s_data(void *arg, struct tcp_pcb *tcp, uint8_t * data, size_t len)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);

  if (c->_recv_cb)
    c->_recv_cb(c->_recv_cb_arg, c, data, len);
}

/////////////////////////////////////////////////

void AsyncClient::_s_handshake(void *arg, struct tcp_pcb *tcp, SSL *ssl)
{
  AsyncClient *c = reinterpret_cast<AsyncClient*>(arg);
  c->_handshake_done = true;

  if (c->_connect_cb)
    c->_connect_cb(c->_connect_cb_arg, c);
}

/////////////////////////////////////////////////

void AsyncClient::_s_ssl_error(void *arg, struct tcp_pcb *tcp, int8_t err)
{
  reinterpret_cast<AsyncClient*>(arg)->_ssl_error(err);
}
#endif

/////////////////////////////////////////////////

// Operators

AsyncClient & AsyncClient::operator+=(const AsyncClient &other)
{
  if (next == NULL)
  {
    next = (AsyncClient*)(&other);
    next->prev = this;
  }
  else
  {
    AsyncClient *c = next;

    while (c->next != NULL)
      c = c->next;

    c->next = (AsyncClient*)(&other);
    c->next->prev = c;
  }

  return *this;
}

/////////////////////////////////////////////////

void AsyncClient::setRxTimeout(uint32_t timeout)
{
  _rx_since_timeout = timeout;
}

/////////////////////////////////////////////////

uint32_t AsyncClient::getRxTimeout() const 
{
  return _rx_since_timeout;
}

/////////////////////////////////////////////////

uint32_t AsyncClient::getAckTimeout() const 
{
  return _ack_timeout;
}

/////////////////////////////////////////////////

void AsyncClient::setAckTimeout(uint32_t timeout)
{
  _ack_timeout = timeout;
}

/////////////////////////////////////////////////

void AsyncClient::setNoDelay(bool nodelay)
{
  if (!_pcb)
    return;

  if (nodelay)
    tcp_nagle_disable(_pcb);
  else
    tcp_nagle_enable(_pcb);
}

/////////////////////////////////////////////////

bool AsyncClient::getNoDelay() const 
{
  if (!_pcb)
    return false;

  return tcp_nagle_disabled(_pcb);
}

/////////////////////////////////////////////////

uint16_t AsyncClient::getMss() const 
{
  if (_pcb)
    return tcp_mss(_pcb);

  return 0;
}

/////////////////////////////////////////////////

uint32_t AsyncClient::getRemoteAddress() const 
{
  if (!_pcb)
    return 0;

  return _pcb->remote_ip.addr;
}

/////////////////////////////////////////////////

uint16_t AsyncClient::getRemotePort() const 
{
  if (!_pcb)
    return 0;

  return _pcb->remote_port;
}

/////////////////////////////////////////////////

uint32_t AsyncClient::getLocalAddress() const 
{
  if (!_pcb)
    return 0;

  return _pcb->local_ip.addr;
}

/////////////////////////////////////////////////

uint16_t AsyncClient::getLocalPort() const 
{
  if (!_pcb)
    return 0;

  return _pcb->local_port;
}

/////////////////////////////////////////////////

IPAddress AsyncClient::remoteIP() const 
{
  return IPAddress(getRemoteAddress());
}

/////////////////////////////////////////////////

uint16_t AsyncClient::remotePort() const 
{
  return getRemotePort();
}

/////////////////////////////////////////////////

IPAddress AsyncClient::localIP() const 
{
  return IPAddress(getLocalAddress());
}

/////////////////////////////////////////////////

uint16_t AsyncClient::localPort() const 
{
  return getLocalPort();
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
SSL * AsyncClient::getSSL()
{
  if (_pcb && _pcb_secure)
  {
    return tcp_ssl_get_ssl(_pcb);
  }

  return NULL;
}
#endif

/////////////////////////////////////////////////

uint8_t AsyncClient::state() const 
{
  if (!_pcb)
    return 0;

  return _pcb->state;
}

/////////////////////////////////////////////////

bool AsyncClient::connected() const 
{
  if (!_pcb)
  {
    ATCP_LOGDEBUG("connected: error NULL pcb");

    return false;
  }

#if ASYNC_TCP_SSL_ENABLED
  return ( (_pcb->state == ESTABLISHED) && _handshake_done );
#else
  return (_pcb->state == ESTABLISHED);
#endif
}

/////////////////////////////////////////////////

bool AsyncClient::connecting() const 
{
  if (!_pcb)
  {
    ATCP_LOGDEBUG("connecting: error NULL pcb");

    return false;
  }

  return ( (_pcb->state > CLOSED) && (_pcb->state < ESTABLISHED) );
}

/////////////////////////////////////////////////

bool AsyncClient::disconnecting() const 
{
  if (!_pcb)
  {
    ATCP_LOGDEBUG("disconnecting: error NULL pcb");

    return false;
  }

  return ( (_pcb->state > ESTABLISHED) && (_pcb->state < TIME_WAIT) );
}

/////////////////////////////////////////////////

bool AsyncClient::disconnected() const 
{
  if (!_pcb)
  {
    ATCP_LOGDEBUG("disconnected: error NULL pcb");

    return false;
  }

  return ( (_pcb->state == CLOSED) || (_pcb->state == TIME_WAIT) );
}

/////////////////////////////////////////////////

bool AsyncClient::freeable() const 
{
  if (!_pcb)
  {
    ATCP_LOGDEBUG("freeable: error NULL pcb");

    return false;
  }

  return ( (_pcb->state == CLOSED) || (_pcb->state > ESTABLISHED) );
}

/////////////////////////////////////////////////

bool AsyncClient::canSend() const 
{
  if (_pcb_busy)
  {
    ATCP_LOGDEBUG("canSend: error _pcb_busy");
  }

  if (!(space() > 0))
  {
    ATCP_LOGDEBUG("canSend: space() <= 0");
  }

  return !_pcb_busy && (space() > 0);
}

/////////////////////////////////////////////////

// Callback Setters

void AsyncClient::onConnect(AcConnectHandler cb, void* arg)
{
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onDisconnect(AcConnectHandler cb, void* arg)
{
  _discard_cb = cb;
  _discard_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onAck(AcAckHandler cb, void* arg)
{
  _sent_cb = cb;
  _sent_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onError(AcErrorHandler cb, void* arg)
{
  _error_cb = cb;
  _error_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onData(AcDataHandler cb, void* arg)
{
  _recv_cb = cb;
  _recv_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onPacket(AcPacketHandler cb, void* arg)
{
  _pb_cb = cb;
  _pb_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onTimeout(AcTimeoutHandler cb, void* arg)
{
  _timeout_cb = cb;
  _timeout_cb_arg = arg;
}

/////////////////////////////////////////////////

void AsyncClient::onPoll(AcConnectHandler cb, void* arg)
{
  _poll_cb = cb;
  _poll_cb_arg = arg;
}

/////////////////////////////////////////////////

size_t AsyncClient::space() const 
{
#if ASYNC_TCP_SSL_ENABLED
  if ( (_pcb != NULL) && (_pcb->state == ESTABLISHED) && _handshake_done )
  {
    uint16_t s = tcp_sndbuf(_pcb);

    if (_pcb_secure)
    {
#ifdef AXTLS_2_0_0_SNDBUF
      return tcp_ssl_sndbuf(_pcb);
#else
      if (s >= 128) //safe approach
        return s - 128;

      return 0;
#endif
    }

    return s;
  }
#else // ASYNC_TCP_SSL_ENABLED
  if ((_pcb != NULL) && (_pcb->state == ESTABLISHED))
  {
    return tcp_sndbuf(_pcb);
  }
#endif // ASYNC_TCP_SSL_ENABLED

  return 0;
}

/////////////////////////////////////////////////

void AsyncClient::ackPacket(struct pbuf * pb)
{
  if (!pb)
  {
    return;
  }

  tcp_recved(_pcb, pb->len);
  pbuf_free(pb);
}

/////////////////////////////////////////////////

const char * AsyncClient::errorToString(err_t error) const 
{
  switch (error)
  {
    case ERR_OK:
      return "OK";
    case ERR_MEM:
      return "Out of memory error";
    case ERR_BUF:
      return "Buffer error";
    case ERR_TIMEOUT:
      return "Timeout";
    case ERR_RTE:
      return "Routing problem";
    case ERR_INPROGRESS:
      return "Operation in progress";
    case ERR_VAL:
      return "Illegal value";
    case ERR_WOULDBLOCK:
      return "Operation would block";
    case ERR_USE:
      return "Address in use";
    case ERR_ALREADY:
      return "Already connected";
    case ERR_CONN:
      return "Not connected";
    case ERR_IF:
      return "Low-level netif error";
    case ERR_ABRT:
      return "Connection aborted";
    case ERR_RST:
      return "Connection reset";
    case ERR_CLSD:
      return "Connection closed";
    case ERR_ARG:
      return "Illegal argument";
    case -55:
      return "DNS failed";
    default:
      return "UNKNOWN";
  }
}

/////////////////////////////////////////////////

/*****************************************************
// Defined in ./pico-sdk/lib/lwip/src/include/lwip/tcpbase.h
enum tcp_state
{
  CLOSED      = 0,
  LISTEN      = 1,
  SYN_SENT    = 2,
  SYN_RCVD    = 3,
  ESTABLISHED = 4,
  FIN_WAIT_1  = 5,
  FIN_WAIT_2  = 6,
  CLOSE_WAIT  = 7,
  CLOSING     = 8,
  LAST_ACK    = 9,
  TIME_WAIT   = 10
};
*****************************************************/

const char * AsyncClient::stateToString() const 
{
  switch (state())
  {
    case CLOSED:
      return "Closed";
    case LISTEN:
      return "Listen";
    case SYN_SENT:
      return "SYN Sent";
    case SYN_RCVD:
      return "SYN Received";
    case ESTABLISHED:
      return "Established";
    case FIN_WAIT_1:
      return "FIN Wait 1";
    case FIN_WAIT_2:
      return "FIN Wait 2";
    case CLOSE_WAIT:
      return "Close Wait";
    case CLOSING:
      return "Closing";
    case LAST_ACK:
      return "Last ACK";
    case TIME_WAIT:
      return "Time Wait";
    default:
      return "UNKNOWN";
  }
}

/////////////////////////////////////////////////
/////////////////////////////////////////////////

/*
  Async TCP Server
*/
struct pending_pcb
{
  tcp_pcb* pcb;
  pbuf *pb;
  struct pending_pcb * next;
};

/////////////////////////////////////////////////

AsyncServer::AsyncServer(IPAddress addr, uint16_t port)
  : _port(port)
  , _addr(addr)
  , _noDelay(false)
  , _pcb(0)
  , _connect_cb(0)
  , _connect_cb_arg(0)
#if ASYNC_TCP_SSL_ENABLED
  , _pending(NULL)
  , _ssl_ctx(NULL)
  , _file_cb(0)
  , _file_cb_arg(0)
#endif
{
#ifdef DEBUG_MORE
  for (size_t i = 0; i < EE_MAX; ++i)
    _event_count[i] = 0;
#endif
}

/////////////////////////////////////////////////

AsyncServer::AsyncServer(uint16_t port)
  : _port(port)
  , _addr((uint32_t) IPADDR_ANY)
  , _noDelay(false)
  , _pcb(0)
  , _connect_cb(0)
  , _connect_cb_arg(0)
#if ASYNC_TCP_SSL_ENABLED
  , _pending(NULL)
  , _ssl_ctx(NULL)
  , _file_cb(0)
  , _file_cb_arg(0)
#endif
{
#ifdef DEBUG_MORE
  for (size_t i = 0; i < EE_MAX; ++i)
    _event_count[i] = 0;
#endif
}

/////////////////////////////////////////////////

AsyncServer::~AsyncServer()
{
  end();
}

/////////////////////////////////////////////////

void AsyncServer::onClient(AcConnectHandler cb, void* arg)
{
  _connect_cb = cb;
  _connect_cb_arg = arg;
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
void AsyncServer::onSslFileRequest(AcSSlFileHandler cb, void* arg)
{
  _file_cb = cb;
  _file_cb_arg = arg;
}
#endif

/////////////////////////////////////////////////

void AsyncServer::begin()
{
  if (_pcb)
    return;

  int8_t err;
  tcp_pcb* pcb = tcp_new();

  if (!pcb)
  {
    return;
  }

  //tcp_setprio(_pcb, TCP_PRIO_MIN);
  tcp_setprio(_pcb, TCP_PRIO_NORMAL);

  ip_addr_t local_addr;
  local_addr.addr = (uint32_t) _addr;
  err = tcp_bind(pcb, &local_addr, _port);

  // Failures are ERR_ISCONN or ERR_USE
  if (err != ERR_OK)
  {
    tcp_close(pcb);

    return;
  }

  tcp_pcb* listen_pcb = tcp_listen(pcb);

  if (!listen_pcb)
  {
    tcp_close(pcb);

    return;
  }

  _pcb = listen_pcb;
  tcp_arg(_pcb, (void*) this);
  tcp_accept(_pcb, &_s_accept);
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED
void AsyncServer::beginSecure(const char *cert, const char *key, const char *password)
{
  if (_ssl_ctx)
  {
    return;
  }

  tcp_ssl_file(_s_cert, this);
  _ssl_ctx = tcp_ssl_new_server_ctx(cert, key, password);

  if (_ssl_ctx)
  {
    begin();
  }
}
#endif

/////////////////////////////////////////////////

void AsyncServer::end()
{
  if (_pcb)
  {
    //cleanup all connections?
    tcp_arg(_pcb, NULL);
    tcp_accept(_pcb, NULL);

    if (tcp_close(_pcb) != ERR_OK)
    {
      tcp_abort(_pcb);
    }

    _pcb = NULL;
  }

#if ASYNC_TCP_SSL_ENABLED
  if (_ssl_ctx)
  {
    ssl_ctx_free(_ssl_ctx);
    _ssl_ctx = NULL;

    if (_pending)
    {
      struct pending_pcb * p;

      while (_pending)
      {
        p = _pending;
        _pending = _pending->next;

        if (p->pb)
        {
          pbuf_free(p->pb);
        }

        free(p);
      }
    }
  }
#endif
}

/////////////////////////////////////////////////

void AsyncServer::setNoDelay(bool nodelay)
{
  _noDelay = nodelay;
}

/////////////////////////////////////////////////

bool AsyncServer::getNoDelay() const 
{
  return _noDelay;
}

/////////////////////////////////////////////////

uint8_t AsyncServer::status() const 
{
  if (!_pcb)
    return 0;

  return _pcb->state;
}

/////////////////////////////////////////////////

err_t AsyncServer::_accept(tcp_pcb* pcb, err_t err)
{
  //http://savannah.nongnu.org/bugs/?43739
  if (NULL == pcb || ERR_OK != err)
  {
    // https://www.nongnu.org/lwip/2_1_x/tcp_8h.html#a00517abce6856d6c82f0efebdafb734d
    // An error code if there has been an error accepting. Only return ERR_ABRT
    // if you have called tcp_abort from within the callback function!
    // eg. 2.1.0 could call with error on failure to allocate pcb.

#ifdef DEBUG_MORE
    incEventCount(EE_ACCEPT_CB);
#endif

    return ERR_OK;
  }

  if (_connect_cb)
  {
#if ASYNC_TCP_SSL_ENABLED
    if (_noDelay || _ssl_ctx)
#else
    if (_noDelay)
#endif
      tcp_nagle_disable(pcb);
    else
      tcp_nagle_enable(pcb);

#if ASYNC_TCP_SSL_ENABLED
    if (_ssl_ctx)
    {
      if (tcp_ssl_has_client() || _pending)
      {
        struct pending_pcb * new_item = (struct pending_pcb*)malloc(sizeof(struct pending_pcb));

        if (!new_item)
        {
          ATCP_LOGDEBUG("### malloc new pending failed!");

          if (tcp_close(pcb) != ERR_OK)
          {
            tcp_abort(pcb);

            return ERR_ABRT;
          }

          return ERR_OK;
        }

        ATCP_LOGDEBUG1("### put to wait:", _clients_waiting);

        new_item->pcb = pcb;
        new_item->pb = NULL;
        new_item->next = NULL;

        //tcp_setprio(_pcb, TCP_PRIO_MIN);
        tcp_setprio(_pcb, TCP_PRIO_NORMAL);

        tcp_arg(pcb, this);
        tcp_poll(pcb, &_s_poll, 1);
        tcp_recv(pcb, &_s_recv);

        if (_pending == NULL)
        {
          _pending = new_item;
        }
        else
        {
          struct pending_pcb * p = _pending;

          while (p->next != NULL)
            p = p->next;

          p->next = new_item;
        }
      }
      else
      {
        AsyncClient *c = new (std::nothrow) AsyncClient(pcb, _ssl_ctx);

        if (c)
        {
          ATCP_LOGDEBUG1("_accept SSL connected: ID =", c->getConnectionId());

          c->onConnect([this](void * arg, AsyncClient * c)
          {
            _connect_cb(_connect_cb_arg, c);
          }, this);
        }
        else
        {
          ATCP_LOGDEBUG("_accept[_ssl_ctx]: new AsyncClient() failed, connection aborted!");

          if (tcp_close(pcb) != ERR_OK)
          {
            tcp_abort(pcb);

            return ERR_ABRT;
          }
        }
      }

      return ERR_OK;
    }
    else
    {
      AsyncClient *c = new (std::nothrow) AsyncClient(pcb, NULL);
#else
    AsyncClient *c = new (std::nothrow) AsyncClient(pcb);
#endif

      if (c)
      {
        auto errorTracker = c->getACErrorTracker();

#ifdef DEBUG_MORE
        errorTracker->onErrorEvent([](void *obj, size_t ee)
        {
          ((AsyncServer*)(obj))->incEventCount(ee);
        }, this);
#endif

        ATCP_LOGDEBUG1("_accept: connected ID = ", errorTracker->getConnectionId());

        _connect_cb(_connect_cb_arg, c);

        return errorTracker->getCallbackCloseError();
      }
      else
      {
        ATCP_LOGDEBUG("_accept: new AsyncClient() failed, connection aborted!");

        if (tcp_close(pcb) != ERR_OK)
        {
          tcp_abort(pcb);

          return ERR_ABRT;
        }
      }
#if ASYNC_TCP_SSL_ENABLED
    }
#endif
  }

  if (tcp_close(pcb) != ERR_OK)
  {
    tcp_abort(pcb);

    return ERR_ABRT;
  }

  return ERR_OK;
}

/////////////////////////////////////////////////

err_t AsyncServer::_s_accept(void *arg, tcp_pcb* pcb, err_t err)
{
  return reinterpret_cast<AsyncServer*>(arg)->_accept(pcb, err);
}

/////////////////////////////////////////////////

#if ASYNC_TCP_SSL_ENABLED

err_t AsyncServer::_poll(tcp_pcb* pcb)
{
  if (!tcp_ssl_has_client() && _pending)
  {
    struct pending_pcb * p = _pending;

    if (p->pcb == pcb)
    {
      _pending = _pending->next;
    }
    else
    {
      while (p->next && p->next->pcb != pcb)
        p = p->next;

      if (!p->next)
        return 0;

      struct pending_pcb * b = p->next;
      p->next = b->next;
      p = b;
    }

    ATCP_LOGDEBUG1("### remove from wait: ", _clients_waiting);

    AsyncClient *c = new (std::nothrow) AsyncClient(pcb, _ssl_ctx);

    if (c)
    {
      c->onConnect([this](void * arg, AsyncClient * c)
      {
        _connect_cb(_connect_cb_arg, c);
      }, this);

      if (p->pb)
        c->_recv(pcb, p->pb, 0);
    }

    // Should there be error handling for when "new AsynClient" fails??
    free(p);
  }
  return ERR_OK;
}

/////////////////////////////////////////////////

err_t AsyncServer::_recv(struct tcp_pcb *pcb, struct pbuf *pb, err_t err)
{
  if (!_pending)
    return ERR_OK;

  struct pending_pcb * p;

  if (!pb)
  {
    ATCP_LOGDEBUG1("### close from wait: ", _clients_waiting);

    p = _pending;

    if (p->pcb == pcb)
    {
      _pending = _pending->next;
    }
    else
    {
      while (p->next && p->next->pcb != pcb)
        p = p->next;

      if (!p->next)
        return 0;

      struct pending_pcb * b = p->next;
      p->next = b->next;
      p = b;
    }

    if (p->pb)
    {
      pbuf_free(p->pb);
    }

    free(p);
    size_t err = tcp_close(pcb);

    if (err != ERR_OK)
    {
      tcp_abort(pcb);

      return ERR_ABRT;
    }

  }
  else
  {
    ATCP_LOGDEBUG3("### wait _recv: tot_len =", pb->tot_len, ", _clients_waiting =", _clients_waiting);

    p = _pending;

    while (p && p->pcb != pcb)
      p = p->next;

    if (p)
    {
      if (p->pb)
      {
        pbuf_chain(p->pb, pb);
      }
      else
      {
        p->pb = pb;
      }
    }
  }

  return ERR_OK;
}

/////////////////////////////////////////////////

int AsyncServer::_cert(const char *filename, uint8_t **buf)
{
  if (_file_cb)
  {
    return _file_cb(_file_cb_arg, filename, buf);
  }

  *buf = 0;

  return 0;
}

/////////////////////////////////////////////////

int AsyncServer::_s_cert(void *arg, const char *filename, uint8_t **buf)
{
  return reinterpret_cast<AsyncServer*>(arg)->_cert(filename, buf);
}

/////////////////////////////////////////////////

err_t AsyncServer::_s_poll(void *arg, struct tcp_pcb *pcb)
{
  return reinterpret_cast<AsyncServer*>(arg)->_poll(pcb);
}

/////////////////////////////////////////////////

err_t AsyncServer::_s_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *pb, err_t err)
{
  return reinterpret_cast<AsyncServer*>(arg)->_recv(pcb, pb, err);
}

#endif		// #if ASYNC_TCP_SSL_ENABLED

/////////////////////////////////////////////////
