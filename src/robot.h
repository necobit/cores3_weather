#pragma once

#include <M5Unified.h>
#include <stdint.h>

class Robot {
public:
    Robot();
    void update(uint32_t now_ms);
    void draw(M5Canvas& cv, uint16_t bg_color);

    void setEyeColor(uint16_t c) { _eye_color = c; }
    void setBodyColor(uint16_t c) { _body_color = c; }

private:
    bool     _walk_initialized = false;
    bool     _walk_dwelling    = true;
    uint32_t _walk_state_until = 0;
    uint32_t _walk_move_started = 0;
    float    _walk_target_x = 0.0f;
    float    _walk_cur_x    = 0.0f;

    int _head_dy = 0;
    int _body_dy = 0;
    int _arm_l_dy = 0;
    int _arm_r_dy = 0;
    int _leg_l_dy = 0;
    int _leg_r_dy = 0;
    int _root_dx  = 0;

    uint16_t _body_color = 0xFC00;   // approx #FF8000
    uint16_t _eye_color  = 0xE041;   // approx #E00808
    uint16_t _mouth_color = 0x1082;  // approx #101014
};
