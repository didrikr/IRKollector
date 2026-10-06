#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

#include "hid_service.h"

static const uint8_t hid_info[] = {
	0x11, 0x01, /* bcdHID 1.11 */
	0x00,       /* bCountryCode */
	0x02,       /* flags: normally connectable */
};

static const uint8_t report_map[] = {
	0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, /* Generic Desktop, Keyboard, Application */
	0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, /* Modifier keys */
	0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
	0x95, 0x01, 0x75, 0x08, 0x81, 0x01, /* Reserved byte */
	0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, /* LEDs */
	0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
	0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, /* Key array */
	0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
	0xC0,
};

static const uint8_t input_report_ref[] = {0x00, 0x01}; /* report ID 0, input */
static uint8_t input_report[8];

static ssize_t read_const(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	/* attr->user_data points at a bt_gatt_chrc-independent descriptor, see below */
	return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
				 attr->user_data == report_map ? sizeof(report_map) :
				 attr->user_data == hid_info ? sizeof(hid_info) :
				 sizeof(input_report_ref));
}

static ssize_t read_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			   uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, input_report, sizeof(input_report));
}

static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	return len;
}

static void report_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
}

/* Every access requires encryption: that is what makes the phone pair when the user taps the
 * device in the Bluetooth menu.
 */
BT_GATT_SERVICE_DEFINE(hid_svc,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
			       read_const, NULL, (void *)hid_info),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ_ENCRYPT, read_const, NULL, (void *)report_map),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ_ENCRYPT, read_report, NULL, NULL),
	BT_GATT_CCC(report_ccc_changed, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
	BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_const, NULL,
			   (void *)input_report_ref),
	BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, write_ctrl_point, NULL),
);
