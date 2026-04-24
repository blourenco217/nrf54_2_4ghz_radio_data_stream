/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#ifndef CONN_TIME_SYNC_H__
#define CONN_TIME_SYNC_H__

/**
 * @file
 * @defgroup conn_time_sync Definitions for the 2.4 GHz raw radio sample.
 * @{
 * @brief Definitions for the 2.4 GHz raw radio sample.
 *
 * This file contains common definitions for raw radio communication.
 */

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

/* 2.4 GHz Radio Configuration */
#define RADIO_FREQUENCY_MHZ     2400
#define RADIO_CHANNEL           10
#define RADIO_TX_POWER          0
#define RADIO_ADDRESS_PREFIX    0xA8
#define RADIO_BASE_ADDRESS      0x00000000
#define RADIO_SEND_INTERVAL_MS  1000
#define RADIO_VALUE_MIN         1
#define RADIO_VALUE_MAX         10

/* Raw radio packet stored in RAM. The on-air address comes from BASE0/PREFIX0. */
struct radio_payload {
	uint8_t value;
} __packed;

/** @brief Start central demo. */
void central_start(void);

/** @brief Start peripheral demo. */
void peripheral_start(void);

/** @brief Start the high-frequency clock required by the radio. */
int radio_hf_clock_start(void);

/** @brief Apply the shared raw-radio configuration used by TX and RX. */
void radio_configure_common(void);

static inline bool radio_value_is_valid(uint8_t value)
{
	return (value >= RADIO_VALUE_MIN) && (value <= RADIO_VALUE_MAX);
}

#endif

/**
 * @}
 */

/**
 * @}
 */
