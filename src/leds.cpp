// See leds.h.
//
// A worker thread of its own drives the pulse and analyses the picture's colour, so the game's threads never
// wait on either. A vibration event raises the pulse quickly, holds it, and lets it fall as the event ends. The
// colour is the picture's dominant vivid hue (dominant_colour), found in the SDK's sample of each frame.
//
//   off   every LED off while the game plays.
//   back  the back LEDs pulse (on the Brick: the top bar and the triggers); the front ones, F1 and F2, stay off.
//   all   every LED pulses.
//
// No `leds` setting leaves the LEDs as NextUI has them. While the host's menu is up they are NextUI's (the SDK
// holds them), and the pulse ends with the motor.
#include "leds.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "dream/runtime/system.h"
#include "dreamcomp/settings.h"

namespace rez {

namespace {

using Clock = std::chrono::steady_clock;

// Which LEDs the game lights: none of them (NextUI's own), or off, the back ones, or all.
enum Mode : unsigned { kNone = 0, kOff = 1, kBack = 2, kAll = 3 };

// The pulse's envelope, in milliseconds: the rise, the fall, and the least time an event is held at full (so
// that a short one still shows).
constexpr float kRiseMs = 30.0f, kFallMs = 70.0f;
constexpr std::int64_t kMinHoldMs = 20;

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    std::uint32_t packed() const noexcept { return (std::uint32_t{r} << 16) | (std::uint32_t{g} << 8) | b; }
};

// `c` brightened until its largest channel is 255, its hue and saturation kept. Black stays black.
Rgb at_full_value(Rgb c) noexcept {
    const unsigned top = std::max(c.r, std::max(c.g, c.b));
    if (!top)
        return c;
    auto up = [top](unsigned v) { return static_cast<std::uint8_t>(std::min(255u, (v * 255u + top / 2) / top)); };
    return Rgb{up(c.r), up(c.g), up(c.b)};
}

// The colour an RGBA8 picture (red first in memory) is most vividly: the hue that carries the most saturation
// and brightness (each pixel weighted by both, squared, so a dim background, grey or white counts for little
// against a bright coloured line), as the mean of its pixels, at full value. False when the picture has no
// colour to speak of (black, grey or white), when the last colour should stay.
bool dominant_colour(const std::uint8_t* rgba, std::size_t pixels, Rgb& out) noexcept {
    // Hue in 24 bins of 15 degrees. A pixel counts with weight saturation^2 * value^2: a bright, fully
    // coloured pixel 1, a dim background (value 0.15) 0.02, grey and white nothing; below a quarter of
    // saturation, or nearly black, it has no hue worth showing and is left out.
    constexpr unsigned kBins = 24;
    constexpr int kDarkest = 32;
    float weight[kBins] = {}, sum_r[kBins] = {}, sum_g[kBins] = {}, sum_b[kBins] = {};
    for (std::size_t i = 0; i < pixels; ++i) {
        const int r = rgba[4 * i], g = rgba[4 * i + 1], b = rgba[4 * i + 2];
        const int top = std::max(r, std::max(g, b)), d = top - std::min(r, std::min(g, b));
        if (top < kDarkest || d * 4 < top)
            continue;
        const float fr = static_cast<float>(r), fg = static_cast<float>(g), fb = static_cast<float>(b);
        const float ftop = static_cast<float>(top), fd = static_cast<float>(d);
        float h = top == r ? (fg - fb) / fd : top == g ? 2.0f + (fb - fr) / fd : 4.0f + (fr - fg) / fd;
        if (h < 0.0f)
            h += 6.0f;
        const unsigned bin = std::min(kBins - 1, static_cast<unsigned>(h * (kBins / 6.0f)));
        const float s = fd / ftop, v = ftop / 255.0f;
        const float w = s * s * v * v;
        weight[bin] += w;
        sum_r[bin] += w * fr, sum_g[bin] += w * fg, sum_b[bin] += w * fb;
    }
    // The bin with the most weight, its neighbours counting half (a hue on a bin's edge is not split).
    unsigned best = 0;
    float best_score = 0.0f;
    for (unsigned i = 0; i < kBins; ++i) {
        const float score = weight[(i + kBins - 1) % kBins] + 2.0f * weight[i] + weight[(i + 1) % kBins];
        if (score > best_score)
            best = i, best_score = score;
    }
    float w = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
    for (const unsigned i : {(best + kBins - 1) % kBins, best, (best + 1) % kBins})
        w += weight[i], r += sum_r[i], g += sum_g[i], b += sum_b[i];
    // At least what two bright, fully coloured pixels of the sample would weigh: less is noise.
    if (w < 2.0f)
        return false;
    // The band's mean, its weakest channel halved (an LED shows a pale colour as white), at full value.
    r /= w, g /= w, b /= w;
    const float low = std::min(r, std::min(g, b)) * 0.5f;
    auto channel = [low](float v) { return static_cast<std::uint8_t>(std::clamp(v - low + 0.5f, 0.0f, 255.0f)); };
    out = at_full_value(Rgb{channel(r), channel(g), channel(b)});
    return true;
}

std::int64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}

dream::System* g_sys = nullptr;
std::atomic<unsigned> g_mode{kNone}, g_percent{100};
std::atomic<std::int64_t> g_hold_until{0};  // the pulse full until here, then its fall
std::atomic<bool> g_stop{false};
std::mutex g_mutex;
std::condition_variable g_cv;
std::thread g_thread;

void wake() {
    { std::lock_guard<std::mutex> lock(g_mutex); }
    g_cv.notify_all();
}

template <class Pred>
void wait_for(std::chrono::milliseconds ms, Pred pred) {
    std::unique_lock<std::mutex> lock(g_mutex);
    g_cv.wait_for(lock, ms, [&] { return g_stop.load() || pred(); });
}

void run() {
    dream::System& sys = *g_sys;
    float level = 0.0f;                           // the pulse's envelope, 0 to 1
    Rgb target{255, 255, 255};                    // the picture's colour: white until a picture gives one
    float sr = 255.0f, sg = 255.0f, sb = 255.0f;  // that colour, smoothed while lit
    std::vector<std::uint8_t> picture;
    unsigned picture_w = 0, picture_h = 0;
    // What the LEDs were last asked to show.
    bool have_last = false, pulsed = false;
    std::uint32_t last = 0;
    unsigned shown_mode = kNone, shown_pct = 0;
    // False while the host's menu has the LEDs.
    auto show = [&](std::uint32_t rgb, unsigned m, unsigned pct) {
        last = rgb, shown_mode = m, shown_pct = pct, have_last = true;
        return sys.leds(rgb, m == kBack, pct);
    };
    auto prev = Clock::now();
    while (!g_stop.load()) {
        const unsigned m = g_mode.load(), pct = g_percent.load();
        if (m == kNone) {
            if (have_last) {
                sys.leds_release();
                have_last = false;
            }
            level = 0.0f;
            wait_for(std::chrono::milliseconds(500), [] { return g_mode.load() != kNone; });
            prev = Clock::now();
            continue;
        }
        if (m == kOff) {
            if (!have_last || last != 0 || shown_mode != kOff || shown_pct != pct)
                show(0, m, pct);
            level = 0.0f;
            wait_for(std::chrono::milliseconds(500),
                     [pct] { return g_mode.load() != kOff || g_percent.load() != pct; });
            prev = Clock::now();
            continue;
        }
        const auto t = Clock::now();
        const float dt_ms = std::chrono::duration<float, std::milli>(t - prev).count();
        prev = t;
        const bool rising = now_ns() < g_hold_until.load();
        if (rising && !pulsed) {
            pulsed = true;
            std::printf("rez: the LEDs pulse with the game's vibration (whatever the motor is set to)\n");
            std::fflush(stdout);
        }
        const bool was_dark = level == 0.0f;
        level = std::clamp(level + (rising ? dt_ms / kRiseMs : -dt_ms / kFallMs), 0.0f, 1.0f);
        if (level > 0.0f) {
            // The picture's colour, analysed here (only while lit). A picture with no colour to speak of
            // keeps the last one.
            if (Rgb c; sys.frame_sample && sys.frame_sample(picture, picture_w, picture_h) &&
                       dominant_colour(picture.data(), std::size_t{picture_w} * picture_h, c))
                target = c;
            // A new pulse starts in the picture's colour; while lit, half way to a new colour each frame (as
            // NextUI's ambient mode blends), so one odd frame does not flicker.
            const float k = was_dark ? 1.0f : 0.5f;
            sr += (target.r - sr) * k, sg += (target.g - sg) * k, sb += (target.b - sb) * k;
        }
        auto channel = [level](float v) {
            return static_cast<std::uint8_t>(std::clamp(v * level + 0.5f, 0.0f, 255.0f));
        };
        const std::uint32_t out = Rgb{channel(sr), channel(sg), channel(sb)}.packed();
        if (!have_last || out != last || m != shown_mode || pct != shown_pct) {
            // The host's menu over the game: the motor is stopped, and the pulse ends with it.
            if (!show(out, m, pct)) {
                g_hold_until.store(0);
                level = 0.0f;
            }
        }
        // A frame apart while lit; dark, it waits for the next event.
        if (level == 0.0f && now_ns() >= g_hold_until.load()) {
            wait_for(std::chrono::milliseconds(250), [m, pct] {
                return g_mode.load() != m || g_percent.load() != pct || now_ns() < g_hold_until.load();
            });
            prev = Clock::now();
        } else {
            std::this_thread::sleep_until(t + std::chrono::milliseconds(16));
        }
    }
    if (have_last)
        sys.leds_release();
}

void read_settings(dreamcomp::Settings& settings) {
    const std::string leds = settings.get("leds");
    g_mode.store(leds == "off" ? kOff : leds == "back" ? kBack : leds == "all" ? kAll : kNone);
    g_percent.store(static_cast<unsigned>(std::clamp(settings.get_int("led_brightness", 100), 10, 100)));
}

// The settings in effect: the frames sampled only while their colour is wanted, the worker started at the
// first mode that lights anything.
void apply() {
    dream::System& sys = *g_sys;
    if (!sys.leds)
        return;
    const unsigned m = g_mode.load();
    if (sys.sample_frames)
        sys.sample_frames(m == kBack || m == kAll);
    if (m != kNone && !g_thread.joinable())
        g_thread = std::thread(run);
    wake();
}

}  // namespace

void leds_start(dream::System& sys, dreamcomp::Settings& settings) {
    g_sys = &sys;
    read_settings(settings);
    if (g_mode.load() != kNone && !sys.leds)
        std::printf("rez: this device has no LEDs to light\n");
    apply();
}

void leds_setting(dream::System&, dreamcomp::Settings& settings, const char* key) {
    if (g_sys == nullptr || g_stop.load())
        return;
    if (std::strcmp(key, "leds") != 0 && std::strcmp(key, "led_brightness") != 0)
        return;
    read_settings(settings);
    apply();
}

void leds_vibration(unsigned port, float strength, unsigned ms) {
    const unsigned m = g_mode.load();
    if (port != 0 || (m != kBack && m != kAll))
        return;
    if (strength <= 0.0f || ms == 0) {
        g_hold_until.store(0);
        return;
    }
    const std::int64_t now = now_ns();
    const std::int64_t end = now + static_cast<std::int64_t>(ms) * 1'000'000;
    // Full until the fall can finish at the event's end, but long enough to be seen.
    g_hold_until.store(std::max(end - static_cast<std::int64_t>(kFallMs) * 1'000'000, now + kMinHoldMs * 1'000'000));
    wake();
}

void leds_stop() {
    g_stop.store(true);
    wake();
    if (g_thread.joinable())
        g_thread.join();  // the worker gives the LEDs back on its way out
    if (g_sys != nullptr && g_sys->sample_frames)
        g_sys->sample_frames(false);
}

}  // namespace rez
