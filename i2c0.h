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

#ifndef _I2C0_H
#define _I2C0_H

#include <stdint.h>

int i2c0_write(const uint8_t *ptr, size_t len);
void i2c0_init(void);

#endif
