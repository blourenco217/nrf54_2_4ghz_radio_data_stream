/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/* Stub implementation - not used in raw radio mode */

int timed_led_toggle_init(void)
{
	/* No LED toggling required for raw radio demo */
	return 0;
}

void timed_led_toggle_trigger_at(uint8_t value, uint32_t timestamp_us)
{
	/* No LED toggling required for raw radio demo */
	ARG_UNUSED(value);
	ARG_UNUSED(timestamp_us);
}