#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "irk_service.h"
#include "irk_store.h"
#include "ui.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* The local name goes into the advertising data itself (not only the scan response): iOS lists
 * nearby devices in Settings based on the advertised name. It carries the last two address bytes
 * so that several IRKollectors can be told apart (and iOS does not reuse a cached name).
 */
static char dev_name[CONFIG_BT_DEVICE_NAME_MAX];

static struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_GAP_APPEARANCE, BT_BYTES_LIST_LE16(CONFIG_BT_DEVICE_APPEARANCE)),
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
		      BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
	BT_DATA(BT_DATA_NAME_COMPLETE, NULL, 0), /* filled in by init_name() */
};

static const struct bt_data sd[] = {
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, IRK_SVC_UUID_VAL),
};

static void init_name(void)
{
	bt_addr_le_t id;
	size_t count = 1;

	bt_id_get(&id, &count);
	snprintk(dev_name, sizeof(dev_name), "%s-%02X%02X", CONFIG_BT_DEVICE_NAME, id.a.val[1],
		 id.a.val[0]);
	bt_set_name(dev_name);

	ad[ARRAY_SIZE(ad) - 1].data = (const uint8_t *)dev_name;
	ad[ARRAY_SIZE(ad) - 1].data_len = strlen(dev_name);
}

static unsigned int num_connections;

static void adv_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (err == -ENOMEM) {
		/* Out of connection slots: not discoverable until one is freed (see recycled()). */
		ui_set_advertising(false);
		return;
	}
	if (err && err != -EALREADY) {
		LOG_WRN("Advertising failed to start (err %d)", err);
		ui_set_advertising(false);
		return;
	}

	ui_set_advertising(true);
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		LOG_WRN("Connection to %s failed (err 0x%02x)", addr, err);
		return;
	}

	num_connections++;
	ui_set_connected(true);
	LOG_INF("Connected: %s", addr);

	/* Allow a second phone while the first one is still connected. */
	if (num_connections < CONFIG_BT_MAX_CONN) {
		adv_start();
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

	if (num_connections > 0) {
		num_connections--;
	}
	ui_set_connected(num_connections > 0);
}

/* The remote version info (manufacturer, Bluetooth version) often arrives after pairing. */
static void remote_info_available(struct bt_conn *conn, struct bt_conn_remote_info *remote)
{
	struct bt_conn_info info;
	uint8_t irk[IRK_LEN];

	if (remote->type != BT_CONN_TYPE_LE || bt_conn_get_info(conn, &info) != 0 ||
	    !irk_store_find(info.id, info.le.dst, irk)) {
		return;
	}

	irk_store_set_info(info.le.dst, remote->manufacturer, remote->version);
	irk_store_log_manufacturer(info.le.dst);
}

static void recycled(void)
{
	adv_start();
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
	.remote_info_available = remote_info_available,
};

/* Reading the phone's GAP Device Name is asynchronous; one context per possible connection. */
struct name_read {
	struct bt_gatt_read_params params;
	bt_addr_le_t peer;
	char name[IRK_NAME_MAX];
	size_t len;
};

static struct name_read name_reads[CONFIG_BT_MAX_CONN];

static uint8_t name_read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
			    const void *data, uint16_t length)
{
	struct name_read *r = CONTAINER_OF(params, struct name_read, params);

	if (err == 0 && data != NULL) {
		size_t n = MIN(length, sizeof(r->name) - 1 - r->len);

		memcpy(&r->name[r->len], data, n);
		r->len += n;
		return BT_GATT_ITER_CONTINUE;
	}

	if (r->len > 0) {
		irk_store_set_name(&r->peer, r->name, r->len);
		LOG_INF("  Name: %s", r->name);
	}
	return BT_GATT_ITER_STOP;
}

static void read_peer_name(struct bt_conn *conn, const bt_addr_le_t *peer)
{
	struct name_read *r = &name_reads[bt_conn_index(conn)];

	memset(r, 0, sizeof(*r));
	bt_addr_le_copy(&r->peer, peer);
	r->params.func = name_read_cb;
	r->params.handle_count = 0;
	r->params.by_uuid.uuid = BT_UUID_GAP_DEVICE_NAME;
	r->params.by_uuid.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	r->params.by_uuid.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;

	int err = bt_gatt_read(conn, &r->params);

	if (err) {
		LOG_DBG("Could not read the peer's device name (err %d)", err);
	}
}

static void pairing_complete(struct bt_conn *conn, bool bonded)
{
	struct bt_conn_info info;
	struct bt_conn_remote_info remote;
	uint8_t irk[IRK_LEN];

	if (bt_conn_get_info(conn, &info) != 0) {
		return;
	}

	if (!irk_store_find(info.id, info.le.dst, irk)) {
		LOG_WRN("Paired (bonded: %d) but the peer did not distribute an IRK", bonded);
		return;
	}

	if (bt_conn_get_remote_info(conn, &remote) == 0 && remote.type == BT_CONN_TYPE_LE) {
		irk_store_set_info(info.le.dst, remote.manufacturer, remote.version);
	}

	irk_store_log(info.le.dst, irk);
	ui_irk_captured();
	read_peer_name(conn, info.le.dst);
}

static void pairing_failed(struct bt_conn *conn, enum bt_security_err reason)
{
	LOG_WRN("Pairing failed (reason %d)", reason);
}

static void bond_deleted(uint8_t id, const bt_addr_le_t *peer)
{
	irk_store_forget(peer);
}

static struct bt_conn_auth_info_cb auth_info_cbs = {
	.pairing_complete = pairing_complete,
	.pairing_failed = pairing_failed,
	.bond_deleted = bond_deleted,
};

int main(void)
{
	int err;

	ui_init();

	err = bt_conn_auth_info_cb_register(&auth_info_cbs);
	if (err) {
		LOG_ERR("Registering auth info callbacks failed (err %d)", err);
		return 0;
	}

	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return 0;
	}

	settings_load();
	init_name();

	adv_start();
	LOG_INF("IRKollector ready. Pair with \"%s\"; use `irk list` to read stored IRKs.",
		dev_name);

	return 0;
}
