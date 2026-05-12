#include "robot.h"
#include <math.h>
#include <stdlib.h>

// pivot 座標 (キャンバス中心)
static constexpr int PIVOT_X = 160;
static constexpr int PIVOT_Y = 100;

// パーツ寸法 (KSstackchan の値を ~0.78x で縮小)
static constexpr int HEAD_W = 140, HEAD_H = 85,  HEAD_R = 12;
static constexpr int BODY_W = 116, BODY_H = 70,  BODY_R = 10;
static constexpr int ARM_W  = 14,  ARM_H  = 40,  ARM_R  = 3;
static constexpr int LEG_W  = 26,  LEG_H  = 18,  LEG_R  = 3;

// パーツのピボット基準オフセット (中心)
static constexpr int HEAD_OY = -40;
static constexpr int BODY_OY = 40;
static constexpr int ARM_OX  = 68,  ARM_OY = 40;
static constexpr int LEG_OX  = 22,  LEG_OY = 82;

// 顔パーツ (KSstackchan eyes.cpp / mouth.cpp:14-18 を 0.78x スケール)
//   元: 左目 26x26 円, 右目 50x50 円, 頭中心から (-48, -13)/(+22, -13)
//   元: 口 30x4 r1, 頭中心から (0, +42)
static constexpr int EYE_L_R   = 10;          // 左目半径 (26/2 * 0.78)
static constexpr int EYE_R_R   = 19;          // 右目半径 (50/2 * 0.78)
static constexpr int EYE_L_OX  = -37;         // 左目 X オフセット
static constexpr int EYE_R_OX  = 22;          // 右目 X オフセット
static constexpr int EYE_OY    = -10;         // 共通 Y オフセット (頭中心から)
static constexpr int MOUTH_W   = 24,  MOUTH_H = 4, MOUTH_R = 1;
static constexpr int MOUTH_OY  = 33;          // 頭中心から下

// 猫耳 (KSstackchan ROBOT は 50x50 を 45° 回転したひし形上半分、ここではそのまま三角形で描画)
static constexpr int ANT_BASE_W = 28;         // 底辺幅
static constexpr int ANT_HEIGHT = 18;         // 三角形の高さ
static constexpr int ANT_OX     = 33;         // 中心 X オフセット (頭中心から)

// アニメーション定数 (KSstackchan robot.cpp:69-79 移植)
static constexpr int      WALK_X_LIMIT         = 30;
static constexpr float    WALK_LERP_SPEED      = 0.04f;
static constexpr uint32_t WALK_DWELL_MIN_MS    = 4000;
static constexpr uint32_t WALK_DWELL_MAX_MS    = 9000;
static constexpr uint32_t WALK_MOVE_DURATION_MS = 2500;
static constexpr float    GAIT_LEG_AMP  = 5.0f;
static constexpr float    GAIT_ARM_AMP  = 4.0f;
static constexpr float    GAIT_BODY_AMP = 1.5f;
static constexpr float    FACE_AMP = 3.0f;
static constexpr float    BODY_AMP = 1.5f;

Robot::Robot() {}

void Robot::update(uint32_t now) {
    // 呼吸: 4000ms 周期の sin
    float bphase = (float)(now % 4000) / 4000.0f * 6.28318530f;
    float bs = sinf(bphase);
    int head_breath = (int)lroundf(bs * FACE_AMP);
    int body_breath = (int)lroundf(bs * BODY_AMP);

    // 歩行状態機械
    if (!_walk_initialized) {
        _walk_state_until = now + 3000;
        _walk_initialized = true;
    }

    if (_walk_dwelling && now >= _walk_state_until) {
        _walk_target_x = (float)((rand() % (WALK_X_LIMIT * 2 + 1)) - WALK_X_LIMIT);
        _walk_dwelling = false;
        _walk_move_started = now;
        _walk_state_until = now + WALK_MOVE_DURATION_MS;
    }
    if (!_walk_dwelling) {
        _walk_cur_x += (_walk_target_x - _walk_cur_x) * WALK_LERP_SPEED;
        if (now >= _walk_state_until) {
            _walk_dwelling = true;
            uint32_t dwell = WALK_DWELL_MIN_MS +
                             (rand() % (WALK_DWELL_MAX_MS - WALK_DWELL_MIN_MS + 1));
            _walk_state_until = now + dwell;
        }
    }

    // 歩行ガイト
    int gait_body = 0, gait_arm_l = 0, gait_arm_r = 0, gait_leg_l = 0, gait_leg_r = 0;
    if (!_walk_dwelling) {
        float gphase = (float)((now - _walk_move_started) % 600) / 600.0f * 6.28318530f;
        float gs = sinf(gphase);
        gait_body  = (int)lroundf(fabsf(gs) * GAIT_BODY_AMP);
        gait_leg_l = (int)lroundf(-gs * GAIT_LEG_AMP);
        gait_leg_r = (int)lroundf( gs * GAIT_LEG_AMP);
        gait_arm_l = (int)lroundf( gs * GAIT_ARM_AMP);
        gait_arm_r = (int)lroundf(-gs * GAIT_ARM_AMP);
    }

    _head_dy = head_breath - gait_body;
    _body_dy = body_breath - gait_body;
    _arm_l_dy = _body_dy + gait_arm_l;
    _arm_r_dy = _body_dy + gait_arm_r;
    _leg_l_dy = gait_leg_l;
    _leg_r_dy = gait_leg_r;
    _root_dx  = (int)lroundf(_walk_cur_x);
}

void Robot::draw(M5Canvas& cv, uint16_t /*bg_color*/) {
    int rx = PIVOT_X + _root_dx;
    int ry = PIVOT_Y;

    // 頭中心 (触覚も頭にぶら下がる)
    int head_cx = rx;
    int head_cy = ry + HEAD_OY + _head_dy;
    int head_top = head_cy - HEAD_H/2;

    // 触覚 (三角形、頭の上左右に立つ)
    int ant_base_y  = head_top;
    int ant_apex_y  = head_top - ANT_HEIGHT;
    // 左
    {
        int ax = head_cx - ANT_OX;
        cv.fillTriangle(ax - ANT_BASE_W/2, ant_base_y,
                        ax + ANT_BASE_W/2, ant_base_y,
                        ax,                ant_apex_y,
                        _body_color);
    }
    // 右
    {
        int ax = head_cx + ANT_OX;
        cv.fillTriangle(ax - ANT_BASE_W/2, ant_base_y,
                        ax + ANT_BASE_W/2, ant_base_y,
                        ax,                ant_apex_y,
                        _body_color);
    }

    // 腕 (胴体の左右)
    cv.fillSmoothRoundRect(rx - ARM_OX - ARM_W/2, ry + ARM_OY - ARM_H/2 + _arm_l_dy,
                           ARM_W, ARM_H, ARM_R, _body_color);
    cv.fillSmoothRoundRect(rx + ARM_OX - ARM_W/2, ry + ARM_OY - ARM_H/2 + _arm_r_dy,
                           ARM_W, ARM_H, ARM_R, _body_color);

    // 脚 (胴体下)
    cv.fillSmoothRoundRect(rx - LEG_OX - LEG_W/2, ry + LEG_OY - LEG_H/2 + _leg_l_dy,
                           LEG_W, LEG_H, LEG_R, _body_color);
    cv.fillSmoothRoundRect(rx + LEG_OX - LEG_W/2, ry + LEG_OY - LEG_H/2 + _leg_r_dy,
                           LEG_W, LEG_H, LEG_R, _body_color);

    // 胴体
    cv.fillSmoothRoundRect(rx - BODY_W/2, ry + BODY_OY - BODY_H/2 + _body_dy,
                           BODY_W, BODY_H, BODY_R, _body_color);

    // 頭
    cv.fillSmoothRoundRect(head_cx - HEAD_W/2, head_cy - HEAD_H/2,
                           HEAD_W, HEAD_H, HEAD_R, _body_color);

    // 目 (非対称: 左小・右大)
    int eye_y = head_cy + EYE_OY;
    cv.fillSmoothCircle(head_cx + EYE_L_OX, eye_y, EYE_L_R, _eye_color);
    cv.fillSmoothCircle(head_cx + EYE_R_OX, eye_y, EYE_R_R, _eye_color);

    // 口 (頭下部、横線状)
    int mouth_y = head_cy + MOUTH_OY;
    cv.fillSmoothRoundRect(head_cx - MOUTH_W/2, mouth_y - MOUTH_H/2,
                           MOUTH_W, MOUTH_H, MOUTH_R, _mouth_color);
}
