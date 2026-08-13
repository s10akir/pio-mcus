# m5-nano-c6-hm3301

M5Stack NanoC6向けのPlatformIO（pioarduino）プロジェクトです。

## 必要なもの

- M5Stack NanoC6
- データ通信対応のUSB Type-Cケーブル
- [mise](https://mise.jdx.dev/)

## セットアップ

このリポジトリでは、miseでpioarduinoを管理し、ダウンロードしたPlatformIOのパッケージをプロジェクト内の`.pioarduino/`に保存します。

```sh
mise install
```

## ビルド

```sh
mise exec -- pio run
```

## 書き込み

NanoC6をUSBで接続して、次を実行します。

```sh
mise exec -- pio run --target upload
```

書き込みを開始できない場合は、GPIO9ボタンを押したままUSBケーブルを接続し、ダウンロードモードに入れてから再実行してください。

## シリアルモニター

```sh
mise exec -- pio device monitor
```

起動すると115200bpsでHM3301の計測値が5秒間隔で表示されます。正常なフレームを受信するたびに青色LEDの状態が切り替わります。

## HM3301について

HM3301を次のように接続します。

| NanoC6 | HM3301 |
| --- | --- |
| G1 | SCL |
| G2 | SDA |
| 5V | VCC |
| GND | GND |

I²Cは、HM3301でのフレーム破損を避けるため20kHzで動作させます。シリアルモニターには次の値を表示します。

- 標準粒子（CF=1）のPM1.0、PM2.5、PM10質量濃度（ug/m3）
- 大気環境のPM1.0、PM2.5、PM10質量濃度（ug/m3）
- 0.3、0.5、1.0、2.5、5、10um以上の粒子数（個/L）

一般家庭の空気質を見る場合は大気環境側の値を主に使います。粒径別の粒子数は、煙などの微粒子が多いのか、舞い上がったほこりなどの比較的大きな粒子が多いのかを判断する補助値です。粒子数は各粒径帯の個数ではなく、表示粒径以上の累積個数です。

センサーの値が安定するまで約30秒かかるため、それまでは時刻の横に`(warming up)`と表示されます。

```text
[30125 ms]
sensor number: 0
mass concentration - standard particulate matter (CF=1):
  PM1.0 : 12 ug/m3
  PM2.5 : 18 ug/m3
  PM10  : 21 ug/m3
mass concentration - atmospheric environment:
  PM1.0 : 10 ug/m3
  PM2.5 : 15 ug/m3
  PM10  : 18 ug/m3
particle count by minimum diameter:
  >= 0.3 um : 120 /L
  >= 0.5 um : 80 /L
  >= 1.0 um : 20 /L
  >= 2.5 um : 5 /L
  >= 5.0 um : 1 /L
  >= 10  um : 0 /L
```

`HM3301 did not respond`と表示された場合は、電源、G1/G2の接続、コネクターの向きを確認してください。

チェックサム不一致が3回続いた場合は、計算したチェックサム、受信したチェックサム、生の29バイトを表示します。継続して発生する場合は、その`raw frame:`行を確認してください。
