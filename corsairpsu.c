/*
 * HID driver for the Corsair RMi and HXi series of PSUs
 *
 * Copyright (c) 2020 Benjamin Maisonnas <ben@wainei.net>
 *
 * Tested devices:
 *	- RM650i
 *	- HX1000i
 *
 * Based on:
 *  - corsairmi        Copyright (c) 2016 notaz             https://github.com/notaz/corsairmi
 *  - corsair_hydro    Copyright (c) 2018 Lukas Kahnert     https://github.com/OpenProgger/corsair_hydro
 *  - zenpower         Copyright (c) 2020 Ondrej Čerman     https://github.com/ocerman/zenpower
 *  - OpenCorsairLink  Copyright (c) 2017-2019 Sean Nelson  https://github.com/audiohacked/OpenCorsairLink/
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
[    2.933392] hid-generic 0003:1B1C:1C0A.0003: hiddev0,hidraw0: USB HID v1.11 Device [] on usb-0000:02:00.0-12/input0
*/

#include <linux/hid.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/hwmon.h>

MODULE_DESCRIPTION("hwmon HID driver for the Corsair RMi and HXi series of PSUs");
MODULE_AUTHOR("Benjamin Maisonnas");
MODULE_LICENSE("GPL");
MODULE_VERSION("0.1.7");

#define USB_VENDOR_ID_CORSAIR	0x1b1c

#define USB_MUTEX_LOCKED_ERR -99
static DEFINE_MUTEX(usbdev_mutex);

struct corsairpsu_data {
	struct usb_device *usbdev;
	char *buf;
};

/*
	Send a command and get its output
	- write using interrupt to endpoint 0x01
	- read using interrupt from endpoint 0x82
*/
static int usb_send_recv_cmd(struct corsairpsu_data* data) {
	int ret, actual_length;

	ret = mutex_trylock(&usbdev_mutex);
	if (ret == 0) {
		return USB_MUTEX_LOCKED_ERR;
	}

	ret = usb_interrupt_msg(data->usbdev, usb_sndintpipe(data->usbdev, 0x01),
				data->buf, 64, &actual_length,
				USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&data->usbdev->dev, "Failed to send HID Request (error %d)\n", ret);
		mutex_unlock(&usbdev_mutex);
		return ret;
	}

	memset(data->buf, 0, 64);
	ret = usb_interrupt_msg(data->usbdev, usb_rcvintpipe(data->usbdev, 0x81),
				data->buf, 64, &actual_length,
				USB_CTRL_SET_TIMEOUT);
	if (ret < 0) {
		dev_err(&data->usbdev->dev, "Failed to get HID Response (error: %d).\n", ret);
		mutex_unlock(&usbdev_mutex);
		return ret;
	}

	mutex_unlock(&usbdev_mutex);

	return 0;
}

/*
	Send/receive command helper

	Name             Length  Opcode  Example
	---              ---     ---     ---
	name             0xFE    0x03    'RM650i'
	vendor           0x03    0x99    'CORSAIR'
	product          0x03    0x9A    'RM650i'
	temp1            0x03    0x8D    46.2
	temp2            0x03    0x8E    39.0
	fan rpm          0x03    0x90    0.0
	fan control      0x03    0xF0    0 (hardware) or 1 (software)
	voltage supply   0x03    0x88    230.0
	power total      0x03    0xEE    82.0
	voltage 12v      0x03    0x8B    12.1
	current 12v      0x03    0x8C    5.2
	power 12v        0x03    0x96    66.0
	voltage 5v       0x03    0x8B    5.0
	current 5v       0x03    0x8C    2.9
	power 5v         0x03    0x96    14.0
	voltage 3.3v     0x03    0x8B    3.3
	current 3.3v     0x03    0x8C    2.2
	power 3.3v       0x03    0x96    7.0
	total uptime     0x03    0xD1    23160895
	uptime           0x03    0xD2    41695
	ocp mode         0x03    0xD8    1 (mono rail) or 2 (multi rail)

	select one of the three rails with 0x02, 0x00, [0x00|0x01|0x02]

	TODO

	fan mode         0x03    0x3A    TODO
	fan pwm          0x03    0x3B    TODO
	fan status       0x03    0x81    TODO
*/

static int send_recv_cmd_impl(struct corsairpsu_data* data, u8 addr, u8 opcode, u8 opdata,
			 void *result, size_t result_size) {
	int ret;

	memset(data->buf, 0, 64);
	data->buf[0] = addr;
	data->buf[1] = opcode;
	data->buf[2] = opdata;

	ret = usb_send_recv_cmd(data);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int send_recv_cmd(struct corsairpsu_data* data, u8 addr, u8 opcode, u8 opdata,
							void *result, size_t result_size) {
	int ret;

	ret = send_recv_cmd_impl(data, addr, opcode, opdata, result, result_size);
	if (ret < 0) {
		return ret;
	}
	if(data->buf[1] != opcode) {
		//we got an error response from the PSU. Try handshaking again:
		ret = send_recv_cmd_impl(data, 0xfe, 0x03, 0x00, NULL, 0);
		if(ret < 0) {
			return ret;
		}
		//we got a good handshake. retry the original command:
		ret = send_recv_cmd_impl(data, addr, opcode, opdata, result, result_size);
		if (ret < 0) {
			return ret;
		}
		if(data->buf[1] != opcode) {
			//well, looks like it really was an error.
			return -ENODATA;
		}
		//success! fall down to the next block & copy the data out.
	}
	if (result != NULL && result_size > 0) {
		memcpy(result, data->buf + 2, result_size);
	}

	return 0;
}

/*
	LINEAR11 format is used for non-output voltage (See PMBusPart II, Section 7.3)

	X = Y ∙ 2N

	Where:
	– X is the real world value
	– Y is a signed 11 bit 2’s complement integer
	– N is a signed 5 bit 2’s complement integer

	The values N and Y form a 16-bit value sent over the bus as {N, Y}

	https://github.com/torvalds/linux/blob/v5.5/drivers/hwmon/pmbus/pmbus_core.c#L612
*/
static long pmbus_linear11_to_long(u16 v16, int scale) {
	s16 exponent;
	s32 mantissa;
	long val;

	exponent = ((s16)v16) >> 11;
	mantissa = ((s16)((v16 & 0x7ff) << 5)) >> 5;
	val = mantissa * scale;

	if (exponent >= 0)
		val <<= exponent;
	else
		val >>= -exponent;

	return val;
}

static int corsairpsu_read(struct device *dev, enum hwmon_sensor_types type,
							u32 attr, int channel, long *val) {

	struct corsairpsu_data *data = dev_get_drvdata(dev);
	u16 reading;
	int ret;

	switch (type) {
		// Chip
		case hwmon_chip:
			switch (attr) {
				case hwmon_chip:
					switch (channel) {
						case 0: // temp1
							ret = send_recv_cmd(data, 0x03, 0x8D, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;

		// Temperatures (millidegree Celsius)
		case hwmon_temp:
			switch (attr) {
				case hwmon_temp_input:
					switch (channel) {
						case 0: // temp1
							ret = send_recv_cmd(data, 0x03, 0x8D, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						case 1: // temp2
							ret = send_recv_cmd(data, 0x03, 0x8E, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;

		// Fan (RPM)
		case hwmon_fan:
			switch (attr) {
				case hwmon_fan_input:
					switch (channel) {
						case 0: // fan rpm
							ret = send_recv_cmd(data, 0x03, 0x90, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 0L);
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;

		// Voltage (millivolt)
		case hwmon_in:
			switch (attr) {
				case hwmon_in_input:
					switch (channel) {
						case 0: // voltage supply
							ret = send_recv_cmd(data, 0x03, 0x88, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						case 1: // voltage 12v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x00, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x8B, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						case 2: // voltage 5v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x01, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x8B, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						case 3: // voltage 3.3v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x02, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x8B, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;

		// Current (microamp)
		case hwmon_curr:
			switch (attr) {
				case hwmon_curr_input:
					switch (channel) {
						case 0: // current 12v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x00, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x8C, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						case 1: // current 5v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x01, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x8C, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						case 2: // current 3.3v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x02, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x8C, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000L);
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;

		// Power (microwatt)
		case hwmon_power:
			switch (attr) {
				case hwmon_power_input:
					switch (channel) {
						case 0: // power total
							ret = send_recv_cmd(data, 0x03, 0xEE, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000000L);
							break;
						case 1: // power 12v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x00, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x96, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000000L);
							break;
						case 2: // power 5v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x01, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x96, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000000L);
							break;
						case 3: // power 3.3v
							ret = send_recv_cmd(data, 0x02, 0x00, 0x02, NULL, 0);
							if (ret < 0) {
								goto err;
							}
							ret = send_recv_cmd(data, 0x03, 0x96, 0x00, &reading, sizeof(u16));
							if (ret < 0) {
								goto err;
							}
							*val = pmbus_linear11_to_long(reading, 1000000L);
							break;
						default:
							return -EOPNOTSUPP;
					}
					break;
				default:
					return -EOPNOTSUPP;
			}
			break;

		default:
			return -EOPNOTSUPP;
	}

	return 0;

err:
	if (ret == USB_MUTEX_LOCKED_ERR) {
		return -EINVAL;
	}
	return -EOPNOTSUPP;
}

static const char *corsairpsu_chip_label[] = {
	"total uptime",
	"uptime",
};

static const char *corsairpsu_temp_label[] = {
	"temp1",
	"temp2",
};

static const char *corsairpsu_fan_label[] = {
	"fan rpm",
};

static const char *corsairpsu_in_label[] = {
	"voltage supply",
	"voltage 12v",
	"voltage 5v",
	"voltage 3.3v",
};

static const char *corsairpsu_curr_label[] = {
	"current 12v",
	"current 5v",
	"current 3.3v",
};

static const char *corsairpsu_power_label[] = {
	"power total",
	"power 12v",
	"power 5v",
	"power 3.3v",
};

static int corsairpsu_read_labels(struct device *dev,
				enum hwmon_sensor_types type, u32 attr,
				int channel, const char **str) {
	switch (type) {
		case hwmon_chip:
			*str = corsairpsu_chip_label[channel];
			break;
		case hwmon_temp:
			*str = corsairpsu_temp_label[channel];
			break;
		case hwmon_fan:
			*str = corsairpsu_fan_label[channel];
			break;
		case hwmon_in:
			*str = corsairpsu_in_label[channel];
			break;
		case hwmon_curr:
			*str = corsairpsu_curr_label[channel];
			break;
		case hwmon_power:
			*str = corsairpsu_power_label[channel];
			break;
		default:
			return -EOPNOTSUPP;
	}

	return 0;
}

static const struct hwmon_channel_info *corsairpsu_info[] = {
	HWMON_CHANNEL_INFO(temp,
		HWMON_T_INPUT | HWMON_T_LABEL,		// temp1
		HWMON_T_INPUT | HWMON_T_LABEL),		// temp2

	HWMON_CHANNEL_INFO(fan,
		HWMON_F_INPUT | HWMON_F_LABEL),		// fan rpm

	HWMON_CHANNEL_INFO(in,
		HWMON_I_INPUT | HWMON_I_LABEL,		// voltage supply
		HWMON_I_INPUT | HWMON_I_LABEL,		// voltage 12v
		HWMON_I_INPUT | HWMON_I_LABEL,		// voltage 5v
		HWMON_I_INPUT | HWMON_I_LABEL),		// voltage 3.3v

	HWMON_CHANNEL_INFO(curr,
		HWMON_C_INPUT | HWMON_C_LABEL,		// current 12v
		HWMON_C_INPUT | HWMON_C_LABEL,		// current 5v
		HWMON_C_INPUT | HWMON_C_LABEL),		// current 3.3v

	HWMON_CHANNEL_INFO(power,
		HWMON_P_INPUT | HWMON_P_LABEL,		// power total
		HWMON_P_INPUT | HWMON_P_LABEL,		// power 12v
		HWMON_P_INPUT | HWMON_P_LABEL,		// power 5v
		HWMON_P_INPUT | HWMON_P_LABEL),		// power 3.3v

	NULL
};

static umode_t corsairpsu_is_visible(const void *rdata, enum hwmon_sensor_types type,
										u32 attr, int channel) {
	// read-only for everybody
	return 0444;
}

static const struct hwmon_ops corsairpsu_hwmon_ops = {
	.is_visible = corsairpsu_is_visible,
	.read = corsairpsu_read,
	.read_string = corsairpsu_read_labels,
};

static const struct hwmon_chip_info corsairpsu_chip_info = {
	.ops = &corsairpsu_hwmon_ops,
	.info = corsairpsu_info,
};

// helper to read a custom attribute
static ssize_t u32_show(struct device *dev, struct device_attribute *attr,
						char *buf, u16 opcode) {
	int len = 0;
	struct corsairpsu_data *data = dev_get_drvdata(dev);
	u32 reading;

	send_recv_cmd(data, 0x03, opcode, 0x00, &reading, sizeof(u32));
	len += sprintf(buf, "%u\n", reading);

	return len;
}

// total PSU uptime in seconds
static ssize_t total_uptime_show(struct device *dev, struct device_attribute *attr,
								char *buf) {
	return u32_show(dev, attr, buf, 0xD1);
}
static DEVICE_ATTR_RO(total_uptime);

// current PSU uptime in seconds
static ssize_t current_uptime_show(struct device *dev,
				struct device_attribute *attr, char *buf) {
	return u32_show(dev, attr, buf, 0xD2);
}
static DEVICE_ATTR_RO(current_uptime);

// OCP (Over Current Protection) mode
// 1 for single rail, 2 for multi rail
static ssize_t ocp_mode_show(struct device *dev, struct device_attribute *attr,
							char *buf) {
	return u32_show(dev, attr, buf, 0xD8);
}
static DEVICE_ATTR_RO(ocp_mode);

// Fan control mode
// 0 for hardware or 1 for software
static ssize_t fan_control_show(struct device *dev, struct device_attribute *attr,
								char *buf) {
	return u32_show(dev, attr, buf, 0xF0);
}
static DEVICE_ATTR_RO(fan_control);

// custom attributes, can be read through /sys/class/hwmon/hwmon* but not 'sensors'
static struct attribute *corsairpsu_attrs[] = {
	&dev_attr_total_uptime.attr,
	&dev_attr_current_uptime.attr,
	&dev_attr_ocp_mode.attr,
	&dev_attr_fan_control.attr,
	NULL
};

static const struct attribute_group corsairpsu_group = {
	.attrs = corsairpsu_attrs
};
__ATTRIBUTE_GROUPS(corsairpsu);

static int corsairpsu_probe(struct hid_device *dev, const struct hid_device_id *id) {
	int ret;
	struct usb_interface *usbif = to_usb_interface(dev->dev.parent);
	struct corsairpsu_data *data;
	struct device *hwmon_dev;
	char name[32], vendor[32], product[32];

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

	// register hwmon device
	hwmon_dev = devm_hwmon_device_register_with_info(
		&dev->dev, "corsairpsu", data, &corsairpsu_chip_info, corsairpsu_groups
	);

	// say hello
	send_recv_cmd(data, 0xfe, 0x03, 0x00, name, sizeof(name)-1);
	send_recv_cmd(data, 0x03, 0x99, 0x00, vendor, sizeof(name)-1);
	send_recv_cmd(data, 0x03, 0x9a, 0x00, product, sizeof(name)-1);
	printk(KERN_DEBUG "corsairpsu driver ready for %s, %s, %s\n", name, vendor, product);

	return PTR_ERR_OR_ZERO(hwmon_dev);
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
