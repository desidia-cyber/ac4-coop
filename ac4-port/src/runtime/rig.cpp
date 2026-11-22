// Automated test rig: scripted pad input, silent output, optional screenshots.
//
// Script format (one state change per line; blank lines and '#' comments ignored):
//
//   <seconds> <port> <buttons> <lx> <ly> <rx> <ry>
//
//   seconds : time since the rig started
//   port    : controller slot (0 or 1)
//   buttons : "none" or '+'-joined names: A B X Y START BACK LB RB L3 R3 UP DOWN LEFT RIGHT
//   lx..ry  : stick values, -32768..32767
//
// At any moment a port's state is the last line at or before the current time. A port that
// appears anywhere in the script is owned by the rig; other ports still use real controllers.
#include "runtime/rig.h"

#include "runtime/config.h"
#include "runtime/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xinput.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace rt::rig {
namespace {

struct Entry {
  double t = 0;      // absolute script time
  double end = 0;    // queue mode: cumulative end time, relative to the queue start
  unsigned port = 0;
  WORD buttons = 0;
  bool lt = false, rt = false;  // analog triggers: not part of wButtons
  SHORT lx = 0, ly = 0, rx = 0, ry = 0;
};

struct State {
  bool enabled = false;
  bool muted = false;
  std::vector<Entry> timeline;
  std::vector<unsigned> ports;
  LARGE_INTEGER t0{}, freq{};
  bool have_clock = false;
  bool shots = false;
  std::string shot_dir;
  double shot_every = 0;
  double last_shot = -1e9;
  std::mutex mu;
  // Live control: the script is re-read when it changes and the elapsed time is published, so an
  // outside driver can steer a run without restarting it.
  std::string script_path, clock_path;
  std::filesystem::file_time_type script_mtime{};
  double last_poll = -1e9;
  // Command queue: lines appended to <shots>\cmd.txt are played in order, each held for its own
  // duration, so a run can be steered without restarting it. Format:
  //   <milliseconds> <buttons|none> [lx ly rx ry]
  std::string cmd_path;
  std::filesystem::file_time_type cmd_mtime{};
  std::vector<Entry> queue;
  size_t q_lines = 0, q_index = 0;
  double q_accum = 0, q_t0 = 0;
  bool q_running = false;
};

State g;

double now_s() {
  if (!g.have_clock) return 0;
  LARGE_INTEGER n;
  QueryPerformanceCounter(&n);
  return double(n.QuadPart - g.t0.QuadPart) / double(g.freq.QuadPart);
}

WORD buttons_from(const std::string& s, unsigned& triggers) {
  triggers = 0;
  if (s == "none" || s == "-" || s == "0") return 0;
  WORD m = 0;
  size_t i = 0;
  while (i <= s.size()) {
    size_t j = s.find('+', i);
    if (j == std::string::npos) j = s.size();
    std::string b = s.substr(i, j - i);
    if (b == "A") m |= XINPUT_GAMEPAD_A;
    else if (b == "B") m |= XINPUT_GAMEPAD_B;
    else if (b == "X") m |= XINPUT_GAMEPAD_X;
    else if (b == "Y") m |= XINPUT_GAMEPAD_Y;
    else if (b == "START") m |= XINPUT_GAMEPAD_START;
    else if (b == "BACK") m |= XINPUT_GAMEPAD_BACK;
    else if (b == "LB") m |= XINPUT_GAMEPAD_LEFT_SHOULDER;
    else if (b == "RB") m |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
    else if (b == "L3") m |= XINPUT_GAMEPAD_LEFT_THUMB;
    else if (b == "R3") m |= XINPUT_GAMEPAD_RIGHT_THUMB;
    else if (b == "LT") triggers |= 1;
    else if (b == "RT") triggers |= 2;
    else if (b == "UP") m |= XINPUT_GAMEPAD_DPAD_UP;
    else if (b == "DOWN") m |= XINPUT_GAMEPAD_DPAD_DOWN;
    else if (b == "LEFT") m |= XINPUT_GAMEPAD_DPAD_LEFT;
    else if (b == "RIGHT") m |= XINPUT_GAMEPAD_DPAD_RIGHT;
    else LOG_W("rig", "unknown button '%s' in the script", b.c_str());
    i = j + 1;
    if (j == s.size()) break;
  }
  return m;
}

void load_script(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    LOG_E("rig", "cannot open the rig script %s", path.c_str());
    return;
  }
  g.timeline.clear();
  g.ports.clear();
  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    std::istringstream ls(line);
    Entry e;
    std::string bs;
    double lx = 0, ly = 0, rx = 0, ry = 0;
    if (!(ls >> e.t >> e.port >> bs >> lx >> ly >> rx >> ry)) continue;
    unsigned trig = 0;
    e.buttons = buttons_from(bs, trig);
    e.lt = (trig & 1) != 0;
    e.rt = (trig & 2) != 0;
    e.lx = SHORT(lx);
    e.ly = SHORT(ly);
    e.rx = SHORT(rx);
    e.ry = SHORT(ry);
    g.timeline.push_back(e);
    bool seen = false;
    for (unsigned p : g.ports)
      if (p == e.port) seen = true;
    if (!seen) g.ports.push_back(e.port);
    ++n;
  }
  LOG_I("rig", "script %s: %d state(s) on %zu port(s)", path.c_str(), n, g.ports.size());
}

// Appends newly written command lines to the queue. Each line holds its state for its own duration,
// so commands can be sent one at a time while the game runs.
void load_commands(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return;
  std::string line;
  size_t n = 0, added = 0;
  while (std::getline(in, line)) {
    if (n++ < g.q_lines) continue;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    std::istringstream ls(line);
    double ms = 0, lx = 0, ly = 0, rx = 0, ry = 0;
    std::string bs;
    if (!(ls >> ms >> bs)) continue;
    // Optional port prefix: "<ms> p<0|1> <buttons> [lx ly rx ry]". Defaults to port 0, so existing
    // command files are unchanged; "p1" steers the second pad (co-op testing).
    unsigned port = 0;
    if (bs.size() == 2 && bs[0] == 'p' && (bs[1] == '0' || bs[1] == '1')) {
      port = unsigned(bs[1] - '0');
      if (!(ls >> bs)) continue;
    }
    ls >> lx >> ly >> rx >> ry;
    Entry e;
    unsigned trig = 0;
    e.port = port;
    e.buttons = buttons_from(bs, trig);
    e.lt = (trig & 1) != 0;
    e.rt = (trig & 2) != 0;
    e.lx = SHORT(lx);
    e.ly = SHORT(ly);
    e.rx = SHORT(rx);
    e.ry = SHORT(ry);
    g.q_accum += ms / 1000.0;
    e.end = g.q_accum;
    g.queue.push_back(e);
    bool seen = false;
    for (unsigned p : g.ports)
      if (p == port) seen = true;
    if (!seen) g.ports.push_back(port);
    ++added;
  }
  g.q_lines = n;
  if (added) LOG_I("rig", "queued %zu command(s), %zu total", added, g.queue.size());
}

// Publishes the elapsed time and reloads the script when it changes, so a run can be steered live.
void poll(double t) {
  if (t - g.last_poll < 0.2) return;
  g.last_poll = t;
  if (!g.clock_path.empty()) {
    if (FILE* f = std::fopen(g.clock_path.c_str(), "w")) {
      std::fprintf(f, "%.2f\n", t);
      std::fclose(f);
    }
  }
  if (!g.cmd_path.empty()) {
    std::error_code ce;
    auto cmt = std::filesystem::last_write_time(g.cmd_path, ce);
    if (!ce && cmt != g.cmd_mtime) {
      g.cmd_mtime = cmt;
      load_commands(g.cmd_path);
    }
  }
  if (g.script_path.empty()) return;
  std::error_code ec;
  auto mt = std::filesystem::last_write_time(g.script_path, ec);
  if (ec || mt == g.script_mtime) return;
  g.script_mtime = mt;
  LOG_I("rig", "script changed: reloading");
  load_script(g.script_path);
}

}  // namespace

void init() {
  const Config& c = cfg();
  QueryPerformanceFrequency(&g.freq);
  QueryPerformanceCounter(&g.t0);
  g.have_clock = true;
  g.muted = true;  // silent unless the script explicitly allows audio
  if (c.rig_script.empty() && c.rig_shots.empty()) g.muted = false;  // an ordinary run: audio as usual
  if (!c.rig_script.empty()) {
    g.enabled = true;
    load_script(c.rig_script);
    g.script_path = c.rig_script;
    std::error_code ec;
    g.script_mtime = std::filesystem::last_write_time(g.script_path, ec);
  }
  if (!c.rig_shots.empty()) {
    g.shots = true;
    g.shot_dir = c.rig_shots;
    g.shot_every = c.rig_shot_every > 0 ? c.rig_shot_every : 1.0;
    CreateDirectoryA(g.shot_dir.c_str(), nullptr);
    g.clock_path = g.shot_dir + "\\clock.txt";
    g.cmd_path = g.shot_dir + "\\cmd.txt";
  }
  if (g.enabled || g.shots)
    LOG_I("rig", "rig active: script '%s', screenshots '%s' every %.2f s, audio %s",
          c.rig_script.c_str(), g.shots ? g.shot_dir.c_str() : "-", g.shot_every, g.muted ? "silent" : "on");
}

bool enabled() { return g.enabled; }
bool muted() { return g.muted; }
bool auto_ok() { return g.enabled; }
bool save_bypass() { return g.enabled; }
double elapsed() { return now_s(); }

bool pad(unsigned port, void* xinput_state) {
  if (!g.enabled || !xinput_state) return false;
  poll(now_s());
  std::lock_guard<std::mutex> lk(g.mu);
  bool owned = false;
  for (unsigned p : g.ports)
    if (p == port) {
      owned = true;
      break;
    }
  if (!owned) return false;
  const double t = now_s();
  const Entry* cur = nullptr;
  if (!g.queue.empty()) {
    // Queue mode: play each command for its duration, then hold the last state. The timeline is
    // shared by all ports; each port takes the last entry addressed to it, so one file can steer
    // both pads (a "p1" prefix targets the second).
    if (g.q_index >= g.queue.size()) {
      g.q_running = false;
      g.q_accum = 0;  // the next batch starts a fresh timeline
      for (size_t i = g.queue.size(); i-- > 0;)
        if (g.queue[i].port == port) { cur = &g.queue[i]; break; }
    } else {
      if (!g.q_running) {
        g.q_t0 = t;
        g.q_running = true;
        LOG_I("rig", "queue start (%zu command(s))", g.queue.size());
      }
      const double rel = t - g.q_t0;
      while (g.q_index < g.queue.size() && rel >= g.queue[g.q_index].end) ++g.q_index;
      if (g.q_index < g.queue.size()) {
        for (size_t i = g.q_index + 1; i-- > 0;)
          if (g.queue[i].port == port) { cur = &g.queue[i]; break; }
      } else {
        g.q_running = false;
        g.q_accum = 0;
        for (size_t i = g.queue.size(); i-- > 0;)
          if (g.queue[i].port == port) { cur = &g.queue[i]; break; }
      }
    }
  } else {
    for (const Entry& e : g.timeline)
      if (e.port == port && e.t <= t && (!cur || e.t >= cur->t)) cur = &e;
  }
  XINPUT_STATE* s = static_cast<XINPUT_STATE*>(xinput_state);
  std::memset(s, 0, sizeof *s);
  if (cur) {
    s->Gamepad.wButtons = cur->buttons;
    s->Gamepad.bLeftTrigger = cur->lt ? 255 : 0;
    s->Gamepad.bRightTrigger = cur->rt ? 255 : 0;
    s->Gamepad.sThumbLX = cur->lx;
    s->Gamepad.sThumbLY = cur->ly;
    s->Gamepad.sThumbRX = cur->rx;
    s->Gamepad.sThumbRY = cur->ry;
  }
  return true;
}

bool capture_enabled() { return g.shots; }

bool capture_due() {
  if (!g.shots) return false;
  std::lock_guard<std::mutex> lk(g.mu);
  const double t = now_s();
  if (t - g.last_shot < g.shot_every) return false;
  g.last_shot = t;
  return true;
}

const std::string& capture_dir() { return g.shot_dir; }

}  // namespace rt::rig


