#pragma once

// ---- WiFi ---------------------------------------------------------------
#define WIFI_SSID   "YOUR_SSID"
#define WIFI_PASS   "YOUR_PASSWORD"

// ---- Yahoo Weather API --------------------------------------------------
// 取得方法は README.md 参照
#define YAHOO_APPID "YOUR_YAHOO_CLIENT_ID"

// ---- 位置情報 (経度, 緯度) -----------------------------------------------
// デフォルト: 東京
#define LOCATION_LON  139.6503
#define LOCATION_LAT   35.6762

// ---- 動作設定 ------------------------------------------------------------
// チェック間隔 (ms) — デフォルト 5分
#define CHECK_INTERVAL_MS  (5UL * 60 * 1000)

// 雨量閾値 (mm/h): これ以上の予報が出たらアラート
#define RAIN_ALERT_THRESHOLD  1.0f

// 何分先までを「もうすぐ雨」と判断するか (10分単位、最大60分)
#define FORECAST_MINUTES  30
