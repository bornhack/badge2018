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

#ifndef _DISPLAY_H
#define _DISPLAY_H

#include <stdint.h>

struct display {
	uint8_t tx;
	uint8_t ty;
	/* reset holds the pre-amble to send the display
	 * controller before sending the databits in the
	 * frame buffer. must be placed just before
	 * the framebuffer, so we can just send all bytes
	 * starting from reset and continuing into
	 * the frame buffer. */
	uint8_t reset[14];
	uint8_t framebuf[128 * 64 / 8];
};

int display_init(struct display *dp);
int display_off(struct display *dp);
int display_on(struct display *dp);
int display_contrast(struct display *dp, uint8_t val);
int display_update(struct display *dp);
void display_clear(struct display *dp);
void display_set(struct display *dp, unsigned int x, unsigned int y);
void display_text_location(struct display *dp, uint8_t x, uint8_t y);
void display_write(struct display *dp, const uint8_t *ptr, size_t len);
void display_puts(struct display *dp, const char *str);

#endif
