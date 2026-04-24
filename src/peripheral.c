/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <esb.h>

#include "conn_time_sync.h"

static volatile bool tx_ready = true;

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
	uint8_t sequence = 0;
	uint8_t value = RADIO_VALUE_MIN;

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

	payload.length = ESB_PAYLOAD_LENGTH;
	payload.pipe = ESB_PIPE;
	payload.noack = 1;

	while (1) {
		if (!tx_ready) {
			k_sleep(K_MSEC(10));
			continue;
		}

		payload.data[ESB_SEQUENCE_INDEX] = sequence;
		payload.data[ESB_VALUE_INDEX] = value;

		tx_ready = false;
		esb_flush_tx();
		err = esb_write_payload(&payload);
		if (err) {
			tx_ready = true;
			printk("ESB TX enqueue failed: %d\n", err);
			k_sleep(K_MSEC(100));
			continue;
		}

		printk("TX seq=%u value=%u\n", sequence, value);

		sequence++;
		value++;
		if (value > RADIO_VALUE_MAX) {
			value = RADIO_VALUE_MIN;
		}

		k_sleep(K_MSEC(RADIO_SEND_INTERVAL_MS));
	}
}
