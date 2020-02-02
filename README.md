# CorsairPSU

CorsairPSU is a simple hwmon Linux kernel module for Corsair RMi and HXi Series PSUs to view power usage, and more info about your PSU.

```bash
$ sensors
corsairpsu-hid-3-3
Adapter: HID adapter
voltage supply: 230.00 V
voltage 12v:     12.09 V
voltage 5v:       5.00 V
voltage 3.3v:     3.30 V
fan rpm:           0 RPM
temp1:           +46.0°C
temp2:           +38.8°C
power total:     88.00 W
power 12v:       72.00 W
power 5v:        14.00 W
power 3.3v:       7.00 W
current 12v:      6.00 A
current 5v:       2.88 A
current 3.3v:     2.19 A
```

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
sudo make dkms-uninstall
sudo modprobe -r corsairpsu
```

## Dev

```bash
make
sudo rmmod corsairpsu; sudo insmod corsairpsu.ko; sensors | grep corsairpsu -A 15
```