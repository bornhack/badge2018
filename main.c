/*
 * This file is part of Bornhack Badge 2018.
 * Copyright 2018 Emil Renner Berthing <esmil@esmil.dk>
 *
 * Bornhack Badge 2018 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bornhack Badge 2018 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bornhack Badge 2018. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "geckonator/clock.h"
#include "geckonator/gpio.h"
#include "geckonator/emu.h"

#include "badge2018.h"

static const struct {
	gpio_pin_t pin;
	uint8_t state;
	uint8_t mode;
} gpio_pins[] = {
	{ LED_POWER,    0, GPIO_MODE_WIREDAND },
	{ LED_BOOT,     1, GPIO_MODE_WIREDAND },
	{ LED_A,        1, GPIO_MODE_WIREDAND },
	{ LED_B,        1, GPIO_MODE_WIREDAND },
	{ BUTTON_POWER, 1, GPIO_MODE_INPUTPULLFILTER },
	{ BUTTON_A,     1, GPIO_MODE_INPUTPULLFILTER },
	{ BUTTON_B,     1, GPIO_MODE_INPUTPULLFILTER },
	{ I2C0_SDA,     1, GPIO_MODE_WIREDAND },
	{ I2C0_SCL,     1, GPIO_MODE_WIREDAND },
};

static void
gpio_init(void)
{
	unsigned int i;

	clock_gpio_enable();
	for (i = 0; i < ARRAY_SIZE(gpio_pins); i++) {
		gpio_pin_t pin = gpio_pins[i].pin;

		if (gpio_pins[i].state)
			gpio_set(pin);
		else
			gpio_clear(pin);
		gpio_mode(pin, gpio_pins[i].mode);
	}
}

static void __noreturn
enter_em4(void)
{
	unsigned int i;

	/* disable all interrupts */
	__disable_irq();

	/* turn off all initialised pins except the POWER button */
	for (i = 0; i < ARRAY_SIZE(gpio_pins); i++) {
		gpio_pin_t pin = gpio_pins[i].pin;

		if (gpio_pin_eq(pin, BUTTON_POWER))
			continue;

		gpio_mode(pin, GPIO_MODE_DISABLED);
		gpio_clear(pin);
	}

	/* clear all wake-up requests */
	gpio_wakeup_clear();
	/* enable EM4 retention */
	gpio_retention_enable();
	/* wake up when pin goes low */
	gpio_wakeup_rising(0);
	/* enable EM4 wake-up from PC4 */
	gpio_wakeup_pins(GPIO_WAKEUP_PC4);

	/* do the EM4 handshake */
	emu_em4_enter();
}

static gpio_pin_t buttons[] = {
	BUTTON_POWER,
	BUTTON_A,
	BUTTON_B,
};

static void
buttons_init(void)
{
	unsigned int i;

	NVIC_SetPriority(GPIO_EVEN_IRQn, 3);
	NVIC_SetPriority(GPIO_ODD_IRQn, 3);
	NVIC_EnableIRQ(GPIO_EVEN_IRQn);
	NVIC_EnableIRQ(GPIO_ODD_IRQn);

	for (i = 0; i < ARRAY_SIZE(buttons); i++) {
		gpio_pin_t pin = buttons[i];

		gpio_flag_select(pin);
		gpio_flag_rising_enable(pin);
		gpio_flag_falling_enable(pin);
		gpio_flag_clear(pin);
		gpio_flag_enable(pin);
	}
}

static void
gpio_handler(void)
{
	static bool power_pressed;
	uint32_t flags = gpio_flags();

	gpio_flags_clear(flags);

	if (gpio_in(BUTTON_POWER)) {
		if (power_pressed)
			enter_em4();
	} else {
		gpio_set(LED_POWER);
		power_pressed = true;
	}

	if (gpio_in(BUTTON_A))
		gpio_set(LED_A);
	else
		gpio_clear(LED_A);

	if (gpio_in(BUTTON_B))
		gpio_set(LED_B);
	else
		gpio_clear(LED_B);
}

void
GPIO_EVEN_IRQHandler(void)
{
	gpio_handler();
}

void
GPIO_ODD_IRQHandler(void)
{
	gpio_handler();
}

void __noreturn
main(void)
{
	/* enable and configure GPIOs */
	gpio_init();

	/* wait until POWER button is released */
	while (!gpio_in(BUTTON_POWER))
		/* wait */;

	/* configure buttons */
	buttons_init();

	/* sleep when not interrupted */
	while (1) {
		__WFI();
	}
}
