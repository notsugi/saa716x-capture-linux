# SAA7160 HDMI Capture Driver for Linux

## Overview

This project provides a V4L2 capture driver for HDMI capture boards based on the **NXP SAA7160** PCIe bridge. This is an updated version of the existing saa716x-dvb driver (https://github.com/s-moch/linux-saa716x) with added v4l2 capture functionality.

The project also includes a V4L2 subdevice driver for HDMI receivers based on the **TDA19977 / TDA19978** family.

Tested kernel version:

```
Linux 5.15.164 x86_64
```

Other kernel versions may require minor adjustments due to Linux media subsystem API changes.

---

# Supported Hardware

## PCI Bridge

```
NXP SAA7160
```

## HDMI Receivers

Two HDMI receiver implementations are supported.

| Receiver | Driver                                 | Notes                               |
| -------- | -------------------------------------- | ----------------------------------- |
| ADV7611  | adv7604.c (upstream kernel driver)       | Uses existing kernel V4L2 subdevice |
| TDA19977 | tda19978.c (included in this repository) | Custom V4L2 subdevice driver        |
| TDA19978 | tda19978.c (included in this repository) | Custom V4L2 subdevice driver        |

---

# Repository Structure

```
drivers/media/pci/saa716x/
    SAA7160 V4L2 bridge driver implementation

drivers/media/i2c/tda19978.c
    HDMI receiver V4L2 subdevice driver

docs/
    Additional documentation
```

---

# Building the Driver

The driver can be built as an external kernel module.

```
make
```

Load the module:

```
sudo insmod saa716x_core.ko saa716x_capture.ko
```

Unload the module:

```
sudo rmmod saa716x_capture saa716x_core
```

---

# Verifying Device Detection

List video devices:

```
v4l2-ctl --list-devices
```

Inspect device capabilities:

```
v4l2-ctl --all
```

---

# Capture Example

Query current source timing info and configure format first.
`<N>` is selected from `--list-dv-timings` output which matched result of the query.

```
v4l2-ctl -d <X> --query-dv-timings
v4l2-ctl -d <X> --set-dv-bt-timings index=<N>
```

Then the device is ready to stream video data to applications.

```
ffmpeg -f v4l2 -i /dev/video<X> capture.raw
```

---

# Known Limitations

This driver is still under development and several areas require further work.

The following limitations are currently known:

**Interlaced video captureing is not supported**

**HDMI event interrupt handling is implemented partially**

The driver currently does not implement interrupt handling for TDA19978 due to lack of datasheet.

**Frame drops may occur at high frame rates**

Under certain conditions (e.g. high resolution or high frame rate capture), frame drops may occur.
Further investigation of the DMA pipeline and buffer management is required.

**Audio capture is not supported**

The current implementation supports **video capture only**.
HDMI audio capture has not yet been implemented.

---

# TODO

Future work includes:

* Interlaced video support
* TDA19978 interrupt handling
* Investigation and mitigation of frame drops during high-FPS capture
* HDMI audio capture support
* Additional testing on different kernel versions
* Code cleanup and documentation improvements

---

# Contributing

Contributions are welcome.

Pull requests addressing the following areas would be especially appreciated:

* HDMI event interrupt handling
* Improvements to DMA performance and buffer management
* HDMI audio capture support
* Code cleanups and documentation improvements

Bug reports, testing results, and hardware information are also highly appreciated.

Please feel free to open an issue or submit a pull request.

---

# License

This project is licensed under the GNU General Public License version 2.

```
SPDX-License-Identifier: GPL-2.0-only
```

