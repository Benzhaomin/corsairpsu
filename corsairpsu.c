/*
 * HID driver for the Corsair RMi and HXi series of PSUs
 *
 * Copyright (c) 2020 Benjamin Maisonnas
 *
 * Tested devices:
 *	- RM650i
 *
 * Based on:
 *  - corsairmi		Copyright (c) 2016 notaz 		 https://github.com/notaz/corsairmi
 *  - corsair_hydro Copyright (c) 2018 Lukas Kahnert https://github.com/OpenProgger/corsair_hydro
 *  - zenpower		Copyright (c) 2020 Ondrej Čerman https://github.com/ocerman/zenpower
 * 
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

/*
[    2.918336] usb 1-12: New USB device found, idVendor=1b1c, idProduct=1c0a, bcdDevice= 0.02
[    2.918339] usb 1-12: New USB device strings: Mfr=1, Product=2, SerialNumber=0
[    2.918340] usb 1-12: Product:
[    2.918341] usb 1-12: Manufacturer:
[    2.933392] hid-generic 0003:1B1C:1C0A.0003: hiddev0,hidraw0: USB HID v1.11 Device [                                                ] on usb-0000:02:00.0-12/input0
*/

// TODO: https://www.kernel.org/doc/Documentation/hwmon/hwmon-kernel-api.txt

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>

#define USB_VENDOR_ID_CORSAIR			0x1b1c

struct corsairpsu_data {
	struct usb_device *usbdev;
	char *buf;
};

/* 
  Send a command and get its output
	- write using interrupt to endpoint 0x01
	- read using interrupt from endpoint 0x82
*/
static int send_msg(struct corsairpsu_data* data) {
	int ret, actual_length;
	
	ret = usb_interrupt_msg(data->usbdev, usb_sndintpipe(data->usbdev, 0x01),
				data->buf, 64, &actual_length,
				USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&data->usbdev->dev, "Failed to send HID Request (error %d)\n", ret);
		return ret;
	}
	
	memset(data->buf, 0, 64);
	ret = usb_interrupt_msg(data->usbdev, usb_rcvintpipe(data->usbdev, 0x81),
				data->buf, 64, &actual_length,
				USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&data->usbdev->dev, "Failed to get HID Response (error: %d).\n", ret);
		return ret;
	}
	
	return 0;
}

static int corsairpsu_probe(struct hid_device *dev, const struct hid_device_id *id) {
	int ret;
	struct usb_interface *usbif = to_usb_interface(dev->dev.parent);
	struct corsairpsu_data *data;

	printk(KERN_DEBUG "Corsair PSU starting\n");

	// hid device setup
	ret = hid_parse(dev);
	if (ret != 0) {
		hid_err(dev, "hid_parse failed\n");
		return ret;
	}
	ret = hid_hw_start(dev, HID_CONNECT_HIDRAW);
	if (ret != 0) {
		hid_err(dev, "hid_hw_start failed\n");
		return ret;
	}
	
	// mem alloc
	data = devm_kzalloc(&dev->dev, sizeof(*data), GFP_KERNEL);
	if (data == NULL)
		return -ENOMEM;
	data->usbdev = interface_to_usbdev(usbif);
	data->buf = devm_kzalloc(&dev->dev, 64, GFP_KERNEL);
	if (data->buf == NULL)
		return -ENOMEM;

	// say hello
	memcpy(data->buf, (char[]){ 0xfe, 0x03, 0x00 }, 3 * sizeof data->buf[0]);
	send_msg(data);

	printk(KERN_DEBUG "Corsair PSU all done\n");

	return 0;
}

static void corsairpsu_remove(struct hid_device *dev) {
	hid_hw_stop(dev);
}

static const struct hid_device_id corsairpsu_devices[] = {
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c0a) }, // RM650i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c0b) }, // RM750i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c0c) }, // RM850i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c0d) }, // RM1000i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c04) }, // HX650i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c05) }, // HX750i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c06) }, // HX850i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c07) }, // HX1000i
	{ HID_USB_DEVICE(USB_VENDOR_ID_CORSAIR, 0x1c08) }, // HX1200i
	{}
};

MODULE_DEVICE_TABLE(hid, corsairpsu_devices);

static struct hid_driver corsairpsu_driver = {
	.name 		= "corsairpsu",
	.id_table 	= corsairpsu_devices,
	.probe 		= corsairpsu_probe,
	.remove 	= corsairpsu_remove,
};

module_hid_driver(corsairpsu_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Benjamin Maisonnas");
MODULE_DESCRIPTION("HID driver for the Corsair RMi and HXi series of PSUs");
