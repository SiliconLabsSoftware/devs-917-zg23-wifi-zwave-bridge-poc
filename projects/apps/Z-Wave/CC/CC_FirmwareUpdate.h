/* © 2014 Silicon Laboratories Inc.
 */

#ifndef FIRMWAREUPDATE_H_
#define FIRMWAREUPDATE_H_

#include "Serialapi.h"
#include <stdint.h>

sl_command_handler_codes_t
Fwupdate_Md_CommandHandler(zwave_connection_t *c, uint8_t* pData, uint16_t bDatalen);

/**
 * @}
 */
#endif /* FIRMWAREUPDATE_H_ */
