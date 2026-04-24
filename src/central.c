/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <nrf.h>

#include "conn_time_sync.h"

static struct radio_payload radio_rx_packet __aligned(4);

void central_start(void)
{
	uint32_t packets_received = 0;

	printk("Starting central raw RX on %u MHz\n",
	       RADIO_FREQUENCY_MHZ + RADIO_CHANNEL);

	if (radio_hf_clock_start() != 0) {
		printk("Failed to start the HF clock for radio RX\n");
		return;
	}

	radio_configure_common();
	NRF_RADIO->PACKETPTR = (uint32_t)&radio_rx_packet;
	NRF_RADIO->SHORTS = RADIO_SHORTS_RXREADY_START_Msk |
			    RADIO_SHORTS_END_START_Msk;
	NRF_RADIO->TASKS_RXEN = 1;

	printk("Listening for values %u..%u on channel %u\n",
	       RADIO_VALUE_MIN, RADIO_VALUE_MAX, RADIO_CHANNEL);

	while (1) {
		if (NRF_RADIO->EVENTS_END) {
			uint8_t value = radio_rx_packet.value;

			NRF_RADIO->EVENTS_END = 0;

			if (radio_value_is_valid(value)) {
				packets_received++;
				printk("RX[%u] value=%u\n", packets_received, value);
			}
			// } else {
			// 	printk("RX ignored invalid value=%u\n", value);
			// }
		} else {
			k_sleep(K_MSEC(1));
		}
	}
}
