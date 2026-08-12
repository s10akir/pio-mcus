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

起動すると115200bpsで`M5Stack NanoC6 is ready.`と表示され、青色LEDが1秒間隔で点滅します。

## HM3301について

現在はNanoC6単体のビルド・書き込み確認用コードのみです。HM3301の接続と測定処理は未実装です。
