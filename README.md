# CorsairPSU

CorsairPSU is a simple hwmon Linux kernel module for Corsair RMi and HXi Series PSUs to view power usage, and more info about your PSU.

All values are exposed using the sysfs interface (`/sys/class/hwmon/hwmon*/`).

Sensors, except a few custom attributes, can also be viewed with the `sensors` command.

## Disclaimer

A `corsair-psu` module is now part of the Linux kernel, see https://github.com/torvalds/linux/blob/master/drivers/hwmon/corsair-psu.c. You should just use it instead of this one.

## Usage

```bash
$ sensors
corsairpsu-hid-3-3
Adapter: HID adapter
voltage supply: 230.00 V
voltage 12v:     12.09 V  (min =  +8.41 V, max = +15.59 V)
voltage 5v:       5.01 V  (min =  +3.50 V, max =  +6.50 V)
voltage 3.3v:     3.30 V  (min =  +2.31 V, max =  +4.30 V)
fan rpm:           0 RPM
temp1:           +29.8°C  (high = +70.0°C)
temp2:           +20.0°C  (high = +70.0°C)
power total:     70.00 W
power 12v:       50.00 W
power 5v:        14.00 W
power 3.3v:       7.00 W
current 12v:      4.75 A  (max = +65.00 A)
current 5v:       2.81 A  (max = +40.00 A)
current 3.3v:     2.25 A  (max = +40.00 A)
$ cat /sys/class/hwmon/hwmon3/current_uptime
6884
$ cat /sys/class/hwmon/hwmon3/total_uptime
23216087
$ cat /sys/class/hwmon/hwmon3/ocp_mode
2
```

## Supported devices

This driver should work with any [Corsair i-CUE PSU](https://www.corsair.com/us/en/Categories/Products/Power-Supply-Units/c/Cor_Products_PowerSupply_Units?q=%3Afeatured%3ApsuLinkSupport%3AYes).

- RM550i
- RM650i
- RM750i
- RM850i
- RM1000i
- HX550i
- HX650i
- HX750i
- HX850i
- HX1000i legacy and Series 2023
- HX1200i legacy, Series 2023 and Series 2025
- HX1500i legacy and Series 2023

## Install

Basic install

```bash
make
sudo insmod corsairpsu.ko
```

or with DKMS

```bash
sudo apt install dkms git build-essential linux-headers-$(uname -r)
sudo make dkms-install
sudo modprobe corsairpsu
```

## Uninstall

Basic uninstall

```bash
sudo rmmod corsairpsu
```

or with DKMS

```bash
sudo modprobe -r corsairpsu
sudo make dkms-uninstall
```

## Dev

```bash
make
sudo rmmod corsairpsu; sudo insmod corsairpsu.ko; sensors | grep corsairpsu -A 15
sudo modprobe usbmon; sudo wireshark
```
