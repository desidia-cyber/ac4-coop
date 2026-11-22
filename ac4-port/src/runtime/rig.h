// Automated test rig (headless-ish, silent, scripted pad input). Not part of a player build's
// feature set: it exists so the game can be driven without a visible window, without sound and
// without touching the real keyboard, mouse or controllers.
#pragma once
#include <string>

namespace rt::rig {

// Reads the rig options from cfg() (set by main.cpp's command line) and loads the script.
void init();
bool enabled();

// Scripted pad state for this port. Returns false when the rig does not drive this port, so the
// real controllers are used instead.
bool pad(unsigned port, void* xinput_state);

// Silent output.
bool muted();

// True when the rig should acknowledge system dialogs by itself (it is enabled).
bool auto_ok();

// Rig only: treat a save the game calls corrupted as loadable, so a console-signed save can be
// used for research. Never enabled in a player build (the rig is not compiled into one).
bool save_bypass();

// Screenshots (the after-present hook in gpu/display.cpp).
bool capture_enabled();
bool capture_due();
const std::string& capture_dir();

// Elapsed seconds since the rig started.
double elapsed();

}  // namespace rt::rig
