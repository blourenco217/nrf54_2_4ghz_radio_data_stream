/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr/kernel.h>
#include <zephyr/console/console.h>
#include "conn_time_sync.h"

int main(void)
{
	char role;
	
	console_init();

	do {
		printk("Choose device role - type c (central) or p (peripheral): ");

		role = console_getchar();
		
		/* Echo the character */
		printk("%c\n", role);

		switch (role) {
		case 'p':
		case 'P':
			printk("Peripheral. Starting ESB transmitter\n");
			peripheral_start();
			break;
		case 'c':
		case 'C':
			printk("Central. Starting ESB receiver\n");
			central_start();
			break;
		case '\n':
		case '\r':
			/* Ignore newline characters, loop again */
			break;
		default:
			printk("Invalid choice '%c'. Please type 'c' or 'p'\n", role);
			break;
		}
	} while (role != 'c' && role != 'C' && role != 'p' && role != 'P');
	
	return 0;
}
