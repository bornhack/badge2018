/*
 * This file is part of Bornhack Badge 2018.
 * Copyright 2017,2018 Emil Renner Berthing <esmil@esmil.dk>
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

#include "geckonator/clock.h"
#include "geckonator/emu.h"
#include "geckonator/i2c0.h"

static struct {
	const uint8_t *data;
	size_t len;
	volatile int ret;
} i2c0;

void
I2C0_IRQHandler(void)
{
	uint32_t flags = i2c0_flags();

	i2c0_flags_clear(flags);

	if (i2c0_flag_nack(flags)) {
		i2c0_flag_tx_buffer_level_disable();
		i2c0_stop();
		i2c0_clear_tx();
		i2c0.len = 1;
		return;
	}

	if (i2c0_flag_master_stop(flags)) {
		i2c0.ret = -i2c0.len;
		return;
	}

	if (i2c0_flag_tx_buffer_level(flags)) {
		size_t len = i2c0.len;

		if (len > 0) {
			i2c0_txdata(*i2c0.data++);
			i2c0.len = len - 1;
		} else {
			i2c0_flag_tx_buffer_level_disable();
			i2c0_stop();
		}
	}
}

int
i2c0_write(const uint8_t *ptr, size_t len)
{
	int ret;

	i2c0.data = ptr;
	i2c0.len = len;
	i2c0.ret = 1;
	emu_em2_block();
	i2c0_flag_tx_buffer_level_enable();
	i2c0_start();
	while ((ret = i2c0.ret) > 0)
		__WFI();
	emu_em2_unblock();
	return ret;
}

void
i2c0_init(void)
{
	clock_peripheral_div1();
	clock_i2c0_enable();
	i2c0_clock_div(3); /* Thigh = Tlow = (4*(3 + 1) + 4) / (14MHz) = 1.43 us */
	i2c0_pins(I2C_PINS_ENABLE6); /* SDA -> PE12, SCL -> PE13 */
	i2c0_config(I2C_CONFIG_ENABLE);
	i2c0_abort(); /* we're the only master, so start transmitting immediately */
	i2c0_flags_enable(I2C_FLAG_NACK | I2C_FLAG_MSTOP);

	NVIC_SetPriority(I2C0_IRQn, 1);
	NVIC_EnableIRQ(I2C0_IRQn);
}
