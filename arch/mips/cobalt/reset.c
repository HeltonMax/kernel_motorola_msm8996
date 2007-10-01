/*
 * Cobalt Reset operations
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 * Copyright (C) 2001 by Liam Davies (ldavies@agile.tv)
 */
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/leds.h>

#include <cobalt.h>

DEFINE_LED_TRIGGER(power_off_led_trigger);

static int __init ledtrig_power_off_init(void)
{
	led_trigger_register_simple("power-off", &power_off_led_trigger);
	return 0;
}
device_initcall(ledtrig_power_off_init);

void cobalt_machine_halt(void)
{
	int state, last, diff;
	unsigned long mark;

	/*
	 * turn on power off LED on RaQ
	 *
	 * restart if ENTER and SELECT are pressed
	 */

	last = COBALT_KEY_PORT;

	led_trigger_event(power_off_led_trigger, LED_FULL);

	for (state = 0;;) {
		diff = COBALT_KEY_PORT ^ last;
		last ^= diff;

		if((diff & (COBALT_KEY_ENTER | COBALT_KEY_SELECT)) && !(~last & (COBALT_KEY_ENTER | COBALT_KEY_SELECT)))
			COBALT_LED_PORT = COBALT_LED_RESET;

		for (mark = jiffies; jiffies - mark < HZ;)
			;
	}
}

void cobalt_machine_restart(char *command)
{
	COBALT_LED_PORT = COBALT_LED_RESET;

	/* we should never get here */
	cobalt_machine_halt();
}

/*
 * This triggers the luser mode device driver for the power switch ;-)
 */
void cobalt_machine_power_off(void)
{
	printk("You can switch the machine off now.\n");
	cobalt_machine_halt();
}
