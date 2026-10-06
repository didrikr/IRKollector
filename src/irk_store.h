#ifndef IRK_STORE_H_
#define IRK_STORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/bluetooth/addr.h>

#define IRK_LEN 16

/* Look up the IRK distributed by `peer` during pairing. The IRK is returned in the order Zephyr
 * stores it, which is the order on air (least significant byte first).
 */
bool irk_store_find(uint8_t id, const bt_addr_le_t *peer, uint8_t irk[IRK_LEN]);

/* Hex string, most significant byte first (i.e. the stored bytes reversed). `out` must hold
 * 2 * IRK_LEN + 1 bytes.
 */
void irk_store_hex_msb(const uint8_t irk[IRK_LEN], char *out);

/* Hex string in stored/on-air order (least significant byte first). */
void irk_store_hex_lsb(const uint8_t irk[IRK_LEN], char *out);

/* Log a captured IRK, in both byte orders, together with what is known about the peer. */
void irk_store_log(const bt_addr_le_t *peer, const uint8_t irk[IRK_LEN]);

/* Peer metadata, kept in flash next to the bond. Set at pairing; an empty name or a zero version
 * means "not known (yet)".
 */
#define IRK_NAME_MAX 32

void irk_store_set_info(const bt_addr_le_t *peer, uint16_t company, uint8_t ll_version);
void irk_store_set_name(const bt_addr_le_t *peer, const char *name, size_t len);
/* Log just the manufacturer/version line, for info that arrives after the IRK was logged. */
void irk_store_log_manufacturer(const bt_addr_le_t *peer);
void irk_store_forget(const bt_addr_le_t *peer);

/* Remove all bonds (and with them all stored IRKs). */
void irk_store_clear_all(void);

#endif /* IRK_STORE_H_ */
