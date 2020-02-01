# CorsairPSU

CorsairPSU is a simple hwmon Linux kernel module for Corsair RMi and HXi Series PSUs to view power usage, and more info about your PSU.

```bash
$ sensors
... TODO
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
