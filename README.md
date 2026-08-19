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

起動すると115200bpsでHM3301を1秒間隔で計測し、1分ごとに大気環境側のPM1.0、PM2.5、PM10質量濃度の平均値と最大値を表示します。

青色LEDは正常時には消灯します。I²CまたはHM3301の初期化に失敗した場合は点灯し続けます。計測中に読み取りまたはチェックサムの検証に失敗した場合も点灯し、次の正常な計測で消灯します。

## HM3301について

HM3301を次のように接続します。

| NanoC6 | HM3301 |
| --- | --- |
| G1 | SCL |
| G2 | SDA |
| 5V | VCC |
| GND | GND |

I²Cは、HM3301でのフレーム破損を避けるため20kHzで動作させます。シリアルモニターには、一般家庭の空気質を見る際に適した大気環境側のPM1.0、PM2.5、PM10質量濃度（ug/m3）だけを表示します。

センサーの値が安定するまで約30秒かかります。最初の集計結果は起動約1分後に表示されるため、その中にはウォームアップ中の値も含まれます。読み取りやチェックサムの検証に失敗した計測は集計から除外され、`samples`にはその区間で正常に取得できた件数が表示されます。区間内に正常な計測がなかった場合は`no valid samples`と表示されます。

```text
[60125 ms] 1-minute summary (samples=60)
sensor number: 0
mass concentration - atmospheric environment:
  PM1.0: average=9.8 ug/m3, max=12 ug/m3
  PM2.5: average=14.7 ug/m3, max=18 ug/m3
  PM10 : average=17.5 ug/m3, max=21 ug/m3
```

`HM3301 did not respond`と表示された場合は、電源、G1/G2の接続、コネクターの向きを確認してください。

チェックサム不一致が3回続いた場合は、計算したチェックサム、受信したチェックサム、生の29バイトを表示します。継続して発生する場合は、その`raw frame:`行を確認してください。
