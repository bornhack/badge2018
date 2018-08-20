#include <stdint.h>
#include <stdlib.h>

#include "geckonator/emu.h"
#include "geckonator/i2c0.h"

#include "geckonator/display.h"
#include "geckonator/clock.h"

typedef struct display {
	uint8_t tx;
	uint8_t ty;
	/* reset holds the pre-ample to send the display
	 * controller before sending the databits in the
	 * frame buffer. must be placed just before
	 * the framebuffer, so we can just send all bytes
	 * starting from reset and continuing into
	 * the frame buffer. */
	uint8_t reset[8];
	uint8_t framebuf[128 * 64 / 8];
} display_t;

static display_t S_display;

static const uint8_t display_init_data[] = {
	0x78, /* I2C address, write                                   */
	0x00, /* Co = 0, D/C = 0: the rest is command bytes           */
	0xAE, /* display off                                          */
	0xD5, /* set display clock divide ratio / osc freq ..         */
	0x80, /*   Osc Freq = 8, DCLK = 0                             */
	0xA8, /* set multiplex ratio ..                               */
	0x3F, /*   64MUX                                              */
	0xD3, /* set display offset ..                                */
	0x38, /*  Fix offset problem, courtesy of Klondike            */
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
	0x10, /* set higher column start address = 0                  */
	0x00, /* set lower column start address = 0                   */
	0xB0, /* set page address = 0                                 */
};


#include "font8x8.c"

static struct {
	const uint8_t *data;
	size_t len;
	volatile int ret;
} i2c0;


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


static int
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
display_init(void)
{
    struct display* dp = &S_display;
	memset(dp, 0, sizeof(struct display));
	dp->reset[0] = 0x78; /* address                             */
	dp->reset[1] = 0x80; /* next byte is control                */
	dp->reset[2] = 0x00; /* set higher column start address = 0 */
	dp->reset[3] = 0x80; /* next byte is control                */
	dp->reset[4] = 0x10; /* set lower column start address = 0; */
	dp->reset[5] = 0x80; /* next byte is control                */
	dp->reset[6] = 0xB0, /* set page address = 0                */
	dp->reset[7] = 0x40; /* the remaining bytes are data bytes  */
	return i2c0_write(display_init_data, sizeof(display_init_data));
}


int
display_on(void)
{
	static const uint8_t data[] = {
		0x78, /* I2C address, write                                   */
		0x00, /* Co = 0, D/C = 0: the rest is command bytes           */
		0xAF, /* display on                                           */
	};
	return i2c0_write(data, sizeof(data));
};


int
display_off(void)
{
	static const uint8_t data[] = {
		0x78, /* I2C address, write                                   */
		0x00, /* Co = 0, D/C = 0: the rest is command bytes           */
		0xAE, /* display off                                          */
	};
	return i2c0_write(data, sizeof(data));
};


void
display_clear(void)
{
    struct display* dp = &S_display;
	dp->tx = 0;
	dp->ty = 0;
	memset(dp->framebuf, 0, sizeof(dp->framebuf));
}


int
display_update(void)
{
    struct display* dp = &S_display;
	return i2c0_write(dp->reset, sizeof(dp->reset) + sizeof(dp->framebuf));
}


void
display_text_location(uint8_t x, uint8_t y)
{
    struct display* dp = &S_display;
	dp->tx = x;
	dp->ty = y;
}


static void
display_write(const uint8_t *ptr, size_t len)
{
    struct display* dp = &S_display;
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
display_puts(char *const str)
{
	display_write((const uint8_t *)str, strlen(str));
}
