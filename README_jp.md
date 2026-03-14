# SAA7160 HDMI Capture Driver for Linux

## 概要

SAA7160 PCIe Bridge 搭載のHDMIキャプチャボード向けのV4L2ドライバです。
本リポジトリは [s-moch/linux-saa716x](https://github.com/s-moch/linux-saa716x)
を基にビデオキャプチャ機能を追加したものです。

テスト環境:

```
Ubuntu 22.04 LTS
Linux 5.15.0-164-generic x86_64
```


## 動作確認済みボード

| Board | 備考                       |
| ----- | ------------------------- |
| SKNET Monstar X3 |    |
| Regia ONE, Regia TWO |    |
| ドリキャプ DC-HC1 |   |
| ドリキャプ DC-HB1 | ExpressCardタイプ  |
| ドリキャプ DC-HD1B | アナログ入力は未対応  |
| サンコー HDMVC4UC |   |


## リポジトリ構成

```
drivers/media/pci/saa716x/
    SAA7160 V4L2 bridge driver implementation

drivers/media/i2c/tda19978.c
    HDMI receiver V4L2 subdevice driver

docs/
    Additional documentation
```


## ビルドとインストール

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


### 動作テスト

List video devices:

```
v4l2-ctl --list-devices
```

Inspect device capabilities:

```
v4l2-ctl --all
```


## キャプチャの実行

Example using FFmpeg:

```
ffmpeg -f v4l2 -i /dev/video0 capture.raw
```


## 不具合・未実装の機能

This driver is still under development and several areas require further work.

The following limitations are currently known:

**HDMI event interrupt handling is not implemented**

The driver currently does not implement interrupt handling for HDMI events such as:

* signal detection
* input format change
* hotplug notifications

**Frame drops may occur at high frame rates**

Under certain conditions (e.g. high resolution or high frame rate capture), frame drops may occur.
Further investigation of the DMA pipeline and buffer management is required.

**Audio capture is not supported**

The current implementation supports **video capture only**.
HDMI audio capture has not yet been implemented.


## TODO

Future work includes:

* HDMI interrupt handling
* Investigation and mitigation of frame drops during high-FPS capture
* HDMI audio capture support
* Additional testing on different kernel versions
* Code cleanup and documentation improvements


## Contributing

Contributions are welcome.

Pull requests addressing the following areas would be especially appreciated:

* HDMI event interrupt handling
* Improvements to DMA performance and buffer management
* HDMI audio capture support
* Code cleanups and documentation improvements

Bug reports, testing results, and hardware information are also highly appreciated.

Please feel free to open an issue or submit a pull request.


## License

This project is licensed under the GNU General Public License version 2.

```
SPDX-License-Identifier: GPL-2.0-only
```

