/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <nrf.h>

#include "conn_time_sync.h"

#if defined(RADIO_SHORTS_END_DISABLE_Msk)
#define RADIO_TX_DISABLE_SHORT RADIO_SHORTS_END_DISABLE_Msk
#elif defined(RADIO_SHORTS_PHYEND_DISABLE_Msk)
#define RADIO_TX_DISABLE_SHORT RADIO_SHORTS_PHYEND_DISABLE_Msk
#else
#error "No supported radio TX disable shortcut for this SoC"
#endif

static struct radio_payload radio_tx_packet __aligned(4);

static void send_packet(uint8_t value)
{
	radio_tx_packet.value = value;
	NRF_RADIO->PACKETPTR = (uint32_t)&radio_tx_packet;
	NRF_RADIO->EVENTS_END = 0;
	NRF_RADIO->EVENTS_DISABLED = 0;
	NRF_RADIO->TASKS_TXEN = 1;

	while (!NRF_RADIO->EVENTS_END) {
		k_busy_wait(10);
	}

	while (!NRF_RADIO->EVENTS_DISABLED) {
		k_busy_wait(10);
	}
}

void peripheral_start(void)
{
	uint8_t value = RADIO_VALUE_MIN;

	printk("Starting peripheral raw TX on %u MHz\n",
	       RADIO_FREQUENCY_MHZ + RADIO_CHANNEL);

	if (radio_hf_clock_start() != 0) {
		printk("Failed to start the HF clock for radio TX\n");
		return;
	}

	radio_configure_common();
	NRF_RADIO->SHORTS = RADIO_SHORTS_TXREADY_START_Msk |
			    RADIO_TX_DISABLE_SHORT;

	while (1) {
		send_packet(value);
		printk("TX value=%u\n", value);

		value++;
		if (value > RADIO_VALUE_MAX) {
			value = RADIO_VALUE_MIN;
		}

		k_sleep(K_MSEC(RADIO_SEND_INTERVAL_MS));
	}
}
