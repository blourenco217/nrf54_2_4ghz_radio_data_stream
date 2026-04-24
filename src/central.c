/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <esb.h>

#include "conn_time_sync.h"

static void event_handler(struct esb_evt const *event)
{
	struct esb_payload payload;

	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	while (esb_read_rx_payload(&payload) == 0) {
		static uint32_t packets_received;
		static uint8_t expected_sequence;
		uint8_t sequence;
		uint8_t value;

		if (payload.length != ESB_PAYLOAD_LENGTH) {
			continue;
		}

		sequence = payload.data[ESB_SEQUENCE_INDEX];
		value = payload.data[ESB_VALUE_INDEX];
		if (!radio_value_is_valid(value)) {
			continue;
		}

		packets_received++;
		printk("RX[%u] seq=%u value=%u%s\n",
		       packets_received,
		       sequence,
		       value,
		       (sequence == expected_sequence) ? "" : " (seq jump)");
		expected_sequence = (uint8_t)(sequence + 1U);
	}
}

void central_start(void)
{
	int err;

	printk("Starting central ESB RX on %u MHz\n",
	       RADIO_FREQUENCY_MHZ + ESB_CHANNEL);

	if (radio_hf_clock_start() != 0) {
		printk("Failed to start the HF clock for ESB RX\n");
		return;
	}

	err = esb_link_init(ESB_MODE_PRX, event_handler);
	if (err) {
		printk("ESB init failed: %d\n", err);
		return;
	}

	err = esb_start_rx();
	if (err) {
		printk("ESB RX start failed: %d\n", err);
		return;
	}

	printk("Listening for ESB values %u..%u on channel %u\n",
	       RADIO_VALUE_MIN, RADIO_VALUE_MAX, ESB_CHANNEL);

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
