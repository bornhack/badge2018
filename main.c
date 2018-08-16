/*
 * This file is part of Bornhack Badge 2018.
 * Copyright 2017 Emil Renner Berthing <esmil@esmil.dk>
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
#include "geckonator/leuart0.h"
#include "geckonator/usb.h"

#include "badge2018.h"
#include "swd.h"
#include "ihex.h"
#include "nRF51prog.h"

#define LEUART_DEBUG
//#define ACM_DEBUG
#ifdef NDEBUG
#undef LEUART_DEBUG
#undef ACM_DEBUG
#define debug(...)
#else
#define debug(...) printf(__VA_ARGS__)
#endif

#define USB_WORD(x) ((x) & 0xFF),((x) >> 8)
#define USB_TRIPLE(x) ((x) & 0xFF),(((x) >> 8) & 0xFF),((x) >> 16)
#define USB_QUAD(x) ((x) & 0xFF),(((x) >> 8) & 0xFF),(((x) >> 16) & 0xFF),((x) >> 24)

#define USB_FIFO_RXSIZE  256
#define USB_FIFO_TX0SIZE 128
#define USB_FIFO_TX1SIZE 128
#define USB_FIFO_TX2SIZE 64
#define USB_FIFO_TX3SIZE 0

#define DFUR_INTERFACE 0

#define DFU_INTERFACE 1
#define DFU_TRANSFERSIZE 512

#define USBMS_INTERFACE 2
#define USBMS_ENDPOINT 1
#define USBMS_PACKETSIZE 64

#define CDC_INTERFACE 3
#define CDC_PACKETSIZE 64
#define CDC_ENDPOINT 3

#define ACM_INTERFACE 4
#define ACM_ENDPOINT 2
#define ACM_PACKETSIZE 64

#define BE_UINT32(x) (((x) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x) << 24))

#define B2W(a,b,c,d) (((d)<<24)|((c)<<16)|((b)<<8)|(a))

#define FLASH_PAGE_OFFSET 0

//#define USBMS_BLOCKSIZE 512U
#define USBMS_BLOCKSIZE 1024U

#define PART_FIRST_BLOCK (1024U*1024U/USBMS_BLOCKSIZE)
#define PART_BLOCKS ((2*USBMS_BLOCKSIZE/3) - 32)

#define USBMS_BLOCKS (PART_FIRST_BLOCK+PART_BLOCKS)

//#define USBMS_REPORT_LUNS

struct usb_setup_packet {
	union {
		struct {
			uint8_t bmRequestType;
			uint8_t bRequest;
		};
		uint16_t request;
	};
	uint16_t wValue;
	uint16_t wIndex;
	uint16_t wLength;
};

struct usb_descriptor_device {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t bcdUSB;
	uint8_t bDeviceClass;
	uint8_t bDeviceSubClass;
	uint8_t bDeviceProtocol;
	uint8_t bMaxPacketSize0;
	uint16_t idVendor;
	uint16_t idProduct;
	uint16_t bcdDevice;
	uint8_t iManufacturer;
	uint8_t iProduct;
	uint8_t iSerialNumber;
	uint8_t bNumConfigurations;
};

struct usb_descriptor_configuration {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wTotalLength;
	uint8_t bNumInterfaces;
	uint8_t bConfigurationValue;
	uint8_t iConfiguration;
	uint8_t bmAttributes;
	uint8_t bMaxPower;
	uint8_t rest[];
};

struct usb_descriptor_string {
	uint8_t bLength;
	uint8_t bDescriptorType;
	uint16_t wCodepoint[];
};

struct usb_setup_handler {
	uint16_t req;
	uint8_t idx;
	uint8_t len;
	int (*fn)(const struct usb_setup_packet *p, const void **data);
};

struct command_block_wrapper {
	uint32_t dCBWSignature;
	uint32_t dCBWTag;
	uint32_t dCBWDataTransferLength;
	uint8_t bmCBWFlags;
	uint8_t bCBWLUN;
	uint8_t bCBWCBLength;
	uint8_t CBWCB[16];
};

struct command_status_wrapper {
	uint32_t dCSWSignature;
	uint32_t dCSWTag;
	uint32_t dCSWDataResidue;
	uint8_t bCSWStatus;
};

struct inquiry_data {
	uint8_t peripheral;
	uint8_t rmb;
	uint8_t version;
	uint8_t response_data_format;
	uint8_t additional_length;
	uint8_t flags[3];
	uint8_t vendor_id[8];
	uint8_t product_id[16];
	uint8_t product_revision[4];
};

struct sense_data {
	uint8_t response_code;
	uint8_t obsolete;
	uint8_t sense_key;
	uint8_t information[4];
	uint8_t additional_sense_length;
	uint8_t command_specific[4];
	uint8_t additional_sense_code;
	uint8_t additional_sense_code_qualifier;
	uint8_t field_replacable_unit_code;
	uint8_t sense_key_specific[3];
	uint8_t rest[];
};

struct mode_sense_data6 {
	uint8_t mode_data_length;
	uint8_t medium_type;
	uint8_t device_specific;
	uint8_t block_descriptor_length;
	uint8_t rest[];
};

struct format_capacity_data {
	/* capacity list header */
	uint8_t reserved[3];
	uint8_t capacity_list_length;
	/* current/maximum capacity descriptor */
	uint32_t number_of_blocks;
	uint8_t descriptor_type;
	uint8_t block_length[3];
};

struct fat12_directory_entry {
	uint8_t name[8];
	uint8_t ext[3];
	uint8_t attrs;
	uint8_t ext_attrs;
	uint8_t ctime_ms;
	uint16_t ctime;
	uint16_t cdate;
	uint16_t adate;
	uint16_t rights;
	uint16_t mtime;
	uint16_t mdate;
	uint16_t start;
	uint32_t size;
};

static const __align(4) struct usb_descriptor_device usb_descriptor_device = {
	.bLength            = 18,
	.bDescriptorType    = 0x01, /* Device */
	.bcdUSB             = 0x0200,
	.bDeviceClass       = 0x00, /* 0x00 = per interface */
	.bDeviceSubClass    = 0x00,
	.bDeviceProtocol    = 0x00,
	.bMaxPacketSize0    = 64,
	.idVendor           = 0x0483,
	.idProduct          = 0x5740,
	.bcdDevice          = 0x0200,
	.iManufacturer      = 1,
	.iProduct           = 2,
	.iSerialNumber      = 3,
	.bNumConfigurations = 1,
};

static const __align(4) struct usb_descriptor_configuration usb_descriptor_configuration1 = {
	.bLength              = 9,
	.bDescriptorType      = 0x02, /* Configuration */
#ifdef ACM_DEBUG
	.wTotalLength         = 9 + 18 + 18 + 23 + 58,
	.bNumInterfaces       = 1 + 1 + 1 + 2,
#else
	.wTotalLength         = 9 + 18 + 18 + 23,
	.bNumInterfaces       = 1 + 1 + 1,
#endif
	.bConfigurationValue  = 1,
	.iConfiguration       = 0,
	.bmAttributes         = 0x80,
	.bMaxPower            = 250,
	.rest = {
	/* Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x04, /* Interface */
	/* .bInterfaceNumber       */ DFUR_INTERFACE,
	/* .bAlternateSetting      */ 0,
	/* .bNumEndpoints          */ 0,
	/* .bInterfaceClass        */ 0xfe, /* 0xfe = Application Specific */
	/* .bInterfaceSubClass     */ 0x01, /* 0x01 = Device Firmware Upgrade */
	/* .bInterfaceProtocol     */ 0x01, /* 0x01 = Runtime Protocol */
	/* .iInterface             */ 4,
	/* DFU Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x21, /* DFU Interface */
	/* .bmAttributes           */ 0x08, /* will detach usb on DFU_DETACH */
	/* .wDetachTimeOut         */ USB_WORD(50), /* 50ms */
	/* .wTransferSize          */ USB_WORD(DFU_TRANSFERSIZE),
	/* .bcdDFUVersion          */ USB_WORD(0x0101), /* DFU v1.1 */
	/* Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x04, /* Interface */
	/* .bInterfaceNumber       */ DFU_INTERFACE,
	/* .bAlternateSetting      */ 0,
	/* .bNumEndpoints          */ 0,    /* only the control pipe is used */
	/* .bInterfaceClass        */ 0xFE, /* application specific */
	/* .bInterfaceSubClass     */ 0x01, /* device firmware upgrade */
	/* .bInterfaceProtocol     */ 0x02, /* DFU mode protocol */
	/* .iInterface             */ 5,
	/* DFU Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x21, /* DFU Interface */
	/* .bmAttributes           */ 0x05, /* can download, can't upload, manifest tolerant */
	/* .wDetachTimeOut         */ USB_WORD(500), /* 500ms */
	/* .wTransferSize          */ USB_WORD(DFU_TRANSFERSIZE),
	/* .bcdDFUVersion          */ USB_WORD(0x0101), /* DFU v1.1 */
	/* Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x04, /* Interface */
	/* .bInterfaceNumber       */ USBMS_INTERFACE,
	/* .bAlternateSetting      */ 0,
	/* .bNumEndpoints          */ 2,
	/* .bInterfaceClass        */ 0x08, /* 0x08 = Mass-Storage Class */
	/* .bInterfaceSubClass     */ 0x06, /* 0x06 = SCSI transparent command set */
	/* .bInterfaceProtocol     */ 0x50, /* 0x50 = Bulk-Only Transport (BBB) */
	/* .iInterface             */ 0,
	/* Endpoint */
	/* .bLength                */ 7,
	/* .bDescriptorType        */ 0x05, /* Endpoint */
	/* .bEndpointAddress       */ 0x80 | USBMS_ENDPOINT, /* in */
	/* .bmAttributes           */ 0x02, /* bulk, data endpoint */
	/* .wMaxPacketSize         */ USB_WORD(USBMS_PACKETSIZE),
	/* .bInterval              */ 0,    /* unused */
	/* Endpoint */
	/* .bLength                */ 7,
	/* .bDescriptorType        */ 0x05, /* Endpoint */
	/* .bEndpointAddress       */ USBMS_ENDPOINT, /* out */
	/* .bmAttributes           */ 0x02, /* bulk, data endpoint */
	/* .wMaxPacketSize         */ USB_WORD(USBMS_PACKETSIZE),
	/* .bInterval              */ 0,    /* unused */
#ifdef ACM_DEBUG
	/* Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x04, /* Interface */
	/* .bInterfaceNumber       */ CDC_INTERFACE,
	/* .bAlternateSetting      */ 0,
	/* .bNumEndpoints          */ 1,
	/* .bInterfaceClass        */ 0x02, /* 0x02 = CDC */
	/* .bInterfaceSubClass     */ 0x02, /* 0x02 = ACM */
	/* .bInterfaceProtocol     */ 0x01, /* 0x00 = no protocol required, 0x01 = AT commands V.250 etc */
	/* .iInterface             */ 0,
	/* CDC Header */
	/* .bLength                */ 5,
	/* .bDescriptorType        */ 0x24, /* CS_INTERFACE */
	/* .bDescriptorSubtype     */ 0x00, /* Header */
	/* .bcdCDC                 */ USB_WORD(0x0120),
	/* CDC Call Management */
	/* .bLength                */ 5,
	/* .bDescriptorType        */ 0x24, /* CS_INTERFACE */
	/* .bDescriptorSubtype     */ 0x01, /* Call Management */
	/* .bmCapabilities         */ 0x00, /* Does not handle call management */
	/* .bDataInterface         */ ACM_INTERFACE,
	/* CDC ACM */
	/* .bLength                */ 4,
	/* .bDescriptorType        */ 0x24, /* CS_INTERFACE */
	/* .bDescriptorSubtype     */ 0x02, /* ACM */
	/* .bmCapabilities         */ 0x00, /* 0x00 = supports nothing */
	/* CDC Union */
	/* .bLength                */ 5,
	/* .bDescriptorType        */ 0x24, /* CS_INTERFACE */
	/* .bDescriptorSubtype     */ 0x06, /* Union */
	/* .bControlInterface      */ CDC_INTERFACE,
	/* .bSubordinateInterface0 */ ACM_INTERFACE,
	/* Endpoint */
	/* .bLength                */ 7,
	/* .bDescriptorType        */ 0x05, /* Endpoint */
	/* .bEndpointAddress       */ 0x80 | CDC_ENDPOINT, /* in */
	/* .bmAttributes           */ 0x03, /* interrupt */
	/* .wMaxPacketSize         */ USB_WORD(CDC_PACKETSIZE),
	/* .bInterval              */ 255,  /* poll every 255ms */
	/* Interface */
	/* .bLength                */ 9,
	/* .bDescriptorType        */ 0x04, /* Interface */
	/* .bInterfaceNumber       */ ACM_INTERFACE,
	/* .bAlternateSetting      */ 0,
	/* .bNumEndpoints          */ 2,
	/* .bInterfaceClass        */ 0x0A, /* 0x0A = CDC Data */
	/* .bInterfaceSubClass     */ 0x00,
	/* .bInterfaceProtocol     */ 0x00,
	/* .iInterface             */ 0,
	/* Endpoint */
	/* .bLength                */ 7,
	/* .bDescriptorType        */ 0x05, /* Endpoint */
	/* .bEndpointAddress       */ 0x80 | ACM_ENDPOINT, /* in */
	/* .bmAttributes           */ 0x02, /* bulk */
	/* .wMaxPacketSize         */ USB_WORD(ACM_PACKETSIZE),
	/* .bInterval              */ 0,    /* unused */
	/* Endpoint */
	/* .bLength                */ 7,
	/* .bDescriptorType        */ 0x05, /* Endpoint */
	/* .bEndpointAddress       */ ACM_ENDPOINT, /* out */
	/* .bmAttributes           */ 0x02, /* bulk */
	/* .wMaxPacketSize         */ USB_WORD(ACM_PACKETSIZE),
	/* .bInterval              */ 0,    /* unused */
#endif
	}
};

static const __align(4) struct usb_descriptor_string usb_descriptor_string0 = {
	.bLength         = 4,
	.bDescriptorType = 0x03, /* String */
	.wCodepoint = {
		0x0409, /* English (US) */
	},
};

static const __align(4) struct usb_descriptor_string usb_descriptor_vendor = {
	.bLength         = 16,
	.bDescriptorType = 0x03, /* String */
	.wCodepoint = {
		'L','a','b','i','t','a','t',
	},
};

static const __align(4) struct usb_descriptor_string usb_descriptor_product = {
	.bLength         = 20,
	.bDescriptorType = 0x03, /* String */
	.wCodepoint = {
		'B','a','d','g','e','2','0','1','8',
	},
};

/* must be at least 12 characters long and consist of only '0'-'9','A'-'B'
 * at least according to the mass-storage bulk-only document */
static const __align(4) struct usb_descriptor_string usb_descriptor_serial = {
	.bLength         = 26,
	.bDescriptorType = 0x03, /* String */
	.wCodepoint = {
		'0','0','0','0','0','0','0','0','0','0','0','1',
	},
};

static const __align(4) struct usb_descriptor_string usb_descriptor_dfu_geckoboot = {
	.bLength         = 20,
	.bDescriptorType = 0x03, /* String */
	.wCodepoint = {
		'G','e','c','k','o','B','o','o','t',
	},
};

static const __align(4) struct usb_descriptor_string usb_descriptor_dfu_nRF51 = {
	.bLength         = 12,
	.bDescriptorType = 0x03, /* String */
	.wCodepoint = {
		'n','R','F','5','1',
	},
};

static const struct usb_descriptor_string *const usb_descriptor_string[] = {
	&usb_descriptor_string0,
	&usb_descriptor_vendor,
	&usb_descriptor_product,
	&usb_descriptor_serial,
	&usb_descriptor_dfu_geckoboot,
	&usb_descriptor_dfu_nRF51,
};

static const __align(4) struct inquiry_data inquiry_data = {
	.peripheral           = 0x00, /* peripheral device connected, SBC-2 (magnetic disk) */
	.rmb                  = 0x80, /* removable medium bit set */
	/* 0x05 = SPC-3, but FreeBSD then needs REPORT LUNS (0xa0) and SERVICE ACTION IN (0x9e) */
	.version              = 0x04,
	.response_data_format = 0x02, /* 0x02 = SPC-3 */
	.additional_length    = 31,
	.flags                = { 0x00, 0x00, 0x00 },
	.vendor_id            = { 'L','a','b','i','t','a','t',' ' },
	.product_id           = { 'n','R','F','5','1',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' },
	.product_revision     = { '0','0','0','1' },
};

static const __align(4) struct sense_data sense_data = {
	.response_code                   = 0x70, /* current errors, fixed format */
	.obsolete                        = 0x00,
	.sense_key                       = 0x05, /* illegal request */
	.information                     = { 0x00, 0x00, 0x00, 0x00 },
	.additional_sense_length         = 10,
	.command_specific                = { 0x00, 0x00, 0x00, 0x00 },
	.additional_sense_code           = 0x24,
	.additional_sense_code_qualifier = 0x00, /* invalid field in CDB */
	.field_replacable_unit_code      = 0x00,
	.sense_key_specific              = { 0x00, 0x00, 0x00 },
	/* .rest = { } */
};

static const __align(4) struct mode_sense_data6 mode_sense_data6 = {
	.mode_data_length        = 35,
	.medium_type             = 0x00,
	.device_specific         = 0x00,
	.block_descriptor_length = 0x00,
	.rest = {
	/* Caching Page */
	/* .page_code             = */ 0x08,
	/* .page_length           = */ 18,
	/* .flags1                = */ 0x00,
	/* .retention_priority    = */ 0x00,
	/* .disable_prefetch      = */ 0x00, 0x00,
	/* .minimum_prefetch      = */ 0x00, 0x00,
	/* .maximum_prefetch      = */ 0x00, 0x00,
	/* .maximum_ceiling       = */ 0x00, 0x00,
	/* .flags2                = */ 0x00,
	/* .cache_segments        = */ 0x00,
	/* .cache_segment_size    = */ 0x00, 0x00,
	/* .reserved              = */ 0x00,
	/* .noncache_segment_size = */ 0x00, 0x00, 0x00,
	/* Information Exceptions Control Mode Page */
	/* .page_code             = */ 0x1C,
	/* .page_length           = */ 10,
	/* .flags1                = */ 0x00,
	/* .mrie                  = */ 0x00,
	/* .interval_timer        = */ 0x00, 0x00, 0x00, 0x00,
	/* .report_count          = */ 0x00, 0x00, 0x00, 0x00,
	},
};

static const __align(4) struct format_capacity_data format_capacity_data = {
	.reserved             = { 0x00, 0x00, 0x00 },
	.capacity_list_length = 8,
	.number_of_blocks     = BE_UINT32(USBMS_BLOCKS),
	.descriptor_type      = 0x02, /* formatted media */
	.block_length         = {
		(USBMS_BLOCKSIZE >> 16) & 0xFFU,
		(USBMS_BLOCKSIZE >>  8) & 0xFFU,
		(USBMS_BLOCKSIZE >>  0) & 0xFFU,
	}
};

static struct {
	uint32_t bytes;
	uint32_t packetsize;
} usb_state;

static __uninitialized uint32_t usb_outbuf[
	(4*sizeof(struct usb_setup_packet) + DFU_TRANSFERSIZE) / sizeof(uint32_t)
];
static __uninitialized union {
	int8_t    i8[4];
	uint8_t   u8[4];
	int16_t  i16[2];
	uint16_t u16[2];
	int32_t  i32[1];
	uint32_t u32[1];
} usb_inbuf;

enum usbms_state {
	USBMS_STATE_CBW,
	USBMS_STATE_DATA_OUT,
	USBMS_STATE_DATA_IN,
	USBMS_STATE_CSW,
	USBMS_STATE_ERROR,
};

static struct {
	enum usbms_state state;
	union {
		const void *data;
		void *buf;
	};
	uint32_t bytes;
	uint32_t chunk;
	uint32_t lba;
	unsigned int page;
} usbms;

static __uninitialized union {
	struct command_block_wrapper cbw;
	struct command_status_wrapper csw;
	uint8_t byte[USBMS_PACKETSIZE];
	uint32_t word[USBMS_PACKETSIZE/sizeof(uint32_t)];
} usbms_buf;

#ifdef ACM_DEBUG
static __uninitialized union {
	uint32_t word[ACM_PACKETSIZE/sizeof(uint32_t)];
	uint8_t byte[ACM_PACKETSIZE];
} acm_outbuf;

static struct {
	volatile uint32_t first;
	volatile uint32_t last;
	uint8_t buf[2048];
} acm_inbuf;

static volatile bool acm_idle;

static void
acm_shipit(void)
{
	uint32_t first = acm_inbuf.first;
	uint32_t last = acm_inbuf.last;
	uint32_t end;

	if (first == last) {
		acm_inbuf.first = acm_inbuf.last = 0;
		acm_idle = true;
		return;
	}

	end = first + ACM_PACKETSIZE;
	if (last > first && end > last) {
		end = last;
		if (last & 3) {
			last = (last & ~3) + 4;
			last %= ARRAY_SIZE(acm_inbuf.buf);
		}
		acm_inbuf.first = acm_inbuf.last = last;
	} else if (end >= ARRAY_SIZE(acm_inbuf.buf)) {
		end = ARRAY_SIZE(acm_inbuf.buf);
		acm_inbuf.first = 0;
	} else
		acm_inbuf.first = end;

	usb_ep_in_dma_address_set(ACM_ENDPOINT, &acm_inbuf.buf[first]);
	usb_ep_in_transfer_size(ACM_ENDPOINT, 1, end - first);
	usb_ep_in_enable(ACM_ENDPOINT);

	acm_idle = false;
}

static void
acm_write(const uint8_t *ptr, size_t len)
{
	uint32_t last = acm_inbuf.last;

	for (; len > 0; len--) {
		acm_inbuf.buf[last++] = *ptr++;
		last %= ARRAY_SIZE(acm_inbuf.buf);
	}
	acm_inbuf.last = last;

	if (acm_idle)
		acm_shipit();
}

static void
acm_writeln(const uint8_t *ptr, size_t len)
{
	static const uint8_t hex[] = {
		'0', '1', '2', '3', '4', '5', '6', '7',
		'8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
	};
	uint32_t last = acm_inbuf.last;

	for (; len > 0; len--) {
		uint8_t c = *ptr++;

		if (c > 32 && c < 127) {
			acm_inbuf.buf[last++] = c;
			last %= ARRAY_SIZE(acm_inbuf.buf);
		} else if (c == '\r') {
			acm_inbuf.buf[last++] = '\r';
			last %= ARRAY_SIZE(acm_inbuf.buf);
			acm_inbuf.buf[last++] = '\n';
			last %= ARRAY_SIZE(acm_inbuf.buf);
		} else if (c == 127) {
			acm_inbuf.buf[last++] = '\b';
			last %= ARRAY_SIZE(acm_inbuf.buf);
		} else {
			acm_inbuf.buf[last++] = '\\';
			last %= ARRAY_SIZE(acm_inbuf.buf);
			acm_inbuf.buf[last++] = 'x';
			last %= ARRAY_SIZE(acm_inbuf.buf);
			acm_inbuf.buf[last++] = hex[c >> 4];
			last %= ARRAY_SIZE(acm_inbuf.buf);
			acm_inbuf.buf[last++] = hex[c & 0xF];
			last %= ARRAY_SIZE(acm_inbuf.buf);
		}
	}
	acm_inbuf.last = last;

	if (acm_idle)
		acm_shipit();
}
#endif

static struct ihex_parser ihex_parser;
static uint32_t prog_next_lba;
static bool prog_enabled;
static struct {
	uint32_t addr;
	union {
		uint32_t word;
		uint8_t byte[4];
	};
	bool dirty;
} progbuf;

static int
progbuf_flush(void)
{
	int ret = nRF51_write(progbuf.addr, progbuf.word);

	if (ret != SWD_OK) {
		debug("(%u) nRF51_write\r\n", ret);
		return -1;
	}
	progbuf.dirty = false;
	return 0;
}

static int
prog_begin(void)
{
	int ret;
	uint32_t v;

	if (prog_enabled)
		return 0;

	swd_engage();
	ret = swd_dp_init(&v);
	debug("(%u) swd_dp_init = 0x%08lx\r\n", ret, v);
	if (ret != SWD_OK)
		goto err;
	ret = swd_armv6m_reset_debug();
	debug("(%u) swd_armv6m_reset_debug\r\n", ret);
	if (ret != SWD_OK)
		goto err;
	ret = nRF51_erase();
	debug("(%u) nRF51_erase\r\n", ret);
	if (ret != SWD_OK)
		goto err;

	prog_enabled = true;
	return 0;
err:
	swd_disengage();
	return -1;
}

static int
prog_done(void)
{
	int ret;
	int iret;

	if (!prog_enabled)
		return 0;

	iret = 0;
	if (progbuf.dirty && progbuf_flush())
		iret = -1;

	ret = swd_armv6m_reset_run();
	debug("(%u) swd_armv6m_reset_run\r\n", ret);
	if (ret != SWD_OK)
		iret = -1;
	ret = nRF51_reboot_run();
	debug("(%u) nRF51_reboot_run\r\n", ret);
	if (ret != SWD_OK)
		iret = -1;
	swd_disengage();

	prog_enabled = false;
	return iret;
}

static int
ihex_record_program(void)
{
	uint32_t addr = ihex_address(&ihex_parser);
	uint8_t *p = ihex_parser.data;
	uint8_t *end = p + ihex_parser.length;
	unsigned int i;

#if 0
	debug("0x%08lx: %hu bytes\r\n", addr,
			ihex_parser.length);
#endif

	if (progbuf.dirty && progbuf.addr != (addr & ~0x3UL) && progbuf_flush())
		return -1;

	progbuf.addr = addr & ~0x3UL;
	i = addr - progbuf.addr;
	while (p < end) {
		progbuf.byte[i++] = *p++;
		progbuf.dirty = true;
		if (i == 4) {
			if (progbuf_flush())
				return -1;

			progbuf.addr += 4;
			i = 0;
		}
	}

	return 0;
}

static void
dumpsetup(const struct usb_setup_packet *p)
{
	debug("{\r\n"
	      "  bmRequestType 0x%02x\r\n"
	      "  bRequest      0x%02x\r\n"
	      "  wValue        0x%04x\r\n"
	      "  wIndex        0x%04x\r\n"
	      "  wLength       0x%04x\r\n"
	      "}\r\n",
		p->bmRequestType,
		p->bRequest,
		p->wValue,
		p->wIndex,
		p->wLength);
}

static void
dumpcbw(const struct command_block_wrapper *cbw)
{
	debug("CBW {\r\n"
	      "  dCBWSignature          0x%08lx\r\n"
	      "  dCBWTag                %lu\r\n"
	      "  dCBWDataTransferLength %lu\r\n"
	      "  bmCBWFlags             0x%02hx\r\n"
	      "  bCBWLUN                %hu\r\n"
	      "  bCBWCBLength           %hu\r\n"
	      "  CBWCB { opcode = 0x%02hx }\r\n"
	      "}\r\n",
			cbw->dCBWSignature,
			cbw->dCBWTag,
			cbw->dCBWDataTransferLength,
			cbw->bmCBWFlags,
			cbw->bCBWLUN,
			cbw->bCBWCBLength,
			cbw->CBWCB[0]);
}

static void
usb_ep0out_prepare_setup(void *buf)
{
	usb_ep0out_dma_address_set(buf);
	usb_ep0out_transfer_size(0, 0);
	usb_ep0out_enable_setup();
}

static void
usb_suspend(void)
{
	usb_phy_stop();
}

static void
usb_unsuspend(void)
{
	usb_phy_start();
}

static void
usb_ep_reset(void)
{
	unsigned int i;

	usb_ep0out_config_64byte_stall();
	usb_ep0out_flags_clear(~0UL);
	for (i = 1; i < 4; i++) {
		usb_ep_out_config_disabled_nak(i);
		usb_ep_out_flags_clear(i, ~0UL);
	}

	usb_ep0in_config_64byte_stall();
	usb_ep0in_flags_clear(~0UL);
	for (i = 1; i < 4; i++) {
		usb_ep_in_config_disabled_nak(i);
		usb_ep_in_flags_clear(i, ~0UL);
	}
}

static void
usb_reset(void)
{
	/* flush fifos */
	usb_fifo_flush();
	while (usb_fifo_flushing())
		/* wait */;

	/* reset endpoint registers */
	usb_ep_reset();

	/* reset address */
	usb_set_address(0);

	/* enable interrupts for endpoint 0 only */
	usb_ep_flags_enable(USB_DAINTMSK_INEPMSK0 | USB_DAINTMSK_OUTEPMSK0);
	usb_ep_out_flags_enable(USB_DOEPMSK_SETUPMSK
			| USB_DOEPMSK_EPDISBLDMSK
			| USB_DOEPMSK_XFERCOMPLMSK);
	usb_ep_in_flags_enable(USB_DIEPMSK_TIMEOUTMSK
			| USB_DIEPMSK_EPDISBLDMSK
			| USB_DIEPMSK_XFERCOMPLMSK);

	/* update internal state */
	usb_state.bytes = 0;
}

static void
usb_enumdone(void)
{
	if (usb_speed_full()) {
		/* we already set 64 byte packages at reset */
		usb_state.packetsize = 64;
		debug("full speed.. ");
	} else {
		/* use 8 byte packages */
		usb_ep0out_config_8byte();
		usb_ep0in_config_8byte();
		usb_state.packetsize = 8;
		debug("low speed.. ");
	}

	/* prepare to receive setup package */
	usb_ep0out_prepare_setup(&usb_outbuf);
}

static int
usb_handle_get_status_device(const struct usb_setup_packet *p, const void **data)
{
	debug("GET_STATUS device\r\n");
	usb_inbuf.u16[0] = 0;
	*data = &usb_inbuf;
	return 2;
}

static int
usb_handle_set_address(const struct usb_setup_packet *p, const void **data)
{
	debug("SET_ADDRESS: wValue = %hu\r\n", p->wValue);
	usb_set_address(p->wValue);
	return 0;
}

static int
usb_handle_get_descriptor_device(const void **data, uint8_t index)
{
	debug("GET_DESCRIPTOR: device\r\n");
	if (index != 0) {
		debug("GET_DESCRIPTOR: type = 0x01, but index = 0x%02x\r\n", index);
		return -1;
	}
	*data = &usb_descriptor_device;
	return sizeof(usb_descriptor_device);
}

static int
usb_handle_get_descriptor_configuration(const void **data, uint8_t index)
{
	debug("GET_DESCRIPTOR: configuration %hu\r\n", index);
	if (index != 0)
		return -1;
	*data = &usb_descriptor_configuration1;
	return usb_descriptor_configuration1.wTotalLength;
}

static int
usb_handle_get_descriptor_string(const void **data, uint8_t index)
{
	const struct usb_descriptor_string *desc;

	debug("GET_DESCRIPTOR: string %hu\r\n", index);
	if (index >= ARRAY_SIZE(usb_descriptor_string)) {
		debug("GET_DESCRIPTOR: unknown string %hu\r\n", index);
		return -1;
	}
	desc = usb_descriptor_string[index];
	*data = desc;
	return desc->bLength;
}

static int
usb_handle_get_descriptor(const struct usb_setup_packet *p, const void **data)
{
	uint8_t type = p->wValue >> 8;
	uint8_t index = p->wValue & 0xFFU;

	switch (type) {
	case 0x01:
		return usb_handle_get_descriptor_device(data, index);
	case 0x02:
		return usb_handle_get_descriptor_configuration(data, index);
	case 0x03:
		return usb_handle_get_descriptor_string(data, index);
	case 0x06: /* DEVICE QUALIFIER (for high-speed) */
		debug("DEVICE_QUALIFIER\r\n");
		break;
	default:
		debug("GET_DESCRIPTOR: unknown type 0x%02x\r\n", type);
		dumpsetup(p);
		break;
	}
	return -1;
}

static int
usb_handle_get_configuration(const struct usb_setup_packet *p, const void **data)
{
	debug("GET_CONFIGURATION\r\n");
	usb_inbuf.u8[0] = usb_descriptor_configuration1.bConfigurationValue;
	*data = &usb_inbuf;
	return 1;
}

static int
usb_handle_set_configuration(const struct usb_setup_packet *p, const void **data)
{
	debug("SET_CONFIGURATION: wIndex = %hu, wValue = %hu\r\n",
			p->wIndex, p->wValue);

	if (p->wIndex == 0 && p->wValue == usb_descriptor_configuration1.bConfigurationValue) {
		/* configure mass-storage bulk endpoints */
		usb_ep_in_config_bulk(USBMS_ENDPOINT, USBMS_PACKETSIZE);
		usb_ep_out_dma_address_set(USBMS_ENDPOINT, &usbms_buf);
		usb_ep_out_transfer_size(USBMS_ENDPOINT, 1, USBMS_PACKETSIZE);
		usb_ep_out_config_bulk_enabled(USBMS_ENDPOINT, USBMS_PACKETSIZE);
		usb_ep_flag_inout_enable(USBMS_ENDPOINT);
#ifdef ACM_DEBUG
		/* configure CDC endpoint */
		usb_ep_in_config_interrupt(CDC_ENDPOINT, CDC_PACKETSIZE);
		usb_ep_flag_in_enable(CDC_ENDPOINT);

		/* configure ACM endpoints */
		usb_ep_in_config_bulk(ACM_ENDPOINT, ACM_PACKETSIZE);

		usb_ep_out_dma_address_set(ACM_ENDPOINT, &acm_outbuf);
		usb_ep_out_transfer_size(ACM_ENDPOINT, 1, ACM_PACKETSIZE);
		usb_ep_out_config_bulk_enabled(ACM_ENDPOINT, ACM_PACKETSIZE);
		usb_ep_flag_inout_enable(ACM_ENDPOINT);
#endif
		return 0;
	}

	return -1;
}

static int
usb_handle_set_interface0(const struct usb_setup_packet *p, const void **data)
{
	debug("SET_INTERFACE: wIndex = %hu, wValue = %hu\r\n", p->wIndex, p->wValue);

	if (p->wValue == 0)
		return 0;

	return -1;
}

static int
usb_handle_clear_feature_endpoint(const struct usb_setup_packet *p, const void **data)
{
	debug("CLEAR_FEATURE endpoint %hu\r\n", p->wIndex);
	return 0;
}

enum dfu_status {
	DFU_OK,
	DFU_errTARGET,
	DFU_errFILE,
	DFU_errWRITE,
	DFU_errERASE,
	DFU_errCHECK_ERASED,
	DFU_errPROG,
	DFU_errVERIFY,
	DFU_errADDRESS,
	DFU_errNOTDONE,
	DFU_errFIRMWARE,
	DFU_errVENDOR,
	DFU_errUSBR,
	DFU_errPOR,
	DFU_errUNKNOWN,
	DFU_errSTALLEDPKT,
};

enum dfu_state {
	DFU_appIDLE,
	DFU_appDETACH,
	DFU_dfuIDLE,
	DFU_dfuDNLOAD_SYNC,
	DFU_dfuDNBUSY,
	DFU_dfuDNLOAD_IDLE,
	DFU_dfuMANIFEST_SYNC,
	DFU_dfuMANIFEST,
	DFU_dfuMANIFEST_WAIT_RESET,
	DFU_dfuUPLOAD_IDLE,
	DFU_dfuERROR,
};

static __align(4) struct {
	uint8_t bStatus;
	uint8_t bwPollTimeout[3];
	volatile uint8_t bState;
	uint8_t iString;
} dfu_status;

static bool dfu_reset;

static uint16_t indata_len;
static uint8_t indata[DFU_TRANSFERSIZE];

static int
dfu_detach(const struct usb_setup_packet *p, const void **data)
{
	debug("DFU_DETACH: wValue = %hu\r\n", p->wValue);
	dfu_reset = true;
	return 0;
}

static int
dfu_dnload(const struct usb_setup_packet *p, const void **data)
{
	debug("DFU_DNLOAD: wValue = %hu, wLength = %hu\r\n",
			p->wValue, p->wLength);

	switch (dfu_status.bState) {
	case DFU_dfuIDLE:
	case DFU_dfuDNLOAD_IDLE:
		break;
	default:
		return -1;
	}

	if (p->wLength == 0) {
		dfu_status.bState = DFU_dfuMANIFEST_SYNC;
		return 0;
	}

	memcpy(indata, *data, p->wLength);
	indata_len = p->wLength;

	dfu_status.bState = DFU_dfuDNBUSY;
	return 0;
}

static int
dfu_getstatus(const struct usb_setup_packet *p, const void **data)
{
#if 0
	debug("DFU_GETSTATUS: wValue = %hu, wLength = %hu\r\n",
			p->wValue, p->wLength);
#endif

	switch (dfu_status.bState) {
	case DFU_dfuDNLOAD_SYNC:
		dfu_status.bState = DFU_dfuDNLOAD_IDLE;
		break;
	case DFU_dfuMANIFEST_SYNC:
		dfu_status.bState = DFU_dfuIDLE;
		break;
	}

	*data = &dfu_status;
	return sizeof(dfu_status);
}

static int
dfu_clrstatus(const struct usb_setup_packet *p, const void **data)
{
	debug("DFU_CLRSTATUS\r\n");

	if (dfu_status.bState != DFU_dfuERROR)
		return -1;

	dfu_status.bStatus = DFU_OK;
	dfu_status.bState = DFU_dfuIDLE;
	return 0;
}

static int
dfu_getstate(const struct usb_setup_packet *p, const void **data)
{
	debug("DFU_GETSTATE\r\n");

	usb_inbuf.u8[0] = dfu_status.bState;
	*data = &usb_inbuf;
	return 1;
}

static int
dfu_abort(const struct usb_setup_packet *p, const void **data)
{
	debug("DFU_ABORT\r\n");

	switch (dfu_status.bState) {
	case DFU_dfuIDLE:
	case DFU_dfuDNLOAD_IDLE:
	case DFU_dfuUPLOAD_IDLE:
		dfu_status.bStatus = DFU_OK;
		dfu_status.bState = DFU_dfuIDLE;
		return 0;
	}

	return -1;
}

static int
usbms_reset(const struct usb_setup_packet *p, const void **data)
{
	debug("MASS_STORAGE_RESET\r\n");
	return 0;
}

static int
usbms_get_max_lun(const struct usb_setup_packet *p, const void **data)
{
	debug("GET_MAX_LUN\r\n");
	usb_inbuf.u8[0] = 0;
	*data = &usb_inbuf;
	return 1;
}

#ifdef ACM_DEBUG
static int
acm_set_control_line_state(const struct usb_setup_packet *p, const void **data)
{
	debug("SET_CONTROL_LINE_STATE: wIndex = %hu wValue = 0x%04hx\r\n",
			p->wIndex, p->wValue);

	debug("  RTS %u DTR %u\r\n", !!(p->wValue & 0x2), !!(p->wValue & 0x1));
	acm_shipit();
	return 0;
}

static int
acm_set_line_coding(const struct usb_setup_packet *p, const void **data)
{
	const struct {
		uint32_t dwDTERate;
		uint8_t bCharFormat; /* 0 = 1 stop bit, 1 = 1.5, 2 = 2 */
		uint8_t bParityType; /* 0=none, 1=odd, 2=even, 3=mark, 4=space */
		uint8_t bDataBits;
	} *lc = *data;

	debug("SET_LINE_CODING: {\r\n"
	      "  dwDTERate   = %lu\r\n"
	      "  bCharFormat = 0x%02x\r\n"
	      "  bParityType = 0x%02x\r\n"
	      "  bDataBits   = 0x%02x\r\n"
	      "}\r\n",
			lc->dwDTERate,
			lc->bCharFormat,
			lc->bParityType,
			lc->bDataBits);
	return 0;
}
#endif

static const struct usb_setup_handler usb_setup_handlers[] = {
	{ .req = 0x0080, .idx =  0, .len = -1, .fn = usb_handle_get_status_device },
	{ .req = 0x0500, .idx =  0, .len =  0, .fn = usb_handle_set_address },
	{ .req = 0x0680, .idx = -1, .len = -1, .fn = usb_handle_get_descriptor },
	{ .req = 0x0880, .idx =  0, .len = -1, .fn = usb_handle_get_configuration },
	{ .req = 0x0900, .idx =  0, .len =  0, .fn = usb_handle_set_configuration },
	{ .req = 0x0102, .idx =  0, .len =  0, .fn = usb_handle_clear_feature_endpoint },
	{ .req = 0x0b01, .idx = DFUR_INTERFACE, .len = 0, .fn = usb_handle_set_interface0 },
	{ .req = 0x0021, .idx = DFUR_INTERFACE, .len = 0, .fn = dfu_detach },
	{ .req = 0x0b01, .idx = DFU_INTERFACE,  .len =  0, .fn = usb_handle_set_interface0 },
	{ .req = 0x0121, .idx = DFU_INTERFACE,  .len = -1, .fn = dfu_dnload },
	{ .req = 0x03a1, .idx = DFU_INTERFACE,  .len = -1, .fn = dfu_getstatus },
	{ .req = 0x0421, .idx = DFU_INTERFACE,  .len =  0, .fn = dfu_clrstatus },
	{ .req = 0x05a1, .idx = DFU_INTERFACE,  .len = -1, .fn = dfu_getstate },
	{ .req = 0x0621, .idx = DFU_INTERFACE,  .len =  0, .fn = dfu_abort },
	{ .req = 0xff21, .idx = USBMS_INTERFACE, .len =  0, .fn = usbms_reset },
	{ .req = 0xfea1, .idx = USBMS_INTERFACE, .len = -1, .fn = usbms_get_max_lun, },
#ifdef ACM_DEBUG
	{ .req = 0x0b01, .idx = CDC_INTERFACE, .len = 0, .fn = usb_handle_set_interface0 },
	{ .req = 0x2221, .idx = CDC_INTERFACE, .len = 0, .fn = acm_set_control_line_state },
	{ .req = 0x2021, .idx = CDC_INTERFACE, .len = 7, .fn = acm_set_line_coding },
	{ .req = 0x0102, .idx = CDC_ENDPOINT,  .len = 0, .fn = usb_handle_clear_feature_endpoint },
	{ .req = 0x0b01, .idx = ACM_INTERFACE, .len = 0, .fn = usb_handle_set_interface0 },
	{ .req = 0x0102, .idx = ACM_ENDPOINT,  .len = 0, .fn = usb_handle_clear_feature_endpoint },
#endif
};

static int
usb_setup_handler_run(const struct usb_setup_packet *p, const void **data)
{
	const struct usb_setup_handler *h = usb_setup_handlers;
	const struct usb_setup_handler *end = h + ARRAY_SIZE(usb_setup_handlers);
	uint8_t idx = p->wIndex;

	for (; h < end; h++) {
		if (h->req == p->request && (h->idx == 0xFFU || h->idx == idx)) {
			if (h->len != 0xFFU && h->len != p->wLength)
				break;
			return h->fn(p, data);
		}
	}

	debug("unknown request 0x%04x\r\n", p->request);
	dumpsetup(p);
	return -1;
}

static void
usb_handle_setup(void)
{
	const struct usb_setup_packet *p =
		usb_ep0out_dma_address() - sizeof(struct usb_setup_packet);

	usb_state.bytes = 0;

	if (p->bmRequestType & 0x80U) {
		const void *data;
		int ret = usb_setup_handler_run(p, &data);

		if (ret >= 0) {
			/* send IN data */
			if (ret > p->wLength)
				ret = p->wLength;
			usb_state.bytes = ret;
			if ((uint32_t)ret > usb_state.packetsize)
				ret = usb_state.packetsize;
			usb_ep0in_dma_address_set(data);
			usb_ep0in_transfer_size(1, ret);
			usb_ep0in_enable();
			/* prepare for IN ack */
			usb_ep0out_dma_address_set(&usb_outbuf);
			usb_ep0out_transfer_size(1, usb_state.packetsize);
			usb_ep0out_enable();
			return;
		}
	} else if (p->wLength == 0) {
		if (!usb_setup_handler_run(p, NULL)) {
			/* send empty ack package */
			usb_ep0in_transfer_size(1, 0);
			usb_ep0in_enable();
			/* prepare for next SETUP package */
			usb_ep0out_prepare_setup(&usb_outbuf);
			return;
		}
	} else if (p->wLength <= sizeof(usb_outbuf) - 4*sizeof(struct usb_setup_packet)) {
		uint32_t *rp = (uint32_t *)p;

		if (rp != &usb_outbuf[0]) {
			usb_outbuf[0] = rp[0];
			usb_outbuf[1] = rp[1];
		}

		/* receive OUT data */
		usb_ep0out_dma_address_set(&usb_outbuf[2]);
		usb_ep0out_transfer_size(1, usb_state.packetsize);
		usb_ep0out_enable();
		usb_state.bytes = p->wLength;
		return;
	}

	/* stall IN endpoint */
	usb_ep0in_stall();
	/* prepare for next SETUP package */
	usb_ep0out_prepare_setup(&usb_outbuf);
}

static void
usb_handle_ep0(void)
{
	uint32_t oflags = usb_ep0out_flags();
	uint32_t iflags = usb_ep0in_flags();
	uint32_t bytes;

	usb_ep0out_flags_clear(oflags);
	usb_ep0in_flags_clear(iflags);

	//debug("EP0 %04lx %04lx %lu\r\n", oflags, iflags, usb_state.bytes);

	if (usb_ep0out_flag_setup(oflags)) {
		usb_handle_setup();
		return;
	}

	bytes = usb_state.bytes;
	if (bytes == 0) {
		if (dfu_reset) {
			if (usb_ep0in_flag_complete(iflags)) {
				NVIC_SystemReset();
				__builtin_unreachable();
			} else
				dfu_reset = false;
		}
		return;
	}

	if (usb_ep0in_flag_complete(iflags)) {
		/* data IN */
		if (bytes > usb_state.packetsize) {
			/* send next package */
			bytes -= usb_state.packetsize;
			usb_state.bytes = bytes;
			if (bytes > usb_state.packetsize)
				bytes = usb_state.packetsize;
			usb_ep0in_transfer_size(1, bytes);
			usb_ep0in_enable();
		} else
			usb_state.bytes = 0;
	} else if (usb_ep0out_flag_complete(oflags)) {
		/* data OUT */
		bytes = usb_state.packetsize - usb_ep0out_bytes_left();
		usb_state.bytes -= bytes;
		if (usb_state.bytes > 0) {
			/* prepare for more OUT data */
			usb_ep0out_transfer_size(1, usb_state.packetsize);
			usb_ep0out_enable();
		} else {
			const void *data = &usb_outbuf[2];

			if (!usb_setup_handler_run((const void *)&usb_outbuf, &data)) {
				/* send empty ack package */
				usb_ep0in_transfer_size(1, 0);
				usb_ep0in_enable();
			} else
				usb_ep0in_stall();
			usb_ep0out_prepare_setup(&usb_outbuf);
		}
	}
}

static __uninitialized union {
	uint32_t word[USBMS_BLOCKSIZE/sizeof(uint32_t)];
	uint8_t byte[USBMS_BLOCKSIZE];
} pagebuf;
static union {
	uint32_t word[USBMS_BLOCKSIZE/sizeof(uint32_t)];
	uint8_t byte[USBMS_BLOCKSIZE];
} fat12;
static union {
	uint32_t word[USBMS_BLOCKSIZE/sizeof(uint32_t)];
	struct fat12_directory_entry entry[USBMS_BLOCKSIZE/32];
} rootentries;

static unsigned int fat12_codebin_entry;

static unsigned int
fat12_get(unsigned int cn)
{
	unsigned int idx = (3*cn) >> 1;
	unsigned int a = fat12.byte[idx];
	unsigned int b = fat12.byte[idx+1];

	if (cn & 1)
		return (a >> 4) | (b << 4);

	return  a | ((b & 0x0F) << 8);
}

#if 0
static void
fat12_put(unsigned int cn, unsigned int next)
{
	unsigned int idx = (3*cn) >> 1;

	if (cn & 1) {
		fat12.byte[idx] = (fat12.byte[idx] & 0x0F) | ((next & 0x0F) << 4);
		fat12.byte[idx+1] = next >> 4;
	} else {
		fat12.byte[idx++] = next & 0xFF;
		fat12.byte[idx] = (fat12.byte[idx] & 0xF0) | (next >> 8);
	}
}
#endif

static int
fat12_getpage(unsigned int first, unsigned int cn)
{
	int page = FLASH_PAGE_OFFSET;
	unsigned int i;

	for (i = first; i < PART_BLOCKS; i = fat12_get(i)) {
		if (i == cn)
			return page;
		page++;
	}

	return -1;
}

static int
usbms_handle_test_unit_ready(const struct command_block_wrapper *cbw)
{
	debug("SCSI: test unit ready\r\n");
	return 0;
}

static int
usbms_handle_request_sense(const struct command_block_wrapper *cbw)
{
	debug("SCSI: request sense\r\n");
	usbms.data = &sense_data;
	usbms.bytes = usbms.chunk = sense_data.additional_sense_length + 8;
	return 0;
}

static int
usbms_handle_inquiry(const struct command_block_wrapper *cbw)
{
	debug("SCSI: inquiry\r\n");
	usbms.data = &inquiry_data;
	usbms.bytes = usbms.chunk = inquiry_data.additional_length + 5;
	return 0;
}

static int
usbms_handle_mode_sense6(const struct command_block_wrapper *cbw)
{
	debug("SCSI: mode sense(6) len=%lu\r\n", cbw->dCBWDataTransferLength);
	usbms.data = &mode_sense_data6;
	usbms.bytes = usbms.chunk = mode_sense_data6.mode_data_length + 1;
	return 0;
}

static int
usbms_handle_start_stop_unit(const struct command_block_wrapper *cbw)
{
	debug("SCSI: start stop unit CBWCB[4]=0x%02x CBWCB[5]=0x%02x\r\n",
			cbw->CBWCB[4], cbw->CBWCB[5]);
	if (cbw->CBWCB[4] == 0x02)
		return 0;
	return 1;
}

static int
usbms_handle_prevent_allow_medium_removal(const struct command_block_wrapper *cbw)
{
	debug("SCSI: prevent allow medium removal\r\n");
	return 1;
}

static int
usbms_handle_read_format_capacity(const struct command_block_wrapper *cbw)
{
	debug("SCSI: read format capacity\r\n");
	usbms.data = &format_capacity_data;
	usbms.bytes = usbms.chunk = format_capacity_data.capacity_list_length + 4;
	return 0;
}

static int
usbms_handle_read_capacity(const struct command_block_wrapper *cbw)
{
	static const struct {
		uint32_t lba;
		uint32_t blocksize;
	} capacity = {
		.lba = BE_UINT32(USBMS_BLOCKS - 1U),
		.blocksize = BE_UINT32(USBMS_BLOCKSIZE),
	};

	debug("SCSI: read capacity\r\n");
	usbms.data = &capacity;
	usbms.bytes = usbms.chunk = sizeof(capacity);
	return 0;
}

static void
usbms_read(void)
{
	unsigned int i;

	if (usbms.lba == PART_FIRST_BLOCK+1) {
		usbms.data = &fat12;
		usbms.chunk = USBMS_BLOCKSIZE;
		return;
	}
	if (usbms.lba == PART_FIRST_BLOCK+2) {
		usbms.data = &rootentries;
		usbms.chunk = USBMS_BLOCKSIZE;
		return;
	}

	for (i = 0; i < ARRAY_SIZE(pagebuf.word); i++)
		pagebuf.word[i] = 0;

	switch (usbms.lba) {
	case 0:
		pagebuf.word[440/4] = 0xCAFEBABE;
		pagebuf.word[444/4] = B2W(0x00, 0x00, 0x80, 0xFE);
		pagebuf.word[448/4] = B2W(0xFF, 0xFF, 0x01, 0xFE);
		pagebuf.word[452/4] = B2W(0xFF, 0xFF, PART_FIRST_BLOCK & 0xFF, (PART_FIRST_BLOCK>>8) & 0xFF);
		pagebuf.word[456/4] = B2W((PART_FIRST_BLOCK>>16) & 0xFF, PART_FIRST_BLOCK>>24,
						PART_BLOCKS & 0xFF, (PART_BLOCKS>>8) & 0xFF);
		pagebuf.word[460/4] = B2W((PART_BLOCKS>>16) & 0xFF, PART_BLOCKS>>24, 0x00, 0x00);

		pagebuf.word[508/4] = B2W(0x00, 0x00, 0x55, 0xAA);
		break;
	case PART_FIRST_BLOCK:
		pagebuf.word[  0/4] = B2W(0xEB, 0x00, 0x90, 0x00);
		/* sector size, sectors pr. cluster, reserved sectors */
		pagebuf.word[  8/4] = B2W(0, 0, 0, USBMS_BLOCKSIZE & 0xFFU);
		pagebuf.word[ 12/4] = B2W(USBMS_BLOCKSIZE >> 8, 1, 1, 0);
		/* number of FATs, root directory entries, total sectors */
		pagebuf.word[ 16/4] = B2W(1, 32, 0, PART_BLOCKS & 0xFF);
		/* total sectors, media descriptor, sectors pr. FAT */
		pagebuf.word[ 20/4] = B2W(PART_BLOCKS >> 8, 0xF8, 1, 0);
		/* extended boot signature, volume id, volume label, filesystem type */
		pagebuf.word[ 36/4] = B2W(0, 0, 0x29, 0xBE);
		pagebuf.word[ 40/4] = B2W(0xBA, 0xFE, 0xCA, 'n');
		pagebuf.word[ 44/4] = B2W('R', 'F', '5', '1');
		pagebuf.word[ 48/4] = B2W(' ', ' ', ' ', ' ');
		pagebuf.word[ 52/4] = B2W(' ', ' ', 'F', 'A');
		pagebuf.word[ 56/4] = B2W('T', '1', '2', ' ');
		pagebuf.word[ 60/4] = B2W(' ', ' ', 0, 0);
		/* boot signature */
		pagebuf.word[508/4] = B2W(0, 0, 0x55, 0xAA);
		break;
	}

	usbms.data = &pagebuf;
	usbms.chunk = USBMS_BLOCKSIZE;
}

static int
usbms_handle_read10(const struct command_block_wrapper *cbw)
{
	uint32_t lba = (((uint32_t)cbw->CBWCB[2]) << 24)
	             | (((uint32_t)cbw->CBWCB[3]) << 16)
	             | (((uint32_t)cbw->CBWCB[4]) << 8)
	             |  ((uint32_t)cbw->CBWCB[5]);
	uint32_t len = (((uint32_t)cbw->CBWCB[7]) << 8) | ((uint32_t)cbw->CBWCB[8]);

	debug("SCSI: read(10) lba=%lu, len=%lu\r\n", lba, len);
	usbms.bytes = len * USBMS_BLOCKSIZE;
	usbms.lba = lba;
	usbms_read();
	return 0;
}

static void
usbms_write_receive(void)
{
	switch (usbms.lba) {
	case PART_FIRST_BLOCK+1:
		usbms.buf = &fat12;
		usbms.chunk = USBMS_BLOCKSIZE;
		break;
	case PART_FIRST_BLOCK+2:
		usbms.buf = &rootentries;
		usbms.chunk = USBMS_BLOCKSIZE;
		break;
	default:
		usbms.buf = &pagebuf;
		if (usbms.bytes < sizeof(pagebuf))
			usbms.chunk = usbms.bytes;
		else
			usbms.chunk = sizeof(pagebuf);
	}
}

#ifndef NDEBUG
static void
find_file(unsigned int cn)
{
	struct fat12_directory_entry *entry;

	for (entry = &rootentries.entry[0]; entry->name[0]; entry++) {
		if (entry->start > 0 && entry->start < PART_BLOCKS) {
			if (fat12_getpage(entry->start, cn) > 0) {
				printf("  that's %.8s.%.3s\r\n",
						entry->name, entry->ext);
				return;
			}
		}
	}

	printf("  not found\r\n");
}
#else
static inline void find_file(unsigned int cn) {}
#endif

static void
parse_block(void)
{
	uint8_t *p = pagebuf.byte;
	uint8_t *end = p + ARRAY_SIZE(pagebuf.byte);

	for (; p < end; p++) {
		enum ihex_return iret = ihex_feed(&ihex_parser, *p);

		switch (iret) {
		case IHEX_MORE:
			break;

		case IHEX_DATA:
			if (prog_begin())
				goto out;
			if (ihex_record_program())
				goto out;
			break;

		case IHEX_EOF:
			debug("EOF\r\n");
			goto out;

		/* case IHEX_EPARSE: */
		default:
			debug("ERROR: %s\r\n", ihex_strerror(iret));
			goto out;
		}
	}

	prog_next_lba = usbms.lba + 1;
	return;
out:
	prog_done();
	ihex_init(&ihex_parser);
	prog_next_lba = 0;
	return;
}

static void
usbms_write_received(void)
{
#ifndef NDEBUG
	const char *extra;

	if (usbms.lba == PART_FIRST_BLOCK)
		extra = " (partbuf0)";
	else if (usbms.lba == PART_FIRST_BLOCK+1)
		extra = " (fat12)";
	else if (usbms.lba == PART_FIRST_BLOCK+2)
		extra = " (rootentries0)";
	else
		extra = "";

	debug("received lba %lu%s\r\n", usbms.lba, extra);
#endif
	if (usbms.lba == PART_FIRST_BLOCK)
		return;

	if (usbms.lba == PART_FIRST_BLOCK+2) {
		struct fat12_directory_entry *entry;

		for (entry = &rootentries.entry[0]; entry->name[0]; entry++) {
#ifndef NDEBUG
			if (entry->name[0] != 0x05 &&
			    entry->name[0] != 0xE5 &&
			    entry->attrs != 0x0F)
				printf("  %.8s.%.3s start:%hu size:%lu\r\n",
						entry->name, entry->ext,
						entry->start, entry->size);
#endif
			if (entry->ext[0] == 'H' &&
					entry->ext[1] == 'E' &&
					entry->ext[2] == 'X') {
				fat12_codebin_entry = entry->start;
				return;
			}
		}
		fat12_codebin_entry = 0;
		return;
	}

	if (usbms.lba > PART_FIRST_BLOCK+2 && (prog_next_lba == 0 || prog_next_lba == usbms.lba))
		parse_block();
}

static int
usbms_handle_write10(const struct command_block_wrapper *cbw)
{
	uint32_t lba = (((uint32_t)cbw->CBWCB[2]) << 24)
	             | (((uint32_t)cbw->CBWCB[3]) << 16)
	             | (((uint32_t)cbw->CBWCB[4]) << 8)
	             |  ((uint32_t)cbw->CBWCB[5]);
	uint32_t len = (((uint32_t)cbw->CBWCB[7]) << 8) | ((uint32_t)cbw->CBWCB[8]);

	debug("SCSI: write(10) lba=%lu, len=%lu\r\n", lba, len);
	usbms.lba = lba;
	usbms.bytes = len * USBMS_BLOCKSIZE;
	usbms.page = FLASH_PAGE_OFFSET;
	usbms_write_receive();
	return 0;
}

#ifdef USBMS_REPORT_LUNS
/* this is only used by FreeBSD with inquiry_data.version == 0x05 */
static int
usbms_handle_report_luns(const struct command_block_wrapper *cbw)
{
	static const struct {
		uint32_t list_length;
		uint8_t reserved[4];
		uint8_t lun_list[1][8];
	} lun_list = {
		.list_length = BE_UINT32(8),
		.reserved    = { 0x00, 0x00, 0x00, 0x00 },
		.lun_list    = {
			{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
		}
	};

	debug("SCSI: report luns\r\n");
	usbms.data = &lun_list;
	usbms.bytes = usbms.chunk = sizeof(lun_list);
	return 0;
}
#endif

struct usbms_cbw_handler {
	uint8_t opcode;
	uint8_t type;
	int (*fn)(const struct command_block_wrapper *cbw);
};

static const struct usbms_cbw_handler usbms_cbw_handlers[] = {
	{ 0x00, 0, usbms_handle_test_unit_ready },
	{ 0x03, 1, usbms_handle_request_sense },
	{ 0x12, 1, usbms_handle_inquiry },
	{ 0x1A, 1, usbms_handle_mode_sense6 },
	{ 0x1B, 0, usbms_handle_start_stop_unit },
	{ 0x1E, 0, usbms_handle_prevent_allow_medium_removal },
	{ 0x23, 1, usbms_handle_read_format_capacity },
	{ 0x25, 1, usbms_handle_read_capacity },
	{ 0x28, 1, usbms_handle_read10 },
	{ 0x2A, 2, usbms_handle_write10 },
#ifdef USBMS_REPORT_LUNS
	{ 0xA0, 1, usbms_handle_report_luns },
#endif
};

static enum usbms_state
usbms_handle_cbw(void)
{
	const struct usbms_cbw_handler *h = usbms_cbw_handlers;
	const struct usbms_cbw_handler *end = h + ARRAY_SIZE(usbms_cbw_handlers);
	uint8_t type = (usbms_buf.cbw.dCBWDataTransferLength == 0) ? 0
	             : (usbms_buf.cbw.bmCBWFlags & 0x80U) ? 1
	             : 2;
	uint8_t ret;

	for (; h < end; h++) {
		if (h->opcode == usbms_buf.cbw.CBWCB[0] && h->type == type) {
			ret = h->fn(&usbms_buf.cbw);
			goto found;
		}
	}
	debug("unknown request %hu type %u\r\n", usbms_buf.cbw.CBWCB[0], type);
	dumpcbw(&usbms_buf.cbw);
	ret = 0x01;

found:
	usbms_buf.csw.dCSWSignature = 0x53425355;
	usbms_buf.csw.bCSWStatus = ret;

	switch (type) {
	case 0:
		return USBMS_STATE_CSW;
	case 1:
		if (usbms.bytes > usbms_buf.csw.dCSWDataResidue)
			usbms.bytes = usbms_buf.csw.dCSWDataResidue;
		if (usbms.chunk > usbms.bytes)
			usbms.chunk = usbms.bytes;
		return USBMS_STATE_DATA_IN;
	case 2:
		usbms.bytes = usbms_buf.csw.dCSWDataResidue;
		return USBMS_STATE_DATA_OUT;
	default:
		__unreachable();
	}
}

static void
usb_handle_mass_storage_in(void)
{
	uint32_t flags = usb_ep_in_flags(USBMS_ENDPOINT);
	uint32_t packets;

	usb_ep_in_flags_clear(USBMS_ENDPOINT, flags);

	usbms.bytes -= usbms.chunk;
	usbms_buf.csw.dCSWDataResidue -= usbms.chunk;

	switch (usbms.state) {
	case USBMS_STATE_DATA_IN:
		if (usbms.bytes == 0) {
			usbms.state = USBMS_STATE_CSW;
			usbms.data = &usbms_buf.csw;
			usbms.bytes = usbms.chunk = 13;
		} else {
			usbms.lba++;
			usbms_read();
		}
		/* fallthrough */
	case USBMS_STATE_CSW:
		if (usbms.bytes == 0) {
			usbms.state = USBMS_STATE_CBW;
			usb_ep_out_dma_address_set(USBMS_ENDPOINT, &usbms_buf);
			usb_ep_out_transfer_size(USBMS_ENDPOINT, 1, USBMS_PACKETSIZE);
			usb_ep_out_enable(USBMS_ENDPOINT);
			break;
		}
		packets = (usbms.chunk + USBMS_PACKETSIZE - 1) / USBMS_PACKETSIZE;
		usb_ep_in_dma_address_set(USBMS_ENDPOINT, usbms.data);
		usb_ep_in_transfer_size(USBMS_ENDPOINT, packets, usbms.chunk);
		usb_ep_in_enable(USBMS_ENDPOINT);
		break;
	default:
		debug("TX: error %hu\r\n", usbms.state);
		usbms.state = USBMS_STATE_ERROR;
		usb_ep_in_stall(USBMS_ENDPOINT);
	}
}

static void
usb_handle_mass_storage_out(void)
{
	uint32_t flags = usb_ep_out_flags(USBMS_ENDPOINT);
	uint32_t len;
	uint32_t packets;

	usb_ep_out_flags_clear(USBMS_ENDPOINT, flags);

	switch (usbms.state) {
	case USBMS_STATE_CBW:
		len = USBMS_PACKETSIZE - usb_ep_out_bytes_left(USBMS_ENDPOINT);
		if (len == 31 && usbms_buf.word[0] == 0x43425355)
			usbms.state = usbms_handle_cbw();
		else
			usbms.state = USBMS_STATE_ERROR;
		break;
	case USBMS_STATE_DATA_OUT:
		if (USB->DOEP[USBMS_ENDPOINT-1].TSIZ) {
			usbms.state = USBMS_STATE_ERROR;
			break;
		}
		usbms.bytes -= usbms.chunk;
		usbms_buf.csw.dCSWDataResidue -= usbms.chunk;
		usbms_write_received();
		usbms.lba++;

		if (usbms.bytes == 0) {
			usbms.state = USBMS_STATE_CSW;
			usbms.data = &usbms_buf.csw;
			usbms.bytes = usbms.chunk = 13;
		} else
			usbms_write_receive();
		break;
	default:
		usbms.state = USBMS_STATE_ERROR;
	}

	switch (usbms.state) {
	case USBMS_STATE_DATA_OUT:
		packets = (usbms.chunk + USBMS_PACKETSIZE - 1) / USBMS_PACKETSIZE;
		usb_ep_out_dma_address_set(USBMS_ENDPOINT, usbms.buf);
		usb_ep_out_transfer_size(USBMS_ENDPOINT, packets, usbms.chunk);
		usb_ep_out_enable(USBMS_ENDPOINT);
		break;
	case USBMS_STATE_CSW:
		usbms.data = &usbms_buf.csw;
		usbms.bytes = usbms.chunk = 13;
		/* fallthrough */
	case USBMS_STATE_DATA_IN:
		packets = (usbms.chunk + USBMS_PACKETSIZE - 1) / USBMS_PACKETSIZE;
		usb_ep_in_dma_address_set(USBMS_ENDPOINT, usbms.data);
		usb_ep_in_transfer_size(USBMS_ENDPOINT, packets, usbms.chunk);
		usb_ep_in_enable(USBMS_ENDPOINT);
		break;
	default:
		debug("RX: error %hu\r\n", usbms.state);
		usbms.state = USBMS_STATE_ERROR;
		usb_ep_in_stall(USBMS_ENDPOINT);
		break;
	}
}

#ifdef ACM_DEBUG
static void
usb_handle_acm_in(void)
{
	uint32_t flags = usb_ep_in_flags(ACM_ENDPOINT);

	usb_ep_in_flags_clear(ACM_ENDPOINT, flags);

	if (usb_ep_in_flag_timeout(flags)) {
		debug("ACM: timeout!!\r\n");
		usb_ep_in_enable(ACM_ENDPOINT);
	}
	if (usb_ep_in_flag_complete(flags))
		acm_shipit();
}

static void
usb_handle_acm_out(void)
{
	uint32_t flags = usb_ep_out_flags(ACM_ENDPOINT);
	uint32_t bytes;

	usb_ep_out_flags_clear(ACM_ENDPOINT, flags);

	if (!usb_ep_out_flag_complete(flags))
		return;

	bytes = ACM_PACKETSIZE - usb_ep_out_bytes_left(ACM_ENDPOINT);

	acm_writeln(acm_outbuf.byte, bytes);

	usb_ep_out_dma_address_set(ACM_ENDPOINT, &acm_outbuf);
	usb_ep_out_transfer_size(ACM_ENDPOINT, 1, ACM_PACKETSIZE);
	usb_ep_out_enable(ACM_ENDPOINT);
}
#endif

static void
usb_handle_endpoints(void)
{
	uint32_t flags = usb_ep_flags();

	if (usb_ep_flag_in_or_out(0, flags))
		usb_handle_ep0();
	if (usb_ep_flag_in(USBMS_ENDPOINT, flags))
		usb_handle_mass_storage_in();
	if (usb_ep_flag_out(USBMS_ENDPOINT, flags))
		usb_handle_mass_storage_out();
#ifdef ACM_DEBUG
	if (usb_ep_flag_in(ACM_ENDPOINT, flags))
		usb_handle_acm_in();
	if (usb_ep_flag_out(ACM_ENDPOINT, flags))
		usb_handle_acm_out();
#endif
}

void
USB_IRQHandler(void)
{
	uint32_t flags = usb_flags();

	usb_flags_clear(flags);

	gpio_clear(LED_A);

	/* we ought to check the endpoint flag
	 * but most likely we're interrupted because
	 * of an endpoint, so just check endpoint
	 * status every time
	if (usb_flag_ep(flags))
	*/
	usb_handle_endpoints();

	if (flags & (USB_GINTSTS_RESETDET | USB_GINTSTS_WKUPINT)) {
		debug("WAKEUP.. ");
		usb_unsuspend();
		debug("done\r\n");
		goto out;
	}
	if (usb_flag_suspend(flags)) {
		debug("SUSPEND.. ");
		usb_suspend();
		debug("done\r\n");
		goto out;
	}
	if (usb_flag_reset(flags)) {
		debug("RESET.. ");
		usb_reset();
		debug("done\r\n");
	}
	if (usb_flag_enumdone(flags)) {
		debug("ENUMDONE.. ");
		usb_enumdone();
		debug("done\r\n");
	}
out:
	gpio_set(LED_A);
}

static void
usb_init(void)
{
	/* enable USB clock */
	clock_usb_enable();

	/* gate usbc clock when bus is idle,
	   signal full speed device */
	usb_mode(USB_CTRL_LEMIDLEEN
			| USB_CTRL_LEMOSCCTRL_GATE
			| USB_CTRL_DMPUAP_LOW);

	/* enable USB and USB core clock */
	clock_usbc_enable();
	/* make sure oscillator is ready and selected */
	clock_usbc_select_ushfrco();
	while (!clock_usbc_ushfrco_selected())
		/* wait */;
	/* enable clock recovery */
	clock_ushfrco_recovery_enable();
	/* wait for core to come out of reset */
	while (!usb_ahb_idle())
		/* wait */;

	/* initialise USB core */
	usb_ahb_config(USB_GAHBCFG_DMAEN
			| USB_GAHBCFG_HBSTLEN_INCR
			| USB_GAHBCFG_GLBLINTRMSK);
	usb_flags_clear(~0UL);
	usb_flags_enable(USB_GINTMSK_WKUPINTMSK
			| USB_GINTMSK_RESETDETMSK
			| USB_GINTMSK_OEPINTMSK
			| USB_GINTMSK_IEPINTMSK
			| USB_GINTMSK_ENUMDONEMSK
			| USB_GINTMSK_USBRSTMSK
			| USB_GINTMSK_USBSUSPMSK);

	/* initialize device mode */
	usb_device_config(USB_DCFG_RESVALID_DEFAULT
			| USB_DCFG_PERFRINT_80PCNT
			/* | USB_DCFG_ENA32KHZSUSP */
			| USB_DCFG_NZSTSOUTHSHK
			| USB_DCFG_DEVSPD_FS);

	/* ignore frame numbers on iso transfers,
	 * clear global nak flags, and disconnect
	 * from the USB bus */
	usb_device_control(USB_DCTL_IGNRFRMNUM
			| USB_DCTL_PWRONPRGDONE
			| USB_DCTL_CGOUTNAK
			| USB_DCTL_CGNPINNAK
			| USB_DCTL_SFTDISCON);

	/* now that we're disconnected, enable USB phy pins */
	usb_pins_enable();

	/* setup fifo allocation */
	usb_allocate_buffers(USB_FIFO_RXSIZE,
			USB_FIFO_TX0SIZE,
			USB_FIFO_TX1SIZE,
			USB_FIFO_TX2SIZE,
			USB_FIFO_TX3SIZE);

	/* reset endpoint registers */
	usb_ep_reset();

	/* enable interrupt */
	NVIC_SetPriority(USB_IRQn, 1);
	NVIC_EnableIRQ(USB_IRQn);

	/* tell the host we're here */
	usb_connect();
}

#ifdef LEUART_DEBUG
static struct {
	volatile uint32_t first;
	volatile uint32_t last;
	uint8_t buf[2048];
} leuart0_output;

void
LEUART0_IRQHandler(void)
{
	uint32_t first = leuart0_output.first;
	uint32_t last = leuart0_output.last;

	if (first == last) {
		leuart0_flag_tx_buffer_level_disable();
		return;
	}

	leuart0_txdata(leuart0_output.buf[first++]);
	leuart0_output.first = first % ARRAY_SIZE(leuart0_output.buf);
}

static void
leuart0_write(const uint8_t *ptr, size_t len)
{
	uint32_t last = leuart0_output.last;

	for (; len > 0; len--) {
		leuart0_output.buf[last++] = *ptr++;
		last %= ARRAY_SIZE(leuart0_output.buf);
	}
	leuart0_output.last = last;
	leuart0_flag_tx_buffer_level_enable();
}

static void
leuart0_init(void)
{
	/* route 24MHz core clock / 2 / 8 to LEUART0 */
	/* clock_le_div2(); bootup default */
	clock_lfb_select_hfclk();
	clock_leuart0_div8();
	clock_leuart0_enable();
	while (clock_lf_syncbusy())
		/* wait */;

	gpio_mode(GPIO_PD4, GPIO_MODE_PUSHPULL); /* LEUART0 TX */
	gpio_mode(GPIO_PD5, GPIO_MODE_INPUT);    /* LEUART0 RX */

	leuart0_freeze();
	leuart0_config(LEUART_CONFIG_8N1);
	leuart0_clock_div(3077); /* 256*(f/115200 - 1), f = 24MHz / 2 / 8 */
	leuart0_pins(LEUART_PINS_RXTX0); /* RX -> PD5, TX -> PD4 */
	leuart0_rxtx_enable();
	leuart0_update();
	while (leuart0_syncbusy())
		/* wait */;

	/* enable interrupt */
	NVIC_SetPriority(LEUART0_IRQn, 2);
	NVIC_EnableIRQ(LEUART0_IRQn);
}
#else
static inline void leuart0_init(void) {}
#endif

#ifndef NDEBUG
ssize_t __used
_write(int fd, const uint8_t *ptr, size_t len)
{
#ifdef LEUART_DEBUG
	leuart0_write(ptr, len);
#endif
#ifdef ACM_DEBUG
	acm_write(ptr, len);
#endif
	return len;
}
#endif

static void
indata_parse(void)
{
	for (uint8_t *p = indata; indata_len > 0; indata_len--) {
		enum ihex_return iret = ihex_feed(&ihex_parser, *p++);

		switch (iret) {
		case IHEX_MORE:
			break;

		case IHEX_DATA:
			if (prog_begin())
				goto prog_err;
			if (ihex_record_program())
				goto prog_err;
			break;

		case IHEX_EOF:
			debug("EOF\r\n");
			dfu_status.bState = DFU_dfuDNLOAD_SYNC;
			goto out;

		case IHEX_EPARSE:
			debug("ERROR: %s\r\n", ihex_strerror(iret));
			dfu_status.bStatus = DFU_errTARGET;
			dfu_status.bState = DFU_dfuERROR;
			goto out;

		default:
			debug("ERROR: %s\r\n", ihex_strerror(iret));
			dfu_status.bStatus = DFU_errFILE;
			dfu_status.bState = DFU_dfuERROR;
			goto out;
		}
	}

	dfu_status.bState = DFU_dfuDNLOAD_SYNC;
	return;
prog_err:
	dfu_status.bStatus = DFU_errPROG;
	dfu_status.bState = DFU_dfuERROR;
out:
	prog_done();
	ihex_init(&ihex_parser);
}

void __noreturn
main(void)
{
	/* switch to 48MHz / 2 ushfrco as core clock */
	clock_ushfrco_48mhz_div2();
	clock_ushfrco_enable();
	while (!clock_ushfrco_ready())
		/* wait */;
	clock_hfclk_select_ushfrco();
	while (!clock_ushfrco_selected())
		/* wait */;
	clock_lfrco_enable();
	clock_hfrco_disable();

	/* disable auxfrco, only needed to program flash */
	clock_auxhfrco_disable();

	clock_peripheral_div1();

	clock_le_enable();
	clock_lf_config(CLOCK_LFA_DISABLED | CLOCK_LFB_DISABLED | CLOCK_LFC_LFRCO);
	clock_usble_enable();
	while (clock_lf_syncbusy())
		/* wait */;

	/* enable and configure GPIOs */
	clock_gpio_enable();
	gpio_clear(LED_POWER);
	gpio_set(LED_BOOT);
	gpio_set(LED_A);
	gpio_set(LED_B);
	gpio_mode(LED_POWER, GPIO_MODE_WIREDAND);
	gpio_mode(LED_BOOT,  GPIO_MODE_WIREDAND);
	gpio_mode(LED_A,     GPIO_MODE_WIREDAND);
	gpio_mode(LED_B,     GPIO_MODE_WIREDAND);

	leuart0_init();

	debug("\r\nHello World!\r\n");

	swd_init();

	ihex_init(&ihex_parser);

	dfu_status.bState = DFU_dfuIDLE;
	dfu_status.bwPollTimeout[0] = 2;

	gpio_clear(LED_A);
	usb_init();
	gpio_set(LED_A);

	/* sleep when not interrupted */
	while (1) {
		switch (dfu_status.bState) {
		case DFU_dfuDNBUSY:
			indata_parse();
			break;
		}

		__WFI();
	}
}
