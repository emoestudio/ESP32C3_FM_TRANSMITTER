#ifndef _RP2040W_ASYNC_CONFIG_H_
#define _RP2040W_ASYNC_CONFIG_H_

#ifndef TCP_MSS
  // May have been definded as a -DTCP_MSS option on the compile line or not.
  // Arduino core 2.3.0 or earlier does not do the -DTCP_MSS option.
  // Later versions may set this option with info from board.txt.
  // However, Core 2.4.0 and up board.txt does not define TCP_MSS for lwIP v1.4
  #define TCP_MSS       MBED_CONF_LWIP_TCP_MSS        //(1460)
#endif

#endif // _RP2040W_ASYNC_CONFIG_H_
