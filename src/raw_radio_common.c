/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/kernel.h>
#include <nrf.h>
#include <nrf_erratas.h>

#if defined(CONFIG_CLOCK_CONTROL_NRF2)
#include <hal/nrf_lrcconf.h>
#endif

#if NRF54L_ERRATA_20_PRESENT
#include <hal/nrf_power.h>
#endif

#if defined(NRF54LM20A_ENGA_XXAA)
#include <hal/nrf_clock.h>
#endif

#include "conn_time_sync.h"

int radio_hf_clock_start(void)
{
#if defined(CONFIG_CLOCK_CONTROL_NRF)
	int err;
	int res;
	struct onoff_manager *clk_mgr;
	struct onoff_client clk_cli;

	clk_mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (!clk_mgr) {
		return -ENXIO;
	}

	sys_notify_init_spinwait(&clk_cli.notify);

	err = onoff_request(clk_mgr, &clk_cli);
	if (err < 0) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&clk_cli.notify, &res);
		if (!err && res) {
			return res;
		}
	} while (err == -EAGAIN);

#if NRF54L_ERRATA_20_PRESENT
	if (nrf54l_errata_20()) {
		nrf_power_task_trigger(NRF_POWER, NRF_POWER_TASK_CONSTLAT);
	}
#endif

#if defined(NRF54LM20A_ENGA_XXAA)
	nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_PLLSTART);
#endif

	return 0;
#elif defined(CONFIG_CLOCK_CONTROL_NRF2)
	int err;
	int res;
	const struct device *radio_clk_dev =
		DEVICE_DT_GET_OR_NULL(DT_CLOCKS_CTLR(DT_NODELABEL(radio)));
	struct onoff_client radio_cli;

	nrf_lrcconf_poweron_force_set(NRF_LRCCONF010,
				      NRF_LRCCONF_POWER_DOMAIN_1,
				      true);

	sys_notify_init_spinwait(&radio_cli.notify);

	err = nrf_clock_control_request(radio_clk_dev, NULL, &radio_cli);
	if (err < 0) {
		return err;
	}

	do {
		err = sys_notify_fetch_result(&radio_cli.notify, &res);
		if (!err && res) {
			return res;
		}
	} while (err == -EAGAIN);

	nrf_lrcconf_clock_always_run_force_set(NRF_LRCCONF000, 0, true);
	nrf_lrcconf_task_trigger(NRF_LRCCONF000, NRF_LRCCONF_TASK_CLKSTART_0);

	return 0;
#else
	return -ENOTSUP;
#endif
}

void radio_configure_common(void)
{
	NRF_RADIO->SHORTS = 0;
	NRF_RADIO->CRCCNF = 0;
	NRF_RADIO->MODE = RADIO_MODE_MODE_Nrf_1Mbit;
	NRF_RADIO->TXPOWER = RADIO_TX_POWER;
	NRF_RADIO->FREQUENCY = RADIO_CHANNEL;
	NRF_RADIO->BASE0 = RADIO_BASE_ADDRESS;
	NRF_RADIO->PREFIX0 = RADIO_ADDRESS_PREFIX;
	NRF_RADIO->TXADDRESS = 0;
	NRF_RADIO->RXADDRESSES = BIT(0);
	NRF_RADIO->PCNF0 = (8 << RADIO_PCNF0_PLEN_Pos);
	NRF_RADIO->PCNF1 =
		((uint32_t)sizeof(struct radio_payload) << RADIO_PCNF1_MAXLEN_Pos) |
		((uint32_t)sizeof(struct radio_payload) << RADIO_PCNF1_STATLEN_Pos) |
		(0U << RADIO_PCNF1_BALEN_Pos);
	NRF_RADIO->EVENTS_READY = 0;
	NRF_RADIO->EVENTS_END = 0;
	NRF_RADIO->EVENTS_DISABLED = 0;
}
