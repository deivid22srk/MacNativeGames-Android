// M4 — touch + gamepad input state (pure logic, host-testable).
//
// Kotlin forwards MotionEvent / KeyEvent into this module through JNI; the
// runtime (and the M4 probe screen) reads the resulting state. The mapping
// target is the Switch layout the UI promises: A/B/X/Y, L/R/ZL/ZR, +/-,
// D-pad and a left stick — driven by standard Android gamepad keycodes.
#pragma once

#include <cstdint>

namespace mng::input {

// Button bits (Switch-style layout).
enum Buttons : uint32_t {
    kBtnA = 1u << 0,
    kBtnB = 1u << 1,
    kBtnX = 1u << 2,
    kBtnY = 1u << 3,
    kBtnL = 1u << 4,
    kBtnR = 1u << 5,
    kBtnZL = 1u << 6,
    kBtnZR = 1u << 7,
    kBtnPlus = 1u << 8,
    kBtnMinus = 1u << 9,
    kBtnUp = 1u << 10,
    kBtnDown = 1u << 11,
    kBtnLeft = 1u << 12,
    kBtnRight = 1u << 13,
};

struct State {
    float touch_x = -1.0f;
    float touch_y = -1.0f;
    bool touch_down = false;
    uint32_t buttons = 0;
    // Left stick, normalized to [-1, 1].
    float stick_lx = 0.0f;
    float stick_ly = 0.0f;
};

// Android MotionEvent action constants (subset): 0 down, 1 up, 2 move.
void on_touch(int action, float x, float y);

// Android KeyEvent code + action (0 down, 1 up). Returns true when the keycode
// mapped to a Switch-layout button (false → unknown, caller may ignore).
bool on_key(int android_keycode, int action);

// Left stick analog values, already normalized to [-1, 1] by the caller.
void on_stick(float x, float y);

State snapshot();

// Used by tests to reset to a neutral state.
void reset();

} // namespace mng::input
