/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/sys/atomic.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/services/nus.h>
#include "conn_time_sync.h"
#include <bluetooth/hci_vs_sdc.h>

#define LED_TOGGLE_PERIOD_MS 120

#define LED_TOGGLE_TIME_OFFSET_US 50000

#define ADV_NAME_STR_MAX_LEN (sizeof(CONFIG_BT_DEVICE_NAME))

/* Connection interval in units of 1.25ms */
#define CONN_INTERVAL_UNITS 6  /* 7.5ms for maximum throughput */

/* At 1M baud, UART can transmit ~2500 lines/sec (40 bytes/line)
 * This is sufficient for ~667 packets/sec BLE throughput without overflow
 */

static const struct bt_uuid *data_notify_uuid = BT_UUID_DATA_NOTIFY_CHAR;

static void scan_start(void);

static uint32_t total_packets_received = 0;

static bool led_value;
static uint8_t volatile conn_count;

/* Connection state structure */
static struct {
	/* Data transfer fields */
	uint16_t data_notify_char_handle;
	struct bt_gatt_discover_params discovery_params;
	struct bt_gatt_subscribe_params subscribe_params;
	struct bt_gatt_exchange_params exchange_params;
} conn_state[CONFIG_BT_MAX_CONN];

/* No timestamp sending needed for simple value mode */

/* No periodic timestamp work needed */

static uint8_t notify_data_received(struct bt_conn *conn,
				    struct bt_gatt_subscribe_params *params,
				    const void *data, uint16_t length)
{
	if (!data) {
		printk("[UNSUBSCRIBED]\n");
		params->value_handle = 0U;
		return BT_GATT_ITER_STOP;
	}

	if (length != sizeof(struct peripheral_data)) {
		printk("Invalid data length: %d\n", length);
		return BT_GATT_ITER_CONTINUE;
	}

	struct peripheral_data *received_data = (struct peripheral_data *)data;
	uint8_t conn_index = bt_conn_index(conn);

	/* Print received data */
	total_packets_received++;
	peripheral_data_print(received_data, conn_index);
	
	if ((total_packets_received % 100) == 0) {
		printk("==> Total packets received: %u\n", total_packets_received);
	}

	return BT_GATT_ITER_CONTINUE;
}

static bool adv_data_parse_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;
	uint8_t len;

	switch (data->type) {
	case BT_DATA_NAME_SHORTENED:
	case BT_DATA_NAME_COMPLETE:
		len = MIN(data->data_len, ADV_NAME_STR_MAX_LEN - 1);
		memcpy(name, data->data, len);
		name[len] = '\0';
		return false;
	default:
		return true;
	}
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			 struct net_buf_simple *ad)
{
	char name_str[ADV_NAME_STR_MAX_LEN] = {0};
	char addr_str[BT_ADDR_LE_STR_LEN];
	int err;

	/* We're only interested in connectable events */
	if (type != BT_GAP_ADV_TYPE_ADV_IND &&
	    type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		return;
	}

	bt_data_parse(ad, adv_data_parse_cb, name_str);

	if (strncmp(name_str, CONFIG_BT_DEVICE_NAME, ADV_NAME_STR_MAX_LEN) != 0) {
		return;
	}

	bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
	printk("Device found: %s (RSSI %d)\n", addr_str, rssi);

	if (bt_le_scan_stop()) {
		return;
	}

	struct bt_conn *unused_conn = NULL;

	/* Use minimum connection interval for maximum throughput */
	err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
				BT_LE_CONN_PARAM(CONN_INTERVAL_UNITS, CONN_INTERVAL_UNITS, 0, 400), &unused_conn);
	if (err) {
		printk("Create conn to %s failed (%d)\n", addr_str, err);
		scan_start();
	}

	if (unused_conn) {
		bt_conn_unref(unused_conn);
	}
}

static void scan_start(void)
{
	int err;

	err = bt_le_scan_start(BT_LE_SCAN_ACTIVE_CONTINUOUS, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}

	printk("Scanning started\n");
}

static uint8_t on_service_discover(struct bt_conn *conn,
	const struct bt_gatt_attr *attr,
	struct bt_gatt_discover_params *params)
{
	uint8_t conn_index = bt_conn_index(conn);

	if (attr) {
		struct bt_gatt_chrc *chrc = (struct bt_gatt_chrc *)attr->user_data;
		
		if (bt_uuid_cmp(chrc->uuid, data_notify_uuid) == 0) {
			conn_state[conn_index].data_notify_char_handle =
				bt_gatt_attr_value_handle(attr);
			printk("Data notification characteristic discovered (handle 0x%04x)\n",
			       conn_state[conn_index].data_notify_char_handle);

			/* Subscribe to notifications - bt_gatt_subscribe will discover CCC */
			memset(&conn_state[conn_index].subscribe_params, 0,
			       sizeof(conn_state[conn_index].subscribe_params));
			conn_state[conn_index].subscribe_params.notify = notify_data_received;
			conn_state[conn_index].subscribe_params.value = BT_GATT_CCC_NOTIFY;
			conn_state[conn_index].subscribe_params.value_handle =
				conn_state[conn_index].data_notify_char_handle;
			conn_state[conn_index].subscribe_params.ccc_handle = attr->handle + 2;

			int err = bt_gatt_subscribe(conn, &conn_state[conn_index].subscribe_params);
			if (err && err != -EALREADY) {
				printk("Subscribe failed (err %d)\n", err);
			} else {
				printk("Subscribed to data notifications\n");
			}
		}

		/* Continue discovery until we find the characteristic */
		return BT_GATT_ITER_CONTINUE;
	} else {
		/* Discovery complete */
		if (conn_state[conn_index].data_notify_char_handle == 0) {
			printk("Warning: Data notification characteristic not found\n");
		} else {
			printk("Service discovery completed successfully\n");
		}
	}

	return BT_GATT_ITER_STOP;
}

static void mtu_exchange_cb(struct bt_conn *conn, uint8_t err,
			    struct bt_gatt_exchange_params *params)
{
	if (err) {
		printk("MTU exchange failed (err %d)\n", err);
	} else {
		uint16_t mtu = bt_gatt_get_mtu(conn);
		printk("MTU exchange successful, MTU = %u bytes\n", mtu);
	}
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s 0x%02x %s\n", addr, err, bt_hci_err_to_str(err));

		scan_start();
		return;
	}

	printk("Connected: %s\n", addr);

	uint8_t conn_index = bt_conn_index(conn);

	/* Exchange MTU to enable larger packets */
	conn_state[conn_index].exchange_params.func = mtu_exchange_cb;
	err = bt_gatt_exchange_mtu(conn, &conn_state[conn_index].exchange_params);
	if (err) {
		printk("MTU exchange request failed (err %d)\n", err);
	}

	/* Request 2M PHY for higher throughput */
	struct bt_conn_le_phy_param phy_params = {
		.options = BT_CONN_LE_PHY_OPT_NONE,
		.pref_tx_phy = BT_HCI_LE_PHY_PREFER_2M,
		.pref_rx_phy = BT_HCI_LE_PHY_PREFER_2M,
	};
	err = bt_conn_le_phy_update(conn, &phy_params);
	if (err) {
		printk("PHY update request failed (err %d)\n", err);
	}

	conn_state[conn_index].discovery_params.uuid = NULL;
	conn_state[conn_index].discovery_params.func = on_service_discover;
	conn_state[conn_index].discovery_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
	conn_state[conn_index].discovery_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
	conn_state[conn_index].discovery_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

	err = bt_gatt_discover(conn, &conn_state[conn_index].discovery_params);
	if (err) {
		printk("Discovery failed, %d\n", err);
	}

	const uint8_t peripheral_conn_count = 1;

	conn_count++;
	if (conn_count < CONFIG_BT_MAX_CONN - peripheral_conn_count) {
		scan_start();
	}
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

	conn_count--;

	uint8_t conn_index = bt_conn_index(conn);

	/* Unsubscribe if subscribed */
	if (conn_state[conn_index].subscribe_params.value_handle) {
		bt_gatt_unsubscribe(conn, &conn_state[conn_index].subscribe_params);
	}

	/* Reset state */
	memset(&conn_state[conn_index], 0, sizeof(conn_state[conn_index]));

	const uint8_t peripheral_conn_count = 1;

	if (conn_count == CONFIG_BT_MAX_CONN - peripheral_conn_count - 1) {
		scan_start();
	}
}

static bool on_conn_param_req(struct bt_conn *conn, struct bt_le_conn_param *param)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(param);

	return false;
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
	.le_param_req = on_conn_param_req,
};

static bool on_vs_evt(struct net_buf_simple *buf)
{
	/* Not needed for simple value receiving */
	return false;
}

void central_start(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);

	scan_start();
	
	printk("Central started - scanning for peripherals...\n");
}