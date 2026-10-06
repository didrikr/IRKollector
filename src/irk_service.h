#ifndef IRK_SERVICE_H_
#define IRK_SERVICE_H_

#include <zephyr/bluetooth/uuid.h>

/* Custom GATT service that lets a bonded peer read back its own IRK, e.g. with nRF Connect.
 * ASCII-encodes to "IRK0Collector" + 000001.
 */
#define IRK_SVC_UUID_VAL     BT_UUID_128_ENCODE(0x49524b30, 0x436f, 0x6c6c, 0x6563, 0x746f72000001)
#define IRK_HEX_CHRC_UUID_VAL BT_UUID_128_ENCODE(0x49524b30, 0x436f, 0x6c6c, 0x6563, 0x746f72000002)
#define IRK_RAW_CHRC_UUID_VAL BT_UUID_128_ENCODE(0x49524b30, 0x436f, 0x6c6c, 0x6563, 0x746f72000003)

#endif /* IRK_SERVICE_H_ */
