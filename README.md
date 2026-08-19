# pio-mcus

pioarduinoを使う複数のMCUプロジェクトを管理するリポジトリです。

## 必要なもの

- [mise](https://mise.jdx.dev/)

```sh
mise install
```

## プロジェクト

| プロジェクト | 説明 |
| --- | --- |
| [m5stack-nanoc6-hm3301](projects/m5stack-nanoc6-hm3301/) | M5Stack NanoC6とHM3301による大気環境PM値の計測・送信 |

対象プロジェクトはPlatformIO標準の`--project-dir`で指定します。

```sh
mise exec -- pio run --project-dir projects/m5stack-nanoc6-hm3301
mise exec -- pio run --project-dir projects/m5stack-nanoc6-hm3301 --target upload
mise exec -- pio device monitor --project-dir projects/m5stack-nanoc6-hm3301
```

新しいプロジェクトは`projects/<name>/`に独立した`platformio.ini`、`src/`、`include/`を置きます。

複数プロジェクトでコードを共有する必要が生じた場合は、manifest付きライブラリを`shared/<library>/`に置き、利用するプロジェクトの`lib_deps`から明示的に参照します。

```ini
lib_deps =
    SharedLibrary=symlink://../../shared/SharedLibrary
```
