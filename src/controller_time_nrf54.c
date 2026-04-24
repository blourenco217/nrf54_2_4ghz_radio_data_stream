/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

/** Stub implementation - not used in raw radio mode */

#include <zephyr/kernel.h>

/* Stub implementations for raw radio mode */

uint64_t controller_time_us_get(void)
{
	/* Return system time in microseconds */
	return k_uptime_ticks() * 1000 / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
}

uint64_t system_time_us_get(void)
{
	/* Return system time in microseconds */
	return k_uptime_ticks() * 1000 / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
}

void controller_time_trigger_set(uint64_t timestamp_us)
{
	/* Not used in raw radio mode */
	ARG_UNUSED(timestamp_us);
}

uint32_t controller_time_trigger_event_addr_get(void)
{
	/* Not used in raw radio mode */
	return 0;
}