// M4 tests — input state machine (touch + Switch-layout mapping).
#include <cstdio>

#include "input/input_manager.h"

using mng::input::kBtnA;
using mng::input::kBtnB;
using mng::input::kBtnDown;
using mng::input::on_key;
using mng::input::on_stick;
using mng::input::on_touch;
using mng::input::reset;
using mng::input::snapshot;

static int failures = 0;
#define CHECK(cond, msg)                                     \
    do {                                                     \
        if (!(cond)) {                                       \
            std::printf("FAIL: %s\n", msg);                  \
            ++failures;                                      \
        } else {                                             \
            std::printf("ok: %s\n", msg);                    \
        }                                                    \
    } while (0)

int main() {
    reset();

    // Touch lifecycle: down → move → up.
    on_touch(0, 100.0f, 50.0f);
    {
        const auto s = snapshot();
        CHECK(s.touch_down && s.touch_x == 100.0f && s.touch_y == 50.0f,
              "touch down records position");
    }
    on_touch(2, 120.0f, 60.0f);
    {
        const auto s = snapshot();
        CHECK(s.touch_down && s.touch_x == 120.0f && s.touch_y == 60.0f,
              "touch move updates position");
    }
    on_touch(1, 120.0f, 60.0f);
    {
        const auto s = snapshot();
        CHECK(!s.touch_down, "touch up clears pressed flag");
    }

    // Gamepad mapping: Android keycodes 96 (A) and 97 (B).
    CHECK(on_key(96, 0), "BUTTON_A maps");
    {
        const auto s = snapshot();
        CHECK(s.buttons & kBtnA, "A bit set on down");
    }
    on_key(96, 1);
    {
        const auto s = snapshot();
        CHECK(!(s.buttons & kBtnA), "A bit cleared on up");
    }
    on_key(97, 0);
    CHECK(snapshot().buttons & kBtnB, "B bit set");
    CHECK(!on_key(77, 0), "unknown keycode reports unmapped");

    // D-pad + stick.
    on_key(20, 0); // DPAD_DOWN
    on_stick(0.5f, -0.5f);
    {
        const auto s = snapshot();
        CHECK(s.buttons & kBtnDown, "dpad down set");
        CHECK(s.stick_lx == 0.5f && s.stick_ly == -0.5f, "stick values stored");
    }

    reset();
    CHECK(snapshot().buttons == 0, "reset clears buttons");

    if (failures == 0) std::printf("input_manager tests PASSED\n");
    return failures == 0 ? 0 : 1;
}
