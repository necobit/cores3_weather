# cores3_weather

M5Stack CoreS3 で Yahoo! 天気 雨雲レーダー API を使い、今後30分の降水予報を表示・音声アラートするアプリです。

## 機能

- Yahoo! 天気 API から現在の降水量と今後30分の予報を取得
- 状態に応じて画面の色と内容を切り替え
  - 青: 雨の心配なし
  - オレンジ: もうすぐ雨（傘の準備を促す）
  - 赤: 現在雨が降っている
  - グレー: 通信エラー
- 雨アラート時はスピーカーで3音ビープ
- 5分ごとに自動更新 / BtnA で手動更新

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
   | サイトURL | `http://localhost/` |
   | ガイドラインへの同意 | チェック |

3. 登録後に表示される **Client ID（アプリケーション ID）** をコピーしておく

### 1-3. 天気 API の有効化確認

- 「Web API」欄に「気象情報API(ウェザーAPI)」が表示されていれば利用可能
- 無料枠: 1日50,000リクエスト（個人利用で十分）

---

## 2. PlatformIO プロジェクトのセットアップ

### 2-1. 前提

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation.html) or PlatformIO IDE (VS Code 拡張) がインストール済みであること

### 2-2. クローン

```bash
git clone https://github.com/necobit/cores3_weather.git
cd cores3_weather
```

### 2-3. `src/config.h` の編集

```cpp
// ---- WiFi ---------------------------------------------------------------
#define WIFI_SSID   "自宅のSSID"
#define WIFI_PASS   "WiFiパスワード"

// ---- Yahoo Weather API --------------------------------------------------
#define YAHOO_APPID "取得したClient ID（アプリケーションID）"

// ---- 位置情報 (経度, 緯度) -----------------------------------------------
// Google Maps で確認するか、下記の例を参考に書き換える
// 例: 東京駅 → LON=139.7671, LAT=35.6812
#define LOCATION_LON  139.6503   // 経度
#define LOCATION_LAT   35.6762   // 緯度

// ---- 動作設定 ------------------------------------------------------------
#define CHECK_INTERVAL_MS  (5UL * 60 * 1000)  // 更新間隔 (ms)
#define RAIN_ALERT_THRESHOLD  1.0f             // アラート閾値 (mm/h)
#define FORECAST_MINUTES  30                   // 何分先まで予報を見るか
```

> **位置情報の調べ方**: Google マップで自宅を右クリック → 緯度/経度が表示される

---

## 3. ビルドと書き込み

```bash
# ビルドのみ確認
pio run

# CoreS3 を USB 接続してビルド＆書き込み
pio run --target upload

# シリアルモニタで動作確認
pio device monitor
```

---

## 4. 操作方法

| 操作 | 動作 |
|------|------|
| 起動 | WiFi 接続 → 即座に天気取得 |
| BtnA (画面左下) | 手動で天気を再取得 |
| 自動更新 | 5分ごとに天気取得（`CHECK_INTERVAL_MS` で変更可） |

---

## 5. API レスポンス仕様メモ

Yahoo! 天気 API エンドポイント:
```
GET https://map.yahooapis.jp/weather/V1/place
    ?coordinates=<経度>,<緯度>
    &output=json&past=0&interval=10
    &appid=<CLIENT_ID>
```

- `interval=10`: 10分ごとのデータ
- `past=0`: 過去データなし（現在＋予報のみ）
- `Type=observation`: 現在の観測値
- `Type=forecast`: 予報値（10分後、20分後、…）
- `Rainfall`: 降水強度 (mm/h)

---

## 6. ライブラリ依存

| ライブラリ | バージョン |
|-----------|-----------|
| M5Unified | ^0.2.4 |
| ArduinoJson | ^7.3.1 |
