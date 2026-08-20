# m5stack-nanoc6-sgp30-env4

M5Stack NanoC6、Unit Mini TVOC/eCO2（SGP30）、ENV.IVによる室内環境計測プロジェクトです。温度、相対湿度、気圧、TVOC、eCO2を1分単位で集計し、VictoriaMetricsへ送信します。

## 配線

NanoC6にはGrove端子が1つしかないため、アドレス変換を行わないM5Stack Unit Hubで分岐します。

```text
NanoC6
  └─ Unit Hub
       ├─ Unit Mini TVOC/eCO2 (SGP30, 0x58)
       └─ ENV.IV
            ├─ SHT40  (0x44)
            └─ BMP280 (0x76)
```

## セットアップ

```sh
cd projects/m5stack-nanoc6-sgp30-env4
cp ../../include/secrets.example.h ../../include/secrets.h
mise exec -- pio run
mise exec -- pio run --target upload
mise exec -- pio device monitor
```

`../../include/secrets.h`に全プロジェクト共通のWi-Fi、VictoriaMetricsの完全な書き込みURL、Bearer token、NTPサーバーを設定してください。このファイルはGit管理外です。

## 計測動作

SGP30は専用タスクで1秒周期を維持し、ENV.IVの温度・相対湿度から計算した絶対湿度を1分ごとにSGP30へ設定します。起動後15秒間のSGP30固定値はライブラリ側で除外されます。

有効なベースラインがない初回起動では12時間連続運転した後、約1時間ごとにベースラインをNVSへ保存します。保存時刻から7日以内かつ同じSGP30の値だけを起動時に復元します。NTP時刻が取得できない場合は古さを判定できないため復元・保存しません。

1分ごとに各値の平均値と最大値をPrometheusテキスト形式でPOSTします。読み取りに失敗した種類だけをその区間のペイロードから除外します。

## メトリクス

- `environment_temperature_celsius_{avg,max}`
- `environment_relative_humidity_percent_{avg,max}`
- `environment_pressure_pascals_{avg,max}`
- `environment_tvoc_parts_per_billion_{avg,max}`
- `environment_eco2_parts_per_million_{avg,max}`

すべての系列に`site="home"`、`location="working-room"`、`device="m5stack-nanoc6-02"`を付けます。ENV.IV由来の系列は`source="env4"`、SGP30由来の系列は`source="sgp30"`です。

eCO2はVOC/H2から推定したCO2換算値であり、実際のCO2濃度ではありません。

ホスト上のペイロード・絶対湿度計算テストは次で実行できます。

```sh
c++ -std=c++11 -Iinclude tests/test_metrics.cpp -o /tmp/test_sgp30_env4_metrics && /tmp/test_sgp30_env4_metrics
```
