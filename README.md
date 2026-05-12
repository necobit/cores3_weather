# cores3_weather

M5Stack CoreS3 で Yahoo! 天気 雨雲レーダー API と livedoor 互換天気予報 API を組み合わせ、雨予報を **ロボットキャラ表示** と **音** で知らせるアプリです。

## 機能

- **雨雲レーダー**: Yahoo! 天気 API から現在の降水量と今後 30 分の予報を取得し、状態を判定
- **天気予報**: weather.tsukumijima.net (livedoor 互換) から今日 / 明日の天気と最高 / 最低気温を取得し画面右上に表示
- **ROBOT キャラ表示**: [KSstackchan](https://github.com/necobit/KSstackchan) の ROBOT スキンを M5Canvas プリミティブ描画で移植。常時呼吸アニメ + 数秒おきに歩行アニメで「ふわふわ」動く
- **状態 → 背景色 + 目の色 + 音**

  | 状態 | 背景 | 目の色 | 音 |
  |------|------|--------|------|
  | 雨の心配なし | 黒 | 赤 | 無音 |
  | もうすぐ雨 | 灰 | 黄 | 1760 Hz (A6) × 3 |
  | 雨が降っている | 青 | 灰 | 1568 Hz (G6) × 1 |
  | 通信エラー | 暗灰 | 赤 | 無音 |

  音は状態が遷移した瞬間のみ鳴ります。

- **音量スライダー**: 画面下のバーを指でなぞって 0〜100 で調整 (タップでは変化しない)。値は NVS に永続化されます
- **左上ミュートトグル**: スピーカーアイコンをタップで全音オフ / オン
- **デモモード**: 画面の任意の場所 (スライダー除く) を 5 秒長押しで、3 状態 (晴 → 直前 → 雨) を 5 秒ずつ繰り返すデモに切替。もう一度長押しで通常モードに戻ります
- **下部テロップ**: 更新時刻 / 現在の降水量 / 30 分先最大 / 状態を左方向にスクロール表示
- **5 分ごとの自動更新** + BtnA で手動更新

---

## 1. Yahoo! デベロッパーネットワーク APP ID の取得

### 1-1. アカウント作成

1. [Yahoo! JAPAN デベロッパーネットワーク](https://e.developer.yahoo.co.jp/register) にアクセス
2. Yahoo! JAPAN ID でログイン（なければ無料で作成）

### 1-2. アプリ登録

1. 「新しいアプリケーションを開発」をクリック
2. 以下を入力して「確認」→「登録」

   | 項目 | 入力例 |
   |------|--------|
   | アプリケーション名 | CoreS3 Weather |
   | アプリケーションの種類 | サーバーサイド（JavaScript） |
   | サイトURL | 自分のGitHubリポジトリURLなど (`http://localhost/`は審査落ちすることがある) |
   | ガイドラインへの同意 | チェック |

3. 登録後に表示される **Client ID（アプリケーション ID）** をコピー

### 1-3. 天気 API の有効化

- 「Web API」欄に「気象情報API（ウェザーAPI）」が表示されていれば利用可能
- 無料枠: 1 日 50,000 リクエスト（個人利用で十分）

---

## 2. プロジェクトのセットアップ

### 2-1. 前提

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html) または PlatformIO IDE (VS Code 拡張) がインストール済み

### 2-2. クローン

```bash
git clone https://github.com/necobit/cores3_weather.git
cd cores3_weather
```

### 2-3. `src/config.h` の作成

リポジトリにはサンプル `src/config.h.example` だけが入っており、実値が入った `src/config.h` は `.gitignore` で除外されています。コピーして自分の値で埋めてください:

```bash
cp src/config.h.example src/config.h
```

```cpp
// ---- WiFi ---------------------------------------------------------------
#define WIFI_SSID   "自宅のSSID"
#define WIFI_PASS   "WiFiパスワード"

// ---- Yahoo Weather API --------------------------------------------------
#define YAHOO_APPID "取得した Client ID (アプリケーション ID)"

// ---- 位置情報 (経度, 緯度) -----------------------------------------------
// Google Maps で右クリック → 緯度/経度をコピー
#define LOCATION_LON 139.969809
#define LOCATION_LAT 35.825754

// ---- 天気予報 (livedoor互換 API / weather.tsukumijima.net) ---------------
// 都市コード一覧: https://weather.tsukumijima.net/primary_area.xml
// 例: 東京 130010 / 千葉 120010 / 横浜 140010 / 大阪 270000
#define CITY_CODE "120010"

// ---- 動作設定 -----------------------------------------------------------
#define CHECK_INTERVAL_MS    (5UL * 60 * 1000)  // 更新間隔 (ms)
#define RAIN_ALERT_THRESHOLD 1.0f                // アラート閾値 (mm/h)
#define FORECAST_MINUTES     30                  // 何分先まで予報を見るか
```

---

## 3. ビルドと書き込み

```bash
pio run                 # ビルド
pio run -t upload       # ビルド + 書き込み
```

シリアルモニタを使う場合 (本リポジトリの `platformio.ini` は ESP32-S3 用 USB CDC 設定済み):

```bash
pio device monitor
```

---

## 4. 操作

| 操作 | 動作 |
|------|------|
| 起動 | WiFi 接続 → 即座に天気取得 |
| BtnA (画面左下の物理ボタン) | 手動で天気を再取得 |
| 自動更新 | 5 分ごと (`CHECK_INTERVAL_MS` で変更可) |
| 画面下のスライドバーを **指でなぞる** | 音量を 0〜100 で調整 (タップでは無効、ドラッグで連続変化) |
| 左上スピーカーアイコンをタップ | ミュート / ミュート解除 |
| 画面 (スライダー外) を **5 秒長押し** | デモモード切替 (3 状態をループ) |

---

## 5. ファイル構成

```
src/
  main.cpp           オーケストレーション、API 取得、Canvas レンダ、入力処理
  robot.h / .cpp     ROBOT キャラの描画・呼吸・歩行アニメ
  config.h           プライベート設定 (gitignore 対象)
  config.h.example   設定テンプレート
platformio.ini       PlatformIO 設定 (ESP32-S3 USB CDC, monitor_rts/dtr=0)
```

---

## 6. API メモ

### Yahoo! 天気 (雨雲レーダー)

```
GET https://map.yahooapis.jp/weather/V1/place
    ?coordinates=<経度>,<緯度>
    &output=json&past=0&interval=10
    &appid=<CLIENT_ID>
```

- `Type=observation`: 現在の観測値
- `Type=forecast`: 10 分後 / 20 分後 / … の予報値
- `Rainfall`: 降水強度 (mm/h)

### weather.tsukumijima.net (天気予報、livedoor 互換)

```
GET https://weather.tsukumijima.net/api/forecast?city=<CITY_CODE>
```

- `forecasts[0]` = 今日 / `forecasts[1]` = 明日 / `forecasts[2]` = 明後日
- `telop`: 「晴れ」「曇り」「雨」など
- `temperature.max.celsius` / `min.celsius`: 最高 / 最低気温

---

## 7. ライブラリ依存

| ライブラリ | バージョン |
|-----------|-----------|
| M5Unified | ^0.2.4 |
| ArduinoJson | ^7.3.1 |

---

## 8. 謝辞

ROBOT スキンのデザインとアニメーションロジックは [KSstackchan](https://github.com/necobit/KSstackchan) のものを参考にしています。
