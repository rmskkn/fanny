#include <string.h>

#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/desig.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/usbd.h>

#include "board.h"
#include "usb_cdc.h"

#define EP_DATA_OUT  0x01
#define EP_DATA_IN   0x82
#define EP_NOTIFY    0x83
#define PACKET_SIZE  64
#define TX_TIMEOUT_MS 10

static const struct usb_device_descriptor dev_desc = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = USB_CLASS_CDC,
	.bMaxPacketSize0 = 64,
	/* ST "Virtual COM Port" IDs; replace with your own VID/PID for anything shipped. */
	.idVendor = 0x0483,
	.idProduct = 0x5740,
	.bcdDevice = 0x0100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

static const struct usb_endpoint_descriptor notify_endp[] = { {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = EP_NOTIFY,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 255,
} };

static const struct usb_endpoint_descriptor data_endp[] = { {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = EP_DATA_OUT,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = PACKET_SIZE,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = EP_DATA_IN,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = PACKET_SIZE,
	.bInterval = 1,
} };

static const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_call_management_descriptor call_mgmt;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,
	},
	.call_mgmt = {
		.bFunctionLength = sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 1,
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 0,
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 0,
		.bSubordinateInterface0 = 1,
	},
};

static const struct usb_interface_descriptor comm_iface[] = { {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.endpoint = notify_endp,
	.extra = &cdcacm_functional_descriptors,
	.extralen = sizeof(cdcacm_functional_descriptors),
} };

static const struct usb_interface_descriptor data_iface[] = { {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.endpoint = data_endp,
} };

static const struct usb_interface ifaces[] = {
	{ .num_altsetting = 1, .altsetting = comm_iface },
	{ .num_altsetting = 1, .altsetting = data_iface },
};

static const struct usb_config_descriptor config_desc = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
	.bNumInterfaces = 2,
	.bConfigurationValue = 1,
	.bmAttributes = 0x80,
	.bMaxPower = 50, /* 100 mA; fans run from the 12 V input, not VBUS */
	.interface = ifaces,
};

static char serial[25];
static const char *usb_strings[] = { "DIY", "STM32 PWM Fan Controller", serial };

static usbd_device *usbd;
static uint8_t control_buffer[128];
static struct usb_cdc_line_coding line_coding = {
	.dwDTERate = 115200, .bCharFormat = 0, .bParityType = 0, .bDataBits = 8,
};
static volatile bool host_dtr;

/* RX ring, filled from the USB callback (main loop context, inside usbd_poll). */
static uint8_t rx_buf[256];
static volatile uint16_t rx_head, rx_tail;

static enum usbd_request_return_codes control_request(usbd_device *dev, struct usb_setup_data *req,
	uint8_t **buf, uint16_t *len, usbd_control_complete_callback *complete)
{
	(void)dev;
	(void)complete;

	switch (req->bRequest) {
	case USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		host_dtr = req->wValue & 1;
		return USBD_REQ_HANDLED;
	case USB_CDC_REQ_SET_LINE_CODING:
		if (*len < sizeof(line_coding))
			return USBD_REQ_NOTSUPP;
		memcpy(&line_coding, *buf, sizeof(line_coding));
		return USBD_REQ_HANDLED;
	case USB_CDC_REQ_GET_LINE_CODING:
		*buf = (uint8_t *)&line_coding;
		*len = sizeof(line_coding);
		return USBD_REQ_HANDLED;
	}
	return USBD_REQ_NOTSUPP;
}

static void data_rx(usbd_device *dev, uint8_t ep)
{
	uint8_t pkt[PACKET_SIZE];
	int len = usbd_ep_read_packet(dev, ep, pkt, sizeof(pkt));

	for (int i = 0; i < len; i++) {
		uint16_t next = (rx_head + 1) % sizeof(rx_buf);

		if (next == rx_tail)
			break; /* full: drop, the protocol reports the broken line */
		rx_buf[rx_head] = pkt[i];
		rx_head = next;
	}
}

static void set_config(usbd_device *dev, uint16_t wValue)
{
	(void)wValue;

	usbd_ep_setup(dev, EP_DATA_OUT, USB_ENDPOINT_ATTR_BULK, PACKET_SIZE, data_rx);
	usbd_ep_setup(dev, EP_DATA_IN, USB_ENDPOINT_ATTR_BULK, PACKET_SIZE, NULL);
	usbd_ep_setup(dev, EP_NOTIFY, USB_ENDPOINT_ATTR_INTERRUPT, 16, NULL);
	usbd_register_control_callback(dev, USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				       USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT, control_request);
}

void usb_cdc_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_USB);

	/* D+ has a fixed 1.5k pull-up: hold it low briefly so the host re-enumerates after reset. */
	gpio_clear(GPIOA, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	for (uint32_t t0 = millis(); millis() - t0 < 10;)
		;
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO12);

	desig_get_unique_id_as_string(serial, sizeof(serial));
	usbd = usbd_init(&st_usbfs_v1_usb_driver, &dev_desc, &config_desc, usb_strings, 3,
			 control_buffer, sizeof(control_buffer));
	usbd_register_set_config_callback(usbd, set_config);
}

void usb_cdc_poll(void)
{
	usbd_poll(usbd);
}

bool usb_cdc_getc(uint8_t *c)
{
	if (rx_tail == rx_head)
		return false;
	*c = rx_buf[rx_tail];
	rx_tail = (rx_tail + 1) % sizeof(rx_buf);
	return true;
}

void usb_cdc_write(const char *s)
{
	size_t len = strlen(s);

	while (len && host_dtr) {
		uint16_t n = len > PACKET_SIZE ? PACKET_SIZE : (uint16_t)len;
		uint32_t t0 = millis();

		while (usbd_ep_write_packet(usbd, EP_DATA_IN, s, n) == 0) {
			if (millis() - t0 >= TX_TIMEOUT_MS)
				return;
			usbd_poll(usbd);
		}
		s += n;
		len -= n;
	}
}
