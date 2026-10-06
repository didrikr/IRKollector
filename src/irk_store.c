#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/shell/shell.h>

#include "host/keys.h"
#include "irk_store.h"
#include "ui.h"

LOG_MODULE_REGISTER(irk_store, LOG_LEVEL_INF);

bool irk_store_find(uint8_t id, const bt_addr_le_t *peer, uint8_t irk[IRK_LEN])
{
	struct bt_keys *keys = bt_keys_find(BT_KEYS_IRK, id, peer);

	if (keys == NULL) {
		return false;
	}

	memcpy(irk, keys->irk.val, IRK_LEN);
	return true;
}

void irk_store_hex_msb(const uint8_t irk[IRK_LEN], char *out)
{
	for (int i = 0; i < IRK_LEN; i++) {
		sprintf(&out[2 * i], "%02X", irk[IRK_LEN - 1 - i]);
	}
}

void irk_store_hex_lsb(const uint8_t irk[IRK_LEN], char *out)
{
	for (int i = 0; i < IRK_LEN; i++) {
		sprintf(&out[2 * i], "%02X", irk[i]);
	}
}

/* ---- Peer metadata ----------------------------------------------------------------------- */

struct peer_meta {
	bool used;
	bt_addr_le_t addr;
	uint32_t seq; /* pairing order, 1 = first */
	uint16_t company;
	uint8_t ll_version;
	char name[IRK_NAME_MAX];
};

static struct peer_meta metas[CONFIG_BT_MAX_PAIRED];
static uint32_t last_seq;

static void meta_key(const bt_addr_le_t *addr, char *out, size_t len)
{
	snprintf(out, len, "irkmeta/%02X%02X%02X%02X%02X%02X%u", addr->a.val[5], addr->a.val[4],
		 addr->a.val[3], addr->a.val[2], addr->a.val[1], addr->a.val[0], addr->type);
}

static struct peer_meta *meta_find(const bt_addr_le_t *addr)
{
	for (size_t i = 0; i < ARRAY_SIZE(metas); i++) {
		if (metas[i].used && bt_addr_le_eq(&metas[i].addr, addr)) {
			return &metas[i];
		}
	}
	return NULL;
}

/* Existing entry, a free one, or (when full) the oldest one. */
static struct peer_meta *meta_get(const bt_addr_le_t *addr)
{
	struct peer_meta *m = meta_find(addr);
	struct peer_meta *oldest = &metas[0];

	if (m != NULL) {
		return m;
	}

	for (size_t i = 0; i < ARRAY_SIZE(metas); i++) {
		if (!metas[i].used) {
			oldest = &metas[i];
			break;
		}
		if (metas[i].seq < oldest->seq) {
			oldest = &metas[i];
		}
	}

	memset(oldest, 0, sizeof(*oldest));
	oldest->used = true;
	bt_addr_le_copy(&oldest->addr, addr);
	oldest->seq = ++last_seq;
	return oldest;
}

static void meta_save(const struct peer_meta *m)
{
	char key[40];

	meta_key(&m->addr, key, sizeof(key));
	settings_save_one(key, m, sizeof(*m));
}

static int meta_set(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	struct peer_meta m;

	if (len != sizeof(m) || read_cb(cb_arg, &m, sizeof(m)) != sizeof(m)) {
		return -EINVAL;
	}

	struct peer_meta *slot = meta_get(&m.addr);

	*slot = m;
	slot->used = true;
	if (m.seq > last_seq) {
		last_seq = m.seq;
	}
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(irkmeta, "irkmeta", NULL, meta_set, NULL, NULL);

void irk_store_set_info(const bt_addr_le_t *peer, uint16_t company, uint8_t ll_version)
{
	struct peer_meta *m = meta_get(peer);

	m->company = company;
	m->ll_version = ll_version;
	meta_save(m);
}

void irk_store_set_name(const bt_addr_le_t *peer, const char *name, size_t len)
{
	struct peer_meta *m = meta_get(peer);

	len = MIN(len, sizeof(m->name) - 1);
	memcpy(m->name, name, len);
	m->name[len] = '\0';
	meta_save(m);
}

void irk_store_forget(const bt_addr_le_t *peer)
{
	struct peer_meta *m = meta_find(peer);
	char key[40];

	if (m == NULL) {
		return;
	}

	meta_key(&m->addr, key, sizeof(key));
	settings_delete(key);
	memset(m, 0, sizeof(*m));
}

static const char *company_name(uint16_t id)
{
	switch (id) {
	case 0x0006: return "Microsoft";
	case 0x004C: return "Apple";
	case 0x0059: return "Nordic Semiconductor";
	case 0x0075: return "Samsung";
	case 0x00E0: return "Google";
	case 0x027D: return "Huawei";
	case 0x038F: return "Xiaomi";
	case 0x0171: return "Amazon";
	case 0x000F: return "Broadcom";
	case 0x000D: return "Texas Instruments";
	case 0x0046: return "MediaTek";
	case 0x000A: return "Qualcomm";
	case 0x001D: return "Qualcomm";
	default: return NULL;
	}
}

static const char *bt_version_name(uint8_t ll_version)
{
	static const char *const names[] = {"4.0", "4.1", "4.2", "5.0", "5.1", "5.2",
					    "5.3", "5.4", "6.0", "6.1", "6.2"};

	if (ll_version >= 6 && ll_version - 6 < ARRAY_SIZE(names)) {
		return names[ll_version - 6];
	}
	return "?";
}

static const char *addr_kind(const bt_addr_le_t *addr)
{
	if (addr->type == BT_ADDR_LE_PUBLIC || addr->type == BT_ADDR_LE_PUBLIC_ID) {
		return "public";
	}
	return BT_ADDR_IS_STATIC(&addr->a) ? "random static" : "random (non-static)";
}

void irk_store_log_manufacturer(const bt_addr_le_t *peer)
{
	const struct peer_meta *m = meta_find(peer);

	if (m != NULL && m->ll_version != 0) {
		const char *co = company_name(m->company);

		LOG_INF("  Manufacturer: %s (0x%04X), Bluetooth %s", co ? co : "unknown",
			m->company, bt_version_name(m->ll_version));
	}
}

#define DESC_LINES    4
#define DESC_LINE_LEN 80

/* Fill `lines` with one human readable fact per line, returns the number of lines. */
static int describe_peer(const bt_addr_le_t *addr, char lines[DESC_LINES][DESC_LINE_LEN])
{
	const struct peer_meta *m = meta_find(addr);
	int n = 0;

	snprintf(lines[n++], DESC_LINE_LEN, "  Address type: %s", addr_kind(addr));
	if (m == NULL) {
		snprintf(lines[n++], DESC_LINE_LEN, "  Device info: not recorded");
		return n;
	}

	snprintf(lines[n++], DESC_LINE_LEN, "  Name: %s",
		 m->name[0] ? m->name : "(not read yet, or not exposed)");
	if (m->ll_version != 0) {
		const char *co = company_name(m->company);

		snprintf(lines[n++], DESC_LINE_LEN, "  Manufacturer: %s (0x%04X), Bluetooth %s",
			 co ? co : "unknown", m->company, bt_version_name(m->ll_version));
	}
	snprintf(lines[n++], DESC_LINE_LEN, "  Paired as device #%u (pairing order)",
		 (unsigned int)m->seq);
	return n;
}

void irk_store_log(const bt_addr_le_t *peer, const uint8_t irk[IRK_LEN])
{
	char addr[BT_ADDR_LE_STR_LEN];
	char msb[2 * IRK_LEN + 1];
	char lsb[2 * IRK_LEN + 1];

	bt_addr_le_to_str(peer, addr, sizeof(addr));
	irk_store_hex_msb(irk, msb);
	irk_store_hex_lsb(irk, lsb);

	LOG_INF("IRK of %s", addr);
	LOG_INF("  MSB first: %s", msb);
	LOG_INF("  LSB first: %s  (as sent over the air)", lsb);

	char lines[DESC_LINES][DESC_LINE_LEN];
	int n = describe_peer(peer, lines);

	for (int i = 0; i < n; i++) {
		LOG_INF("%s", lines[i]);
	}
}

void irk_store_clear_all(void)
{
	int err = bt_unpair(BT_ID_DEFAULT, NULL);

	if (err) {
		LOG_ERR("Failed to clear bonds (err %d)", err);
	} else {
		LOG_INF("All bonds cleared");
	}
}

static void print_key(struct bt_keys *keys, void *data)
{
	const struct shell *sh = data;
	char addr[BT_ADDR_LE_STR_LEN];
	char msb[2 * IRK_LEN + 1];
	char lsb[2 * IRK_LEN + 1];

	bt_addr_le_to_str(&keys->addr, addr, sizeof(addr));
	irk_store_hex_msb(keys->irk.val, msb);
	irk_store_hex_lsb(keys->irk.val, lsb);

	shell_print(sh, "%s", addr);
	shell_print(sh, "  MSB first: %s", msb);
	shell_print(sh, "  LSB first: %s", lsb);

	char lines[DESC_LINES][DESC_LINE_LEN];
	int n = describe_peer(&keys->addr, lines);

	for (int i = 0; i < n; i++) {
		shell_print(sh, "%s", lines[i]);
	}
}

static int cmd_list(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "Stored peer IRKs:");
	bt_keys_foreach_type(BT_KEYS_IRK, print_key, (void *)sh);
	return 0;
}

static int cmd_clear(const struct shell *sh, size_t argc, char **argv)
{
	irk_store_clear_all();
	return 0;
}

static int cmd_own(const struct shell *sh, size_t argc, char **argv)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);
	char str[BT_ADDR_LE_STR_LEN];

	bt_id_get(addrs, &count);
	for (size_t i = 0; i < count; i++) {
		bt_addr_le_to_str(&addrs[i], str, sizeof(str));
		shell_print(sh, "Own identity %zu: %s", i, str);
	}
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(irk_cmds,
	SHELL_CMD(list, NULL, "List the IRKs of all bonded peers", cmd_list),
	SHELL_CMD(clear, NULL, "Remove all bonds and IRKs", cmd_clear),
	SHELL_CMD(own, NULL, "Show the device's own identity address", cmd_own),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(irk, &irk_cmds, "IRK collector commands", NULL);
