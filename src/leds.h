// Rez's LEDs: they pulse with the game's vibration, in the picture's colour (module.cfg's settings `leds`: off,
// back or all, and `led_brightness`, 10-100). Lit through the SDK's access to the device and to the finished
// frames (dream::System::leds, frame_sample); nothing happens where the device has no LEDs.
#pragma once

namespace dream {
class System;
}

namespace dreamcomp {
class Settings;
}

namespace rez {

// Once, before the first guest instruction.
void leds_start(dream::System& sys, dreamcomp::Settings& settings);
// A setting the host changed while the game runs (PortInfo::on_setting).
void leds_setting(dream::System& sys, dreamcomp::Settings& settings, const char* key);
// The vibration the game asks of player `port`'s pack: `strength` 0-1 for `ms` milliseconds, 0 to stop.
// Whatever the player set the motor to (Off too), the LEDs follow what the game asks.
void leds_vibration(unsigned port, float strength, unsigned ms);
// Once, after the guest stopped: the LEDs given back as NextUI had them.
void leds_stop();

}  // namespace rez
