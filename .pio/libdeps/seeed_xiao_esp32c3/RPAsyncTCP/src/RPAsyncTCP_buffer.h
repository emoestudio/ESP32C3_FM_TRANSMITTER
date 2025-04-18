#ifndef RPAsyncTCP_BUFFER_H_
#define RPAsyncTCP_BUFFER_H_

#ifndef DEBUG_ASYNC_TCP
  #define DEBUG_ASYNC_TCP(...)
#endif

#include <Arduino.h>
#include <cbuf.h>

#include "RPAsyncTCP.h"

typedef enum
{
  ATB_RX_MODE_NONE,
  ATB_RX_MODE_FREE,
  ATB_RX_MODE_READ_BYTES,
  ATB_RX_MODE_TERMINATOR,
  ATB_RX_MODE_TERMINATOR_STRING
} atbRxMode_t;

/////////////////////////////////////////////////////////

class AsyncTCPbuffer: public Print
{

  public:

    typedef std::function<size_t(uint8_t * payload, size_t length)> AsyncTCPbufferDataCb;
    typedef std::function<void(bool ok, void * ret)> AsyncTCPbufferDoneCb;
    typedef std::function<bool(AsyncTCPbuffer * obj)> AsyncTCPbufferDisconnectCb;

    AsyncTCPbuffer(AsyncClient* c);
    virtual ~AsyncTCPbuffer();

    size_t write(String & data);
    size_t write(uint8_t data);
    size_t write(const char* data);
    size_t write(const char *data, size_t len);
    size_t write(const uint8_t *data, size_t len);

    void flush();

    void noCallback();

    void readStringUntil(char terminator, String * str, AsyncTCPbufferDoneCb done);

    // TODO implement read terminator non string
    //void readBytesUntil(char terminator, char *buffer, size_t length, AsyncTCPbufferDoneCb done);
    //void readBytesUntil(char terminator, uint8_t *buffer, size_t length, AsyncTCPbufferDoneCb done);

    void readBytes(char *buffer, size_t length, AsyncTCPbufferDoneCb done);
    void readBytes(uint8_t *buffer, size_t length, AsyncTCPbufferDoneCb done);

    // TODO implement
    // void setTimeout(size_t timeout);

    void onData(AsyncTCPbufferDataCb cb);
    void onDisconnect(AsyncTCPbufferDisconnectCb cb);

    IPAddress remoteIP();
    uint16_t  remotePort();
    IPAddress localIP();
    uint16_t  localPort();

    bool connected();

    void stop();
    void close();

  protected:
    AsyncClient* _client;
    cbuf * _TXbufferRead;
    cbuf * _TXbufferWrite;
    cbuf * _RXbuffer;
    atbRxMode_t _RXmode;
    size_t _rxSize;
    char _rxTerminator;
    uint8_t * _rxReadBytesPtr;
    String * _rxReadStringPtr;

    AsyncTCPbufferDataCb _cbRX;
    AsyncTCPbufferDoneCb _cbDone;
    AsyncTCPbufferDisconnectCb _cbDisconnect;

    void _attachCallbacks();
    void _sendBuffer();
    void _on_close();
    void _rxData(uint8_t *buf, size_t len);
    size_t _handleRxBuffer(uint8_t *buf, size_t len);
};

#endif /* RPAsyncTCP_BUFFER_H_ */
