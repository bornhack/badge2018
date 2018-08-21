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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "i2c0.h"
#include "display.h"

#include "font8x8.c"

static const uint8_t display_init_data[] = {
	0x78, /* I2C address, write                                   */
	0x00, /* Co = 0, D/C = 0: the rest is command bytes           */
	0xAE, /* display off                                          */
	0xD5, /* set display clock divide ratio / osc freq ..         */
	0x80, /*   Osc Freq = 8, DCLK = 0                             */
	0xA8, /* set multiplex ratio ..                               */
	0x3F, /*   64MUX                                              */
	0xD3, /* set display offset ..                                */
	0x00, /*   0                                                  */
	0x40, /* set display start line = 0                           */
	0x8D, /* enable change pump regulator ..                      */
	0x14, /*   (only mentioned in next to last page of datasheet) */
	0x20, /* set memory addressing mode ..                        */
	0x01, /*   0b01 = vertical addressing mode                    */
	0xA1, /* set segment re-map = 0                               */
	0xC8, /* set COM output scan direction = 1: from N-1 to 0     */
	0xDA, /* set COM pins hardware configuration ..               */
	0x12, /*   sequential, disable left/right remap               */
	0x81, /* set contrast control                                 */
	0x01, /*   0x01 (reset = 0x7F)                                */
	0xD9, /* set pre-charge period ..                             */
	0xF1, /*   phase 2 = 15 (reset 2), phase 1 = 1 (reset 2)      */
	0xDB, /* set Vcom deselect level ..                           */
	0x40, /*   0b100                                              */
	0x2E, /* deactivate scroll                                    */
	0xA4, /* entire display on: resume to ram content display     */
	0xA6, /* set normal/inverse display: normal                   */
	0x21, /* Select columns                                       */
	0x00, /* 0                                                    */
	0x7f, /* to 127                                               */
	0x22, /* Select pages                                         */
	0x00, /* 0                                                    */
	0x07, /* to 7                                                 */
};

int
display_init(struct display *dp)
{
	int ret;
	memset(dp, 0, sizeof(struct display));
	dp->reset[ 0] = 0x78; /* address                             */
	dp->reset[ 1] = 0x80; /* next byte is control                */
	dp->reset[ 2] = 0x21; /* Select columns                      */
	dp->reset[ 3] = 0x80; /* next byte is control                */
	dp->reset[ 4] = 0x00; /* 0;                                  */
	dp->reset[ 5] = 0x80; /* next byte is control                */
	dp->reset[ 6] = 0x7f, /* to 127                              */
	dp->reset[ 7] = 0x80; /* next byte is control                */
	dp->reset[ 8] = 0x22; /* Select pages                        */
	dp->reset[ 9] = 0x80; /* next byte is control                */
	dp->reset[10] = 0x00; /* 0                                   */
	dp->reset[11] = 0x80; /* next byte is control                */
	dp->reset[12] = 0x07, /* to 7                                */
	dp->reset[13] = 0x40; /* the remaining bytes are data bytes  */
	/*The display takes some time to initialize and will return nack until that happens*/
	while ((ret = i2c0_write(display_init_data, sizeof(display_init_data))) == -1);
	return ret;
}

int
display_off(struct display *dp)
{
	static const uint8_t data[] = {
		0x78, /* I2C address, write                                   */
		0x00, /* Co = 0, D/C = 0: the rest is command bytes           */
		0xAE, /* display off                                          */
	};
	return i2c0_write(data, sizeof(data));
};

int
display_on(struct display *dp)
{
	static const uint8_t data[] = {
		0x78, /* I2C address, write                                   */
		0x00, /* Co = 0, D/C = 0: the rest is command bytes           */
		0xAF, /* display on                                           */
	};
	return i2c0_write(data, sizeof(data));
};

int
display_contrast(struct display *dp, uint8_t val)
{
	uint8_t data[4];

	data[0] = 0x78; /* I2C address, write                         */
	data[1] = 0x00; /* Co = 0, D/C = 0: the rest is command bytes */
	data[2] = 0x81; /* set contrast                               */
	data[3] = val;

	return i2c0_write(data, sizeof(data));
};

int
display_update(struct display *dp)
{
	return i2c0_write(dp->reset, sizeof(dp->reset) + sizeof(dp->framebuf));
}

void
display_clear(struct display *dp)
{
	dp->tx = 0;
	dp->ty = 0;
	memset(dp->framebuf, 0, sizeof(dp->framebuf));
}

/* set a single pixel in the frame buffer */
void
display_set(struct display *dp, unsigned int x, unsigned int y)
{
	unsigned int idx = 8*x + y/8;
	uint8_t mask = 1 << (y & 0x7U);

	dp->framebuf[idx] |= mask;
}

void
display_text_location(struct display *dp, uint8_t x, uint8_t y)
{
	dp->tx = x;
	dp->ty = y;
}

void
display_write(struct display *dp, const uint8_t *ptr, size_t len)
{
	for (; len; len--) {
		const uint8_t *glyph;
		const uint8_t *glyph_end;
		uint8_t *dest;
		unsigned int c = *ptr++;

		if (c == '\r') {
			dp->tx = 0;
			continue;
		}
		if (c == '\n') {
			dp->tx = 0;
			goto inc_ty;
		}

		if (c < 32 || c > 0x7F)
			c = 0x7F;

		c -= 32;

		glyph = &font8x8[c][0];
		glyph_end = glyph + 8;
		dest = &dp->framebuf[dp->ty + 8*8*dp->tx];
		while (glyph < glyph_end) {
			*dest |= *glyph++;
			dest += 8;
		}

		dp->tx++;
		if (dp->tx == 16) {
			dp->tx = 0;
inc_ty:
			dp->ty++;
			if (dp->ty == 8)
				dp->ty = 0;
		}
	}
}

void
display_puts(struct display *dp, const char *str)
{
	display_write(dp, (const uint8_t *)str, strlen(str));
}
