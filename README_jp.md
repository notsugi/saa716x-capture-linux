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

| Board | 備考 |
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


### デバイスの確認

List video devices:

```
v4l2-ctl --list-devices
```

Inspect device capabilities:

```
v4l2-ctl --all
```


## キャプチャの実行
アプリケーションにキャプチャデータを正常に渡すために、入力信号のタイミングを元にボードを手動で設定する必要があります。

```
v4l2-ctl -d <X> --query-dv-timings
v4l2-ctl -d <X> --set-dv-bt-timings index=<N>
```

解像度やリフレッシュレートの変更が生じる度に行ってください。

アプリケーションにキャプチャデータを渡す準備ができたので、ffmpeg等でデバイスファイルを指定してあげればキャプチャを開始できます。

```
ffmpeg -f v4l2 -i /dev/video<X> capture.raw
```

### 補足
* GUIアプリについては`qv4l2`でキャプチャできることを確認しています。
* キャプチャ映像に水平方向の細いノイズが入る場合、おそらくCPUの省電力機能が影響しています。`cpupower`コマンドでC3,C6 stateを無効にしてみてください。

## 不具合・未実装の機能

本ドライバは開発中のため、不具合や未実装の機能があります。


**HDMIイベントの割り込み処理が不完全**

データシートが入手出来ないため、tda19978の割り込み処理は未実装です。

**まれにフレーム落ちが発生する**

特定のアプリケーションと高フレームレート入力の組み合わせでコマ落ちが発生することがあります。DMAエンジンとvideobuf2フレームワークの間のバッファのやり取りに改善可能な点があるかもしれません。

**音声キャプチャ機能がない**

今のところサポートしているのは動画のキャプチャのみです。


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

