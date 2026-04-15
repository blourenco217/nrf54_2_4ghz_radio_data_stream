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
#include <bluetooth/services/nus_client.h>
#include <bluetooth/hci_vs_sdc.h>

#include "conn_time_sync.h"

#define DATA_SEND_INTERVAL_MS 10   /* Retry interval when link/buffers are not ready */

static struct bt_conn *notify_conn;
static bool notif_subscribed;
static uint8_t current_value = 1;  /* Current value to send (1-10) */
static atomic_t notify_in_flight;
static struct peripheral_data notify_data;
static struct bt_gatt_notify_params notify_params;

static void data_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);

	notif_subscribed = (value == BT_GATT_CCC_NOTIFY);

	printk("Data notifications %s\n", notif_subscribed ? "enabled" : "disabled");
}

/* Forward declarations */
static void data_notify_complete(struct bt_conn *conn, void *user_data);

/* Forward declaration for GATT service definition */
static ssize_t data_write_handler(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr,
				   const void *buf, uint16_t len,
				   uint16_t offset, uint8_t flags);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

BT_GATT_SERVICE_DEFINE(conn_time_sync_service,
	BT_GATT_PRIMARY_SERVICE(BT_UUID_CONN_TIME_SYNC_SERVICE),
	/* Data notification characteristic - sends data to central */
	BT_GATT_CHARACTERISTIC(BT_UUID_DATA_NOTIFY_CHAR,
		BT_GATT_CHRC_NOTIFY,
		BT_GATT_PERM_READ, NULL, NULL, NULL),
	BT_GATT_CCC(data_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static ssize_t data_write_handler(struct bt_conn *conn,
				   const struct bt_gatt_attr *attr,
				   const void *buf, uint16_t len,
				   uint16_t offset, uint8_t flags)
{
	/* Placeholder for potential write requests. For now, just accept and ignore. */
	return len;
}

static void adv_start(void)
{
	int err;

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		printk("Advertising failed to start (err %d)\n", err);
		return;
	}

	printk("Advertising started\n");
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err) {
		printk("Failed to connect to %s 0x%02x %s\n", addr, err, bt_hci_err_to_str(err));

		adv_start();
		return;
	}

	printk("Connected: %s\n", addr);

	notify_conn = bt_conn_ref(conn);

	// /* Request minimum connection interval for maximum throughput */
	// struct bt_le_conn_param param = {
	// 	.interval_min = 6,  /* 7.5ms */
	// 	.interval_max = 6,  /* 7.5ms */
	// 	.latency = 0,       /* No latency */
	// 	.timeout = 400      /* 4s timeout */
	// };
	
	// err = bt_conn_le_param_update(conn, &param);
	// if (err) {
	// 	printk("Failed to update conn params (err %d)\n", err);
	// }

	// /* Request 2M PHY for maximum throughput */
	// struct bt_conn_le_phy_param phy_param = {
	// 	.options = BT_CONN_LE_PHY_OPT_NONE,
	// 	.pref_tx_phy = BT_HCI_LE_PHY_PREFER_2M,
	// 	.pref_rx_phy = BT_HCI_LE_PHY_PREFER_2M,
	// };
	// err = bt_conn_le_phy_update(conn, &phy_param);
	// if (err) {
	// 	printk("PHY update failed (err %d)\n", err);
	// }

	/* Log MTU after connection (will be updated after exchange) */
	uint16_t mtu = bt_gatt_get_mtu(conn);
	printk("Initial MTU = %u bytes\n", mtu);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	printk("Disconnected: %s, reason 0x%02x %s\n", addr, reason, bt_hci_err_to_str(reason));

	if (notify_conn) {
		bt_conn_unref(notify_conn);
		notify_conn = NULL;
	}

	notif_subscribed = false;
	atomic_clear_bit(&notify_in_flight, 0);

	adv_start();
}

static struct bt_conn_cb conn_callbacks = {
	.connected = connected,
	.disconnected = disconnected,
};

static uint64_t central_timestamp_to_local_clock(uint64_t central_timestamp_us,
						 uint16_t conn_interval_us,
						 uint64_t central_anchor_us,
						 uint64_t peripheral_anchor_us,
						 uint16_t central_event_count,
						 uint16_t peripheral_event_count)
{
	int32_t event_counter_diff = central_event_count - peripheral_event_count;
	int32_t anchor_point_time_diff_us = event_counter_diff * conn_interval_us;
	uint64_t peripheral_time_us_at_central_anchor =
		peripheral_anchor_us + anchor_point_time_diff_us;
	int64_t anchor_to_timestamp = central_timestamp_us - central_anchor_us;

	return peripheral_time_us_at_central_anchor + anchor_to_timestamp;
}

static bool on_vs_evt(struct net_buf_simple *buf)
{
	/* Not needed for simple value sending */
	return false;
}


static void send_data_notification(struct k_work *work)
{
	if (notify_conn && notif_subscribed) {
		if (atomic_test_and_set_bit(&notify_in_flight, 0)) {
			goto reschedule;
		}

		/* Check if MTU is large enough for our packet */
		uint16_t mtu = bt_gatt_get_mtu(notify_conn);
		if (mtu < (sizeof(struct peripheral_data) + 3)) {
			printk("MTU too small (%u bytes)\n", mtu);
			atomic_clear_bit(&notify_in_flight, 0);
			goto reschedule;
		}

		/* Send the current value (1-10) */
		notify_data.value = current_value;

		notify_params.attr = &conn_time_sync_service.attrs[2];
		notify_params.data = &notify_data;
		notify_params.len = sizeof(notify_data);
		notify_params.func = data_notify_complete;
		notify_params.user_data = NULL;

		int err = bt_gatt_notify_cb(notify_conn, &notify_params);
		if (err) {
			atomic_clear_bit(&notify_in_flight, 0);
			if (err != -ENOMEM) {
				printk("Notify send error %d\n", err);
			}
			goto reschedule;
		}

		/* Move to next value (1-10, wrap around) */
		current_value++;
		if (current_value > 10) {
			current_value = 1;
		}
		printk("Sent value: %u\n", current_value - 1);

		/* Next packet is queued from data_notify_complete(). */
		return;
	}

reschedule:
	k_work_schedule(k_work_delayable_from_work(work), K_MSEC(DATA_SEND_INTERVAL_MS));
}

K_WORK_DELAYABLE_DEFINE(data_send_work, send_data_notification);

static void data_notify_complete(struct bt_conn *conn, void *user_data)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(user_data);

	atomic_clear_bit(&notify_in_flight, 0);

	/* Pace by completion: queue the next notification only after TX completion. */
	k_work_schedule(&data_send_work, K_NO_WAIT);
}

void peripheral_start(void)
{
	int err;

	err = bt_enable(NULL);
	if (err) {
		printk("Bluetooth init failed (err %d)\n", err);
		return;
	}

	bt_conn_cb_register(&conn_callbacks);

	adv_start();

	/* Start sending data notifications periodically */
	k_work_schedule(&data_send_work, K_MSEC(DATA_SEND_INTERVAL_MS));

	printk("Peripheral started - sending integers 1-10 repeatedly\n");
}