/*
 * cores3_weather — Yahoo天気雨雲レーダー通知アプリ
 * ハードウェア: M5Stack CoreS3
 *
 * 機能:
 *   - Yahoo天気APIから今後30分の降水量予報を取得
 *   - 雨が降りそうなら画面に警告表示 + スピーカーでビープ音
 *   - 晴れ/曇りなら画面に現在状況を表示
 *   - 5分ごとに自動更新 / BtnA で手動更新
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

// ---------------------------------------------------------------------------
// 型定義
// ---------------------------------------------------------------------------
enum WeatherState { STATE_INIT, STATE_CLEAR, STATE_RAIN_SOON, STATE_RAINING, STATE_ERROR };

// ---------------------------------------------------------------------------
// グローバル変数
// ---------------------------------------------------------------------------
static WeatherState  g_state           = STATE_INIT;
static float         g_maxForecast     = 0.0f;
static float         g_currentRainfall = 0.0f;
static String        g_lastUpdated     = "--:--";
static unsigned long g_lastCheck       = 0;
static bool          g_wifiOk          = false;

// ---------------------------------------------------------------------------
// プロトタイプ
// ---------------------------------------------------------------------------
bool connectWiFi();
bool fetchWeather(float &outCurrent, float &outMaxForecast);
void drawScreen();
void playAlert();
void playOk();

// ---------------------------------------------------------------------------
// setup
// ---------------------------------------------------------------------------
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // 日本語フォント有効化
    M5.Display.setFont(&fonts::lgfxJapanGothicP_20);
    M5.Display.setTextSize(1);
    M5.Display.setRotation(1);    // 横向き (320x240)
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setCursor(8, 8);
    M5.Display.print("WiFi 接続中...");

    g_wifiOk = connectWiFi();
    g_lastCheck = millis() - CHECK_INTERVAL_MS;   // 即時チェックトリガー
}

// ---------------------------------------------------------------------------
// loop
// ---------------------------------------------------------------------------
void loop() {
    M5.update();

    // BtnA: 手動更新
    if (M5.BtnA.wasPressed()) {
        g_lastCheck = 0;
    }

    // 定期チェック
    if ((unsigned long)(millis() - g_lastCheck) >= CHECK_INTERVAL_MS) {
        g_lastCheck = millis();

        if (!g_wifiOk) g_wifiOk = connectWiFi();

        if (g_wifiOk) {
            bool ok = fetchWeather(g_currentRainfall, g_maxForecast);
            if (!ok) {
                g_state = STATE_ERROR;
            } else if (g_currentRainfall >= RAIN_ALERT_THRESHOLD) {
                g_state = STATE_RAINING;
                playAlert();
            } else if (g_maxForecast >= RAIN_ALERT_THRESHOLD) {
                g_state = STATE_RAIN_SOON;
                playAlert();
            } else {
                g_state = STATE_CLEAR;
                playOk();
            }

            // 更新時刻 (NTP)
            struct tm ti;
            if (getLocalTime(&ti)) {
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
                g_lastUpdated = buf;
            }
        }

        drawScreen();
    }
}

// ---------------------------------------------------------------------------
// WiFi接続
// ---------------------------------------------------------------------------
bool connectWiFi() {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for (int i = 0; i < 20 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
    }
    if (WiFi.status() != WL_CONNECTED) return false;
    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    return true;
}

// ---------------------------------------------------------------------------
// Yahoo天気API呼び出し
// ---------------------------------------------------------------------------
bool fetchWeather(float &outCurrent, float &outMaxForecast) {
    char url[256];
    snprintf(url, sizeof(url),
        "https://map.yahooapis.jp/weather/V1/place"
        "?coordinates=%.4f,%.4f"
        "&output=json&past=0&interval=10"
        "&appid=%s",
        LOCATION_LON, LOCATION_LAT, YAHOO_APPID);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);

    int code = http.GET();
    if (code != 200) {
        Serial.printf("[Weather] HTTP %d\n", code);
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, body)) return false;

    JsonArray arr = doc["Feature"][0]["Property"]["WeatherList"]["Weather"].as<JsonArray>();
    if (arr.isNull()) return false;

    outCurrent     = 0.0f;
    outMaxForecast = 0.0f;
    int forecastCount = 0;
    const int forecastLimit = FORECAST_MINUTES / 10;

    for (JsonObject w : arr) {
        const char* type    = w["Type"];
        float       rain    = w["Rainfall"].as<float>();
        Serial.printf("  [%s] %s %.1f mm/h\n",
            type, w["Date"].as<const char*>(), rain);

        if (strcmp(type, "observation") == 0) {
            outCurrent = rain;
        } else if (strcmp(type, "forecast") == 0 && forecastCount < forecastLimit) {
            if (rain > outMaxForecast) outMaxForecast = rain;
            forecastCount++;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 画面描画
// ---------------------------------------------------------------------------
void drawScreen() {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setFont(&fonts::lgfxJapanGothicP_20);
    M5.Display.setTextSize(1);

    switch (g_state) {

    case STATE_CLEAR:
        M5.Display.fillScreen(0x0014);   // 深青
        M5.Display.setTextColor(TFT_WHITE, 0x0014);
        M5.Display.setFont(&fonts::lgfxJapanGothicP_28);
        M5.Display.setCursor(20, 30);
        M5.Display.print("  雨の心配はありません");
        M5.Display.setFont(&fonts::lgfxJapanGothicP_20);
        M5.Display.setCursor(20, 100);
        M5.Display.printf("  現在: %.1f mm/h", g_currentRainfall);
        M5.Display.setCursor(20, 135);
        M5.Display.printf("  %d分先最大: %.1f mm/h", FORECAST_MINUTES, g_maxForecast);
        break;

    case STATE_RAIN_SOON:
        M5.Display.fillScreen(0xFD20);   // オレンジ
        M5.Display.setTextColor(TFT_BLACK, 0xFD20);
        M5.Display.setFont(&fonts::lgfxJapanGothicP_28);
        M5.Display.setCursor(20, 20);
        M5.Display.print("  もうすぐ雨が来ます！");
        M5.Display.setFont(&fonts::lgfxJapanGothicP_20);
        M5.Display.setCursor(20, 80);
        M5.Display.print("  傘の準備をしましょう");
        M5.Display.setCursor(20, 120);
        M5.Display.printf("  現在: %.1f mm/h", g_currentRainfall);
        M5.Display.setCursor(20, 155);
        M5.Display.printf("  %d分先最大: %.1f mm/h", FORECAST_MINUTES, g_maxForecast);
        break;

    case STATE_RAINING:
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.setFont(&fonts::lgfxJapanGothicP_28);
        M5.Display.setCursor(20, 20);
        M5.Display.print("  現在 雨が降っています！");
        M5.Display.setFont(&fonts::lgfxJapanGothicP_20);
        M5.Display.setCursor(20, 80);
        M5.Display.printf("  現在: %.1f mm/h", g_currentRainfall);
        M5.Display.setCursor(20, 115);
        M5.Display.printf("  %d分先最大: %.1f mm/h", FORECAST_MINUTES, g_maxForecast);
        break;

    case STATE_ERROR:
        M5.Display.fillScreen(TFT_DARKGREY);
        M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREY);
        M5.Display.setFont(&fonts::lgfxJapanGothicP_20);
        M5.Display.setCursor(20, 80);
        M5.Display.print("  通信エラー");
        M5.Display.setCursor(20, 115);
        M5.Display.print("  WiFiまたはAPIキーを確認");
        break;

    default:   // STATE_INIT
        M5.Display.setCursor(20, 100);
        M5.Display.print("  初期化中...");
        break;
    }

    // フッター (更新時刻 + 操作ヒント)
    M5.Display.setFont(&fonts::lgfxJapanGothicP_16);
    M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.fillRect(0, 215, 320, 25, TFT_BLACK);
    M5.Display.setCursor(4, 218);
    M5.Display.printf("更新: %s  [BtnA:手動更新]", g_lastUpdated.c_str());
}

// ---------------------------------------------------------------------------
// 音: アラート (雨が来る)
// ---------------------------------------------------------------------------
void playAlert() {
    const int freqs[] = {440, 550, 660};
    for (int f : freqs) {
        M5.Speaker.tone(f, 200);
        delay(250);
    }
    M5.Speaker.stop();
}

// ---------------------------------------------------------------------------
// 音: OK (雨なし)
// ---------------------------------------------------------------------------
void playOk() {
    M5.Speaker.tone(880, 100);
    delay(150);
    M5.Speaker.stop();
}
