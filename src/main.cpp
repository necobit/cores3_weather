/*
 * cores3_weather — Yahoo天気雨雲レーダー通知 + ROBOT UI
 */

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "config.h"
#include "robot.h"

enum WeatherState { STATE_INIT, STATE_CLEAR, STATE_RAIN_SOON, STATE_RAINING, STATE_ERROR };

// 色 (RGB565)
static constexpr uint16_t COLOR_WHITE = 0xFFFF;
static constexpr uint16_t COLOR_GRAY  = 0x8410; // mid gray
static constexpr uint16_t COLOR_BLUE  = 0x021F; // deep blue
static constexpr uint16_t COLOR_DARK  = 0x4208; // dark gray
static constexpr uint16_t COLOR_BLACK = 0x0000;

// スライダー領域
static constexpr int SLIDER_X = 10;
static constexpr int SLIDER_Y = 192;
static constexpr int SLIDER_W = 300;
static constexpr int SLIDER_H = 20;

// ミュートボタン (左上)
static constexpr int MUTE_X = 0;
static constexpr int MUTE_Y = 0;
static constexpr int MUTE_W = 40;
static constexpr int MUTE_H = 40;

// 予報表示 (右上)
static constexpr int FCST_X = 200;
static constexpr int FCST_Y = 2;
static constexpr int FCST_W = 120;

// テロップ領域
static constexpr int TELOP_Y = 215;
static constexpr int TELOP_H = 25;

// グローバル
static WeatherState g_state = STATE_INIT;
static WeatherState g_prev_state = STATE_INIT;
static float        g_current = 0.0f;
static float        g_max_forecast = 0.0f;
static String       g_last_updated = "--:--";
static unsigned long g_last_check = 0;
static bool         g_wifi_ok = false;
static uint8_t      g_volume = 50;        // 0-100
static bool         g_dragging = false;
static bool         g_dragged_this_press = false;
static bool         g_muted = false;
static Preferences  g_prefs;

// 天気予報 (今日/明日)
struct DayForecast {
    bool   valid = false;
    String telop = "";       // "晴れ", "曇り", "雨" など
    int    t_max = -127;     // °C, -127 = N/A
    int    t_min = -127;
};
static DayForecast g_forecast[2];

static bool         g_press_in_mute = false;

// デモモード
static bool         g_demo_mode = false;
static uint32_t     g_demo_state_started = 0;
static int          g_demo_state_idx = 0;
static constexpr uint32_t DEMO_STATE_MS = 5000;
static const WeatherState DEMO_SEQ[3] = {STATE_CLEAR, STATE_RAIN_SOON, STATE_RAINING};

// 長押し検出
static uint32_t     g_press_start_ms = 0;
static bool         g_press_in_slider = false;
static bool         g_long_press_fired = false;
static constexpr uint32_t LONG_PRESS_MS = 5000;

static M5Canvas g_canvas(&M5.Display);
static M5Canvas g_telop_sprite(&M5.Display);
static Robot    g_robot;

// テロップ
static int    g_telop_pixel_w = 0;
static int    g_telop_offset = 0;
static uint32_t g_telop_last_ms = 0;
static String g_telop_text = "";

// プロトタイプ
static bool connectWiFi();
static bool fetchWeather(float& outCurrent, float& outMaxForecast);
static bool fetchForecast();
static void playBeep(WeatherState s);
static void rebuildTelopSprite();
static void handleTouch();
static void renderFrame();
static void drawSpeakerIcon(M5Canvas& cv, int x, int y, bool muted);
static void drawTopForecast(M5Canvas& cv);
static void applyVolume();
static uint16_t bgForState(WeatherState s);

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(COLOR_BLACK);

    // PSRAM 確保
    g_canvas.setPsram(true);
    g_canvas.setColorDepth(16);
    g_canvas.createSprite(320, 240);

    g_telop_sprite.setPsram(true);
    g_telop_sprite.setColorDepth(16);
    // テロップは長め(後で文字列に応じて再生成)
    g_telop_sprite.createSprite(960, TELOP_H);

    // NVS から音量読み出し
    g_prefs.begin("cores3wx", false);
    g_volume = g_prefs.getUChar("vol", 50);
    applyVolume();

    Serial.printf("[Boot] volume=%u\n", g_volume);

    // 初期メッセージ表示
    g_canvas.fillSprite(COLOR_BLACK);
    g_canvas.setFont(&fonts::lgfxJapanGothicP_20);
    g_canvas.setTextColor(COLOR_WHITE, COLOR_BLACK);
    g_canvas.setCursor(8, 8);
    g_canvas.print("WiFi 接続中...");
    g_canvas.pushSprite(0, 0);

    g_wifi_ok = connectWiFi();
    g_last_check = millis() - CHECK_INTERVAL_MS;  // 即時取得

    g_telop_text = "起動中...";
    rebuildTelopSprite();
}

void loop() {
    M5.update();
    uint32_t now = millis();

    // BtnA: 手動更新
    if (M5.BtnA.wasPressed()) g_last_check = 0;

    handleTouch();

    // デモモード進行
    if (g_demo_mode) {
        if ((unsigned long)(now - g_demo_state_started) >= DEMO_STATE_MS) {
            g_demo_state_idx = (g_demo_state_idx + 1) % 3;
            g_demo_state_started = now;
            g_state = DEMO_SEQ[g_demo_state_idx];
            if (g_state != g_prev_state) {
                playBeep(g_state);
                g_prev_state = g_state;
            }
            // テロップ更新 (デモ表示)
            const char* state_str = "雨の心配なし";
            if (g_state == STATE_RAIN_SOON) state_str = "もうすぐ雨が来ます";
            else if (g_state == STATE_RAINING) state_str = "雨が降っています";
            char buf[128];
            snprintf(buf, sizeof(buf),
                     "[DEMO] %s ・ 5秒長押しで通常モードへ ・ ", state_str);
            g_telop_text = buf;
            rebuildTelopSprite();
        }
    }

    // 天気取得 (デモ中はスキップ)
    if (!g_demo_mode && (unsigned long)(now - g_last_check) >= CHECK_INTERVAL_MS) {
        g_last_check = now;
        if (!g_wifi_ok) g_wifi_ok = connectWiFi();
        if (g_wifi_ok) {
            fetchForecast();   // 失敗してもメイン状態には影響させない
            bool ok = fetchWeather(g_current, g_max_forecast);
            if (!ok) {
                g_state = STATE_ERROR;
            } else if (g_current >= RAIN_ALERT_THRESHOLD) {
                g_state = STATE_RAINING;
            } else if (g_max_forecast >= RAIN_ALERT_THRESHOLD) {
                g_state = STATE_RAIN_SOON;
            } else {
                g_state = STATE_CLEAR;
            }

            struct tm ti;
            if (getLocalTime(&ti)) {
                char buf[6];
                snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
                g_last_updated = buf;
            }
        } else {
            g_state = STATE_ERROR;
        }

        // 状態遷移時のみ音を鳴らす
        if (g_state != g_prev_state) {
            playBeep(g_state);
            g_prev_state = g_state;
        }

        // テロップ更新
        const char* state_str = "雨の心配なし";
        if (g_state == STATE_RAIN_SOON) state_str = "もうすぐ雨が来ます";
        else if (g_state == STATE_RAINING) state_str = "雨が降っています";
        else if (g_state == STATE_ERROR) state_str = "通信エラー";

        char buf[128];
        snprintf(buf, sizeof(buf),
                 "更新 %s ・ 現在 %.1f mm/h ・ %d分先最大 %.1f mm/h ・ %s ・ ",
                 g_last_updated.c_str(), g_current, FORECAST_MINUTES,
                 g_max_forecast, state_str);
        g_telop_text = buf;
        rebuildTelopSprite();
    }

    g_robot.update(now);
    renderFrame();

    delay(30);  // ~30fps
}

static uint16_t bgForState(WeatherState s) {
    switch (s) {
        case STATE_RAIN_SOON: return COLOR_GRAY;
        case STATE_RAINING:   return COLOR_BLUE;
        case STATE_ERROR:     return COLOR_DARK;
        case STATE_CLEAR:
        case STATE_INIT:
        default:              return COLOR_BLACK;
    }
}

static void rebuildTelopSprite() {
    g_telop_sprite.setFont(&fonts::lgfxJapanGothicP_16);
    int tw = g_telop_sprite.textWidth(g_telop_text.c_str());
    if (tw < 1) tw = 1;
    g_telop_pixel_w = tw;

    // sprite 横幅は最低 320 + tw に
    int need = tw + 320;
    if (need > 960) need = 960;
    if (g_telop_sprite.width() < need) {
        g_telop_sprite.deleteSprite();
        g_telop_sprite.setPsram(true);
        g_telop_sprite.setColorDepth(16);
        g_telop_sprite.createSprite(need, TELOP_H);
        g_telop_sprite.setFont(&fonts::lgfxJapanGothicP_16);
    }
    g_telop_sprite.fillSprite(COLOR_BLACK);
    g_telop_sprite.setTextColor(COLOR_WHITE, COLOR_BLACK);
    g_telop_sprite.setCursor(0, 4);
    g_telop_sprite.print(g_telop_text.c_str());
    g_telop_offset = 0;
    g_telop_last_ms = millis();
}

static void renderFrame() {
    uint16_t bg = bgForState(g_state);
    g_canvas.fillSprite(bg);

    // 状態ごとの目の色
    switch (g_state) {
        case STATE_RAIN_SOON: g_robot.setEyeColor(0xFFE0); break; // 黄
        case STATE_RAINING:   g_robot.setEyeColor(0xC618); break; // 灰
        case STATE_ERROR:     g_robot.setEyeColor(0xF800); break; // 赤
        default:              g_robot.setEyeColor(0xE041); break; // デフォルト赤 (CLEAR/INIT)
    }

    g_robot.draw(g_canvas, bg);

    // スライダー描画
    int track_y = SLIDER_Y + SLIDER_H / 2;
    g_canvas.fillSmoothRoundRect(SLIDER_X, track_y - 3, SLIDER_W, 6, 3, 0x4208);
    int fill_w = (SLIDER_W * g_volume) / 100;
    g_canvas.fillSmoothRoundRect(SLIDER_X, track_y - 3, fill_w, 6, 3, 0x07E0);
    int thumb_x = SLIDER_X + fill_w;
    g_canvas.fillSmoothCircle(thumb_x, track_y, 9, COLOR_WHITE);
    g_canvas.drawCircle(thumb_x, track_y, 9, COLOR_BLACK);
    // 音量数値
    g_canvas.setFont(&fonts::Font0);
    g_canvas.setTextColor(COLOR_BLACK);
    g_canvas.setCursor(SLIDER_X, SLIDER_Y - 2);
    g_canvas.printf("VOL %d", g_volume);

    // テロップ (黒帯背景に左スクロール)
    g_canvas.fillRect(0, TELOP_Y, 320, TELOP_H, COLOR_BLACK);
    uint32_t now = millis();
    uint32_t dt = now - g_telop_last_ms;
    g_telop_last_ms = now;
    g_telop_offset += (int)((dt * 60) / 1000);  // 60 px/sec
    int period = g_telop_pixel_w > 0 ? g_telop_pixel_w : 320;
    if (g_telop_offset >= period) g_telop_offset -= period;

    // sprite の (offset, 0, 320, TELOP_H) を切り出して画面下部に貼る
    g_telop_sprite.pushSprite(&g_canvas, -g_telop_offset, TELOP_Y);
    // ループ用に右側にも繋げて描く
    g_telop_sprite.pushSprite(&g_canvas, period - g_telop_offset, TELOP_Y);

    // 左上 ミュートアイコン
    drawSpeakerIcon(g_canvas, MUTE_X, MUTE_Y, g_muted);
    // 右上 予報
    drawTopForecast(g_canvas);

    g_canvas.pushSprite(0, 0);
}

static void toggleDemoMode() {
    g_demo_mode = !g_demo_mode;
    Serial.printf("[Demo] mode=%s\n", g_demo_mode ? "ON" : "OFF");
    if (g_demo_mode) {
        g_demo_state_idx = 0;
        g_demo_state_started = millis();
        g_state = DEMO_SEQ[0];
        if (g_state != g_prev_state) {
            playBeep(g_state);
            g_prev_state = g_state;
        }
    } else {
        // 通常モード復帰: 状態とテロップを初期化、次の更新を即時実行
        g_state = STATE_INIT;
        g_prev_state = STATE_INIT;
        g_last_check = millis() - CHECK_INTERVAL_MS;
        g_telop_text = "通常モードに戻ります  更新中...  ";
        rebuildTelopSprite();
    }
}

static void handleTouch() {
    if (!M5.Touch.isEnabled()) return;
    auto td = M5.Touch.getDetail();
    uint32_t now = millis();

    if (td.isPressed()) {
        if (td.wasPressed()) {
            int bx = td.base_x;
            int by = td.base_y;
            bool in_slider =
                (bx >= SLIDER_X - 4 && bx <= SLIDER_X + SLIDER_W + 4 &&
                 by >= SLIDER_Y - 10 && by <= SLIDER_Y + SLIDER_H + 10);
            bool in_mute =
                (bx >= MUTE_X && bx < MUTE_X + MUTE_W &&
                 by >= MUTE_Y && by < MUTE_Y + MUTE_H);
            g_press_in_slider = in_slider;
            g_press_in_mute = in_mute;
            g_dragging = in_slider;
            g_dragged_this_press = false;
            g_press_start_ms = now;
            g_long_press_fired = false;
        }

        // 長押し検出 (スライダー外で 5 秒保持)
        if (!g_press_in_slider && !g_long_press_fired &&
            (now - g_press_start_ms) >= LONG_PRESS_MS) {
            g_long_press_fired = true;
            toggleDemoMode();
        }

        // スライダードラッグ
        if (g_dragging && (td.x != td.prev_x || td.y != td.prev_y)) {
            g_dragged_this_press = true;
            int rel = td.x - SLIDER_X;
            if (rel < 0) rel = 0;
            if (rel > SLIDER_W) rel = SLIDER_W;
            uint8_t v = (uint8_t)((rel * 100) / SLIDER_W);
            if (v != g_volume) {
                g_volume = v;
                applyVolume();
            }
        }
    } else if (td.wasReleased()) {
        if (g_dragging && g_dragged_this_press) {
            g_prefs.putUChar("vol", g_volume);
            Serial.printf("[Volume] saved=%u\n", g_volume);
        }
        // ミュートタップ判定: ミュートエリア内で押し → ドラッグせず → 長押しもなく離した
        if (g_press_in_mute && !g_dragged_this_press && !g_long_press_fired) {
            g_muted = !g_muted;
            applyVolume();
            Serial.printf("[Mute] %s\n", g_muted ? "ON" : "OFF");
        }
        g_dragging = false;
        g_dragged_this_press = false;
        g_press_in_slider = false;
        g_press_in_mute = false;
        g_long_press_fired = false;
    }
}

static void playBeep(WeatherState s) {
    if (s == STATE_RAIN_SOON) {
        for (int i = 0; i < 3; i++) {
            M5.Speaker.tone(1760, 150);
            delay(200);
        }
        M5.Speaker.stop();
    } else if (s == STATE_RAINING) {
        M5.Speaker.tone(1568, 350);
        delay(400);
        M5.Speaker.stop();
    }
}

static bool connectWiFi() {
    Serial.printf("[WiFi] SSID='%s' に接続開始\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.printf("[WiFi] 接続失敗 status=%d\n", WiFi.status());
        return false;
    }
    Serial.printf("[WiFi] 接続成功 IP=%s RSSI=%d\n",
        WiFi.localIP().toString().c_str(), WiFi.RSSI());
    configTime(9 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    return true;
}

static bool fetchWeather(float& outCurrent, float& outMaxForecast) {
    char url[256];
    snprintf(url, sizeof(url),
        "https://map.yahooapis.jp/weather/V1/place"
        "?coordinates=%.4f,%.4f"
        "&output=json&past=0&interval=10"
        "&appid=%s",
        LOCATION_LON, LOCATION_LAT, YAHOO_APPID);

    Serial.printf("[Weather] GET %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(10000);
    http.setUserAgent("cores3_weather/1.0");

    int code = http.GET();
    Serial.printf("[Weather] HTTP code=%d\n", code);
    if (code != 200) {
        http.end();
        return false;
    }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, body);
    if (jerr) {
        Serial.printf("[Weather] JSON parse error: %s\n", jerr.c_str());
        return false;
    }

    JsonArray arr = doc["Feature"][0]["Property"]["WeatherList"]["Weather"].as<JsonArray>();
    if (arr.isNull()) {
        Serial.println("[Weather] WeatherList.Weather not found");
        return false;
    }

    outCurrent = 0.0f;
    outMaxForecast = 0.0f;
    int forecastCount = 0;
    const int forecastLimit = FORECAST_MINUTES / 10;

    for (JsonObject w : arr) {
        const char* type = w["Type"];
        float rain = w["Rainfall"].as<float>();
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

static void applyVolume() {
    if (g_muted) {
        M5.Speaker.setVolume(0);
    } else {
        M5.Speaker.setVolume((g_volume * 255) / 100);
    }
}

// ---------------------------------------------------------------------------
// 天気予報取得 (livedoor互換)
// ---------------------------------------------------------------------------
static bool fetchForecast() {
    char url[160];
    snprintf(url, sizeof(url),
        "https://weather.tsukumijima.net/api/forecast?city=%s", CITY_CODE);
    Serial.printf("[Forecast] GET %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setTimeout(8000);
    http.setUserAgent("cores3_weather/1.0");

    int code = http.GET();
    Serial.printf("[Forecast] HTTP code=%d\n", code);
    if (code != 200) { http.end(); return false; }

    String body = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError jerr = deserializeJson(doc, body);
    if (jerr) {
        Serial.printf("[Forecast] JSON parse error: %s\n", jerr.c_str());
        return false;
    }

    JsonArray arr = doc["forecasts"].as<JsonArray>();
    if (arr.isNull()) return false;

    for (int i = 0; i < 2 && i < (int)arr.size(); i++) {
        JsonObject f = arr[i];
        g_forecast[i].telop = String((const char*)(f["telop"] | ""));
        const char* tmax = f["temperature"]["max"]["celsius"] | "";
        const char* tmin = f["temperature"]["min"]["celsius"] | "";
        g_forecast[i].t_max = (tmax[0]) ? atoi(tmax) : -127;
        g_forecast[i].t_min = (tmin[0]) ? atoi(tmin) : -127;
        g_forecast[i].valid = true;
        Serial.printf("  [%d] %s max=%d min=%d\n", i,
            g_forecast[i].telop.c_str(), g_forecast[i].t_max, g_forecast[i].t_min);
    }
    return true;
}

// ---------------------------------------------------------------------------
// スピーカーアイコン描画 (40x40 領域、白)
// ---------------------------------------------------------------------------
static void drawSpeakerIcon(M5Canvas& cv, int x, int y, bool muted) {
    const uint16_t col = muted ? 0xC618 : COLOR_WHITE;  // ミュート時は薄灰
    // 本体 (小さな縦長矩形 x+10..16, y+15..25)
    cv.fillRect(x + 10, y + 15, 6, 10, col);
    // コーン (三角形)
    cv.fillTriangle(x + 16, y + 12,
                    x + 16, y + 28,
                    x + 28, y + 6,  col);
    cv.fillTriangle(x + 16, y + 28,
                    x + 28, y + 6,
                    x + 28, y + 34, col);
    if (muted) {
        // 赤い斜線
        for (int i = -1; i <= 1; i++) {
            cv.drawLine(x + 5 + i, y + 5, x + 35 + i, y + 35, 0xF800);
        }
    } else {
        // サウンドウェーブ (右側に弧)
        cv.drawCircle(x + 28, y + 20, 5, col);
        cv.drawCircle(x + 28, y + 20, 9, col);
    }
}

// ---------------------------------------------------------------------------
// 右上 予報表示
// ---------------------------------------------------------------------------
static void drawTopForecast(M5Canvas& cv) {
    cv.setFont(&fonts::lgfxJapanGothicP_16);
    // 背景に被る場合に備えて半透明風の枠を描かず、単純に白文字で
    for (int i = 0; i < 2; i++) {
        int y = FCST_Y + i * 19;
        cv.setTextColor(COLOR_WHITE);
        cv.setCursor(FCST_X, y);
        const char* label = (i == 0) ? "今日" : "明日";
        if (g_forecast[i].valid) {
            char buf[64];
            if (g_forecast[i].t_max != -127 && g_forecast[i].t_min != -127) {
                snprintf(buf, sizeof(buf), "%s %s %d/%d",
                    label, g_forecast[i].telop.c_str(),
                    g_forecast[i].t_max, g_forecast[i].t_min);
            } else {
                snprintf(buf, sizeof(buf), "%s %s",
                    label, g_forecast[i].telop.c_str());
            }
            cv.print(buf);
        } else {
            cv.printf("%s --", label);
        }
    }
}
