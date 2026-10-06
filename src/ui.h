#ifndef UI_H_
#define UI_H_

#include <stdbool.h>

/* LED + button. The LED pulses once a second while advertising (discoverable), is solid while a
 * phone is connected, and blinks quickly when an IRK was captured.
 * Holding the button for UI_CLEAR_HOLD_MS clears all bonds.
 */
#define UI_CLEAR_HOLD_MS 3000

void ui_init(void);
void ui_set_connected(bool connected);
void ui_set_advertising(bool advertising);
void ui_irk_captured(void);

#endif /* UI_H_ */
