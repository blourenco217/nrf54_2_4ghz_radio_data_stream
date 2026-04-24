/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <esb.h>

#include "conn_time_sync.h"

static struct {
	uint32_t received_count;
	uint8_t expected_sequence;
} rx_state;

static void event_handler(struct esb_evt const *event)
{
	struct esb_payload payload;
	struct radio_packet packet;

	if (event->evt_id != ESB_EVENT_RX_RECEIVED) {
		return;
	}

	while (esb_read_rx_payload(&payload) == 0) {
		if (!radio_packet_decode(&payload, &packet)) {
			continue;
		}

		rx_state.received_count++;
		printk("RX[%u] seq=%u value=%u%s\n",
		       rx_state.received_count,
		       packet.sequence,
		       packet.value,
		       (packet.sequence == rx_state.expected_sequence) ? "" : " (seq jump)");
		rx_state.expected_sequence = (uint8_t)(packet.sequence + 1U);
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
