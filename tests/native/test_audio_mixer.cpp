// M4 tests — mixer math (pure logic; no audio device).
#include <cmath>
#include <cstdio>
#include <vector>

#include "audio/audio_mixer.h"

using mng::audio::mix_stereo_s16;
using mng::audio::volume_ramp_s16;

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

static void test_mix_gain_and_clamp() {
    const int16_t a[4] = {1000, -1000, 20000, -20000};
    const int16_t b[4] = {500, 500, 20000, 20000};
    const std::vector<const int16_t*> inputs{a, b};
    const float gains[2] = {1.0f, 0.5f};
    int16_t out[4] = {};
    CHECK(mix_stereo_s16(inputs, 2, gains, out), "mix returns ok");
    CHECK(out[0] == 1250 && out[1] == -750, "gain math frame0");
    CHECK(out[2] == 30000, "frame1 L sums");
    CHECK(out[3] == -10000, "frame1 R sums");
}

static void test_mix_clamps() {
    std::vector<int16_t> big(64, 32767);
    std::vector<int16_t> big2(64, 32767);
    const std::vector<const int16_t*> inputs{big.data(), big2.data()};
    const float gains[2] = {1.0f, 1.0f};
    std::vector<int16_t> out(64, 0);
    CHECK(mix_stereo_s16(inputs, 32, gains, out.data()), "mix ok");
    CHECK(out[0] == 32767, "positive clamp");
}

static void test_ramp_endpoints() {
    std::vector<int16_t> s(20, 1000); // 10 stereo frames
    volume_ramp_s16(s.data(), 10, 1.0f, 0.0f);
    CHECK(s[0] == 1000, "ramp starts at full volume");
    CHECK(std::abs(s[18]) <= 110, "ramp ~ends near zero");
}

int main() {
    test_mix_gain_and_clamp();
    test_mix_clamps();
    test_ramp_endpoints();
    if (failures == 0) {
        std::printf("test_audio_mixer: ALL PASS\n");
        return 0;
    }
    std::printf("test_audio_mixer: %d FAILURES\n", failures);
    return 1;
}
