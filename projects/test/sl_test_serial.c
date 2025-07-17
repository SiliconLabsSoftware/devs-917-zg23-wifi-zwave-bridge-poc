#include "FreeRTOS.h"
#include "../apps/SerialAPI/sl_serial.h"
#include "rsi_common_apis.h"
#include "sl_common_log.h"

#define FUNC_ID_SERIAL_API_GET_CAPABILITIES 0x07

void sl_test_serial_api(void)
{
  int ret;
  // init serial
  sl_serial_init("USART0");

  for (int i = 0; i < 10; i++) {
    // send frame.
    sl_serial_tx_frame(FUNC_ID_SERIAL_API_GET_CAPABILITIES, REQUEST, 0, 0);
    // receive frame.
    ret = sl_serial_rx_frame(1);
    if (conFrameSent == ret) {
      SL_LOG_PRINT("fram sent\n");
      // wait respond.
      ret = sl_serial_rx_frame(1);
      if (conFrameReceived == ret) {
        uint8_t buf[255];
        uint32_t len;
        len = sl_serial_get_buf(buf);
        sl_print_hex_buf(buf, len);
      }
    } else {
      SL_LOG_PRINT("no ack response\n");
    }
    osDelay(1000);
  }
}
