# m5stack-nanoc6-hm3301

M5Stack NanoC6向けのPlatformIO（pioarduino）プロジェクトです。

## 必要なもの

- M5Stack NanoC6
- データ通信対応のUSB Type-Cケーブル
- [mise](https://mise.jdx.dev/)

## セットアップ

このリポジトリでは、miseでpioarduinoを管理し、ダウンロードしたPlatformIOのパッケージをリポジトリルートの`.pioarduino/`に保存します。

リポジトリルートからプロジェクトディレクトリへ移動して実行します。

```sh
cd projects/m5stack-nanoc6-hm3301
mise install
cp ../../include/secrets.example.h ../../include/secrets.h
```

`../../include/secrets.h`に全プロジェクト共通のWi-FiのSSIDとパスワード、VictoriaMetricsの完全な書き込みURL、Bearer token、NTPサーバーを設定してください。書き込みURLは`https://.../api/v1/import/prometheus`まで含めます。

`../../include/secrets.h`はGit管理外です。秘密情報を`secrets.example.h`やその他の追跡対象ファイルへ書き込まないでください。

## ビルド

```sh
mise exec -- pio run
```

ペイロード生成のホストテストは次で実行できます。

```sh
c++ -std=c++11 -Iinclude tests/test_metrics.cpp -o /tmp/test_metrics && /tmp/test_metrics
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

起動すると115200bpsでHM3301を1秒間隔で計測し、1分ごとに大気環境側のPM1.0、PM2.5、PM10質量濃度の平均値と最大値を表示してVictoriaMetricsへ送信します。Wi-Fiは計測と並行して接続し、切断中は10秒間隔で再接続します。

青色LEDは正常時には消灯します。センサー異常または通信異常がある間は点灯します。センサー異常は次の正常な計測、通信異常は次のPOST成功で解除されます。

## VictoriaMetricsへ送るメトリクス

Prometheusテキスト形式で、タイムスタンプを省略して送ります。すべての系列に`site="home"`、`location="working-room"`、`device="m5stack-nanoc6-01"`、`source="hm3301"`を付けます。

送信するメトリクスは次の6種類です。

- `environment_pm1_avg_micrograms_per_cubic_meter`
- `environment_pm1_max_micrograms_per_cubic_meter`
- `environment_pm2_5_avg_micrograms_per_cubic_meter`
- `environment_pm2_5_max_micrograms_per_cubic_meter`
- `environment_pm10_avg_micrograms_per_cubic_meter`
- `environment_pm10_max_micrograms_per_cubic_meter`

有効サンプルが0件の区間はPOSTしません。HTTP 2xx以外、Wi-Fi切断、NTP未同期、TLSエラーでは送信失敗としてSerialへ記録し、その区間の集計値は再送せず破棄します。

TLS通信は暗号化されますが、サーバー証明書は検証しません。Bearer tokenの窃取やメトリクス改ざんを防ぐ必要が生じた場合は、ルートCA検証を有効にしてください。

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
