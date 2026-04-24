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
#include <esb.h>

#define RADIO_FREQUENCY_MHZ      2400
#define ESB_CHANNEL              10
#define ESB_PIPE                 0
#define ESB_PAYLOAD_LENGTH       2
#define ESB_SEQUENCE_INDEX       0
#define ESB_VALUE_INDEX          1
#define RADIO_SEND_INTERVAL_MS   1000
#define RADIO_VALUE_MIN          1
#define RADIO_VALUE_MAX          10

/** @brief Start central demo. */
void central_start(void);

/** @brief Start peripheral demo. */
void peripheral_start(void);

/** @brief Start the high-frequency clock required by the radio. */
int radio_hf_clock_start(void);

/** @brief Initialize the shared ESB link configuration. */
int esb_link_init(enum esb_mode mode, esb_event_handler event_handler);

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
