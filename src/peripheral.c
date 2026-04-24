/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <esb.h>

#include "conn_time_sync.h"

static volatile bool tx_ready = true;
static struct radio_packet tx_packet = {
	.sequence = 0,
	.value = RADIO_VALUE_MIN,
};

static void event_handler(struct esb_evt const *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
	case ESB_EVENT_TX_FAILED:
		tx_ready = true;
		break;
	case ESB_EVENT_RX_RECEIVED:
		break;
	}
}

void peripheral_start(void)
{
	int err;
	struct esb_payload payload = {0};

	printk("Starting peripheral ESB TX on %u MHz\n",
	       RADIO_FREQUENCY_MHZ + ESB_CHANNEL);

	if (radio_hf_clock_start() != 0) {
		printk("Failed to start the HF clock for ESB TX\n");
		return;
	}

	err = esb_link_init(ESB_MODE_PTX, event_handler);
	if (err) {
		printk("ESB init failed: %d\n", err);
		return;
	}

	while (1) {
		if (!tx_ready) {
			k_sleep(K_MSEC(10));
			continue;
		}

		payload.noack = 1;
		radio_packet_encode(&payload, &tx_packet);
		tx_ready = false;
		esb_flush_tx();
		err = esb_write_payload(&payload);
		if (err) {
			tx_ready = true;
			printk("ESB TX enqueue failed: %d\n", err);
			k_sleep(K_MSEC(100));
			continue;
		}

		printk("TX seq=%u value=%u\n", tx_packet.sequence, tx_packet.value);

		tx_packet.sequence++;
		tx_packet.value++;
		if (tx_packet.value > RADIO_VALUE_MAX) {
			tx_packet.value = RADIO_VALUE_MIN;
		}

		k_sleep(K_MSEC(RADIO_SEND_INTERVAL_MS));
	}
}
