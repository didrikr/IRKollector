#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

#include "irk_service.h"
#include "irk_store.h"

LOG_MODULE_REGISTER(irk_service, LOG_LEVEL_INF);

static const struct bt_uuid_128 irk_svc_uuid = BT_UUID_INIT_128(IRK_SVC_UUID_VAL);
static const struct bt_uuid_128 irk_hex_uuid = BT_UUID_INIT_128(IRK_HEX_CHRC_UUID_VAL);
static const struct bt_uuid_128 irk_raw_uuid = BT_UUID_INIT_128(IRK_RAW_CHRC_UUID_VAL);

/* Only ever hand out the IRK of the connected peer itself. */
static bool get_peer_irk(struct bt_conn *conn, uint8_t irk[IRK_LEN])
{
	struct bt_conn_info info;

	if (bt_conn_get_info(conn, &info) != 0 || info.type != BT_CONN_TYPE_LE) {
		return false;
	}

	return irk_store_find(info.id, info.le.dst, irk);
}

static ssize_t read_hex(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			uint16_t len, uint16_t offset)
{
	uint8_t irk[IRK_LEN];
	char hex[2 * IRK_LEN];
	char tmp[2 * IRK_LEN + 1];

	if (!get_peer_irk(conn, irk)) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	irk_store_hex_msb(irk, tmp);
	memcpy(hex, tmp, sizeof(hex));
	return bt_gatt_attr_read(conn, attr, buf, len, offset, hex, sizeof(hex));
}

static ssize_t read_raw(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			uint16_t len, uint16_t offset)
{
	uint8_t irk[IRK_LEN];

	if (!get_peer_irk(conn, irk)) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, irk, sizeof(irk));
}

/* Reads require an encrypted link, so a phone is made to pair before it can read its IRK. */
BT_GATT_SERVICE_DEFINE(irk_svc,
	BT_GATT_PRIMARY_SERVICE(&irk_svc_uuid),
	BT_GATT_CHARACTERISTIC(&irk_hex_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_hex, NULL, NULL),
	BT_GATT_CUD("Your IRK, hex text, MSB first", BT_GATT_PERM_READ),
	BT_GATT_CHARACTERISTIC(&irk_raw_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_raw, NULL, NULL),
	BT_GATT_CUD("Your IRK, raw 16 bytes, LSB first (as on air)", BT_GATT_PERM_READ),
);
