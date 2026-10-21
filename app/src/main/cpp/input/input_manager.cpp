#include "input/input_manager.h"

#include <mutex>

namespace mng::input {
namespace {

std::mutex g_mu;
State g_state;

void set_btn(bool down, uint32_t mask) {
    if (down) g_state.buttons |= mask;
    else g_state.buttons &= ~mask;
}

} // namespace

void on_touch(int action, float x, float y) {
    std::lock_guard<std::mutex> lk(g_mu);
    switch (action) {
    case 0: // ACTION_DOWN
    case 2: // ACTION_MOVE
        g_state.touch_down = true;
        g_state.touch_x = x;
        g_state.touch_y = y;
        break;
    case 1: // ACTION_UP
    case 3: // ACTION_CANCEL
        g_state.touch_down = false;
        g_state.touch_x = x;
        g_state.touch_y = y;
        break;
    default:
        break;
    }
}

bool on_key(int keycode, int action) {
    const bool down = (action == 0); // ACTION_DOWN
    std::lock_guard<std::mutex> lk(g_mu);
    switch (keycode) {
    case 96:  set_btn(down, kBtnA); return true;      // BUTTON_A
    case 97:  set_btn(down, kBtnB); return true;      // BUTTON_B
    case 99:  set_btn(down, kBtnX); return true;      // BUTTON_X
    case 100: set_btn(down, kBtnY); return true;      // BUTTON_Y
    case 102: set_btn(down, kBtnL); return true;      // BUTTON_L1
    case 103: set_btn(down, kBtnR); return true;      // BUTTON_R1
    case 104: set_btn(down, kBtnZL); return true;     // BUTTON_L2
    case 105: set_btn(down, kBtnZR); return true;     // BUTTON_R2
    case 108: set_btn(down, kBtnPlus); return true;   // BUTTON_START
    case 109: set_btn(down, kBtnMinus); return true;  // BUTTON_SELECT
    case 19:  set_btn(down, kBtnUp); return true;     // DPAD_UP
    case 20:  set_btn(down, kBtnDown); return true;   // DPAD_DOWN
    case 21:  set_btn(down, kBtnLeft); return true;   // DPAD_LEFT
    case 22:  set_btn(down, kBtnRight); return true;  // DPAD_RIGHT
    default: return false;
    }
}

void on_stick(float x, float y) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_state.stick_lx = x;
    g_state.stick_ly = y;
}

State snapshot() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_state;
}

void reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_state = State();
}

} // namespace mng::input
