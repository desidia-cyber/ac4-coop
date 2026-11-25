// Optional Spacewar (AppID 480) Steamworks P2P transport (coop_steam.h).
//
// Dynamic LoadLibrary("steam_api64.dll") + Steamworks flat C API + manual callback dispatch.
// No Valve SDK headers or import libraries; steam_api64.dll is obtained at install time via
// tools\install_steam_api.ps1 (Steamworks.NET standalone release) and is never bundled.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "runtime/game/coop_steam.h"
#include "runtime/log.h"

namespace rt::game::coopsteam {
namespace {

constexpr uint32_t kSpacewarAppId = 480;
constexpr int32_t kChannelGame = 0;
constexpr int32_t kLobbyTypeFriendsOnly = 1;
constexpr int32_t kFriendFlagImmediate = 0x04;
constexpr int32_t kSendUnreliableNoNagle = 0 | 1;  // k_nSteamNetworkingSend_Unreliable | NoNagle
constexpr int32_t kEResultOK = 1;
constexpr uint32_t kChatEnterSuccess = 1;
constexpr uint32_t kChatStateEntered = 0x01;
constexpr uint32_t kChatStateLeaveMask = 0x02 | 0x04 | 0x08 | 0x10;  // Left|Disconnected|Kicked|Banned

// Steamworks callback IDs
constexpr int32_t kCbGameLobbyJoinRequested = 333;
constexpr int32_t kCbGameRichPresenceJoinRequested = 337;
constexpr int32_t kCbLobbyEnter = 504;
constexpr int32_t kCbLobbyChatUpdate = 506;
constexpr int32_t kCbLobbyCreated = 513;
constexpr int32_t kCbSteamAPICallCompleted = 703;
constexpr int32_t kCbSessionRequest = 1251;
constexpr int32_t kCbSessionFailed = 1252;

// ESteamNetworkingConfigValue int32 keys (from Valve's steamnetworkingtypes.h)
constexpr int32_t kCfgSendBufferSize = 9;
constexpr int32_t kCfgRecvBufferSize = 47;
constexpr int32_t kCfgRecvMaxMessageSize = 49;

#pragma pack(push, 8)
struct CallbackMsg_t {
  int32_t hSteamUser;
  int32_t callback;
  uint8_t* param;
  int32_t param_size;
};

struct SteamAPICallCompleted_t {
  uint64_t async_call;
  int32_t callback;
  uint32_t param_size;
};

struct LobbyCreated_t {
  int32_t result;
  uint64_t lobby;
};

struct LobbyEnter_t {
  uint64_t lobby;
  uint32_t perms;
  uint8_t locked;
  uint32_t response;
};

struct LobbyChatUpdate_t {
  uint64_t lobby;
  uint64_t user_changed;
  uint64_t making_change;
  uint32_t state_change;
};

struct GameLobbyJoinRequested_t {
  uint64_t lobby;
  uint64_t friend_id;
};

struct GameRichPresenceJoinRequested_t {
  uint64_t friend_id;
  char connect[256];
};

struct FriendGameInfo_t {
  uint64_t game_id;
  uint32_t game_ip;
  uint16_t game_port;
  uint16_t query_port;
  uint64_t lobby_id;
};
#pragma pack(pop)

static_assert(sizeof(LobbyCreated_t) == 16, "LobbyCreated_t x64 layout");
static_assert(offsetof(LobbyEnter_t, response) == 16, "LobbyEnter_t x64 layout");
static_assert(offsetof(FriendGameInfo_t, lobby_id) == 16, "FriendGameInfo_t x64 layout");

struct OpaqueIdentity {
  alignas(8) uint8_t bytes[160]{};
};

// Flat C API function table loaded dynamically from steam_api64.dll.
struct SteamApi {
  HMODULE dll = nullptr;

  bool (*IsSteamRunning)() = nullptr;
  int32_t (*InitFlat)(char* err_msg) = nullptr;
  void (*Shutdown)() = nullptr;
  int32_t (*GetHSteamPipe)() = nullptr;

  void (*ManualDispatch_Init)() = nullptr;
  void (*ManualDispatch_RunFrame)(int32_t pipe) = nullptr;
  bool (*ManualDispatch_GetNextCallback)(int32_t pipe, CallbackMsg_t* msg) = nullptr;
  void (*ManualDispatch_FreeLastCallback)(int32_t pipe) = nullptr;
  bool (*ManualDispatch_GetAPICallResult)(int32_t pipe, uint64_t call, void* buf, int32_t size, int32_t expected_cb,
                                          bool* failed) = nullptr;

  void* (*SteamUser_v023)() = nullptr;
  void* (*SteamMatchmaking_v009)() = nullptr;
  void* (*SteamFriends_v018)() = nullptr;
  void* (*SteamNetworkingMessages_v002)() = nullptr;
  void* (*SteamNetworkingUtils_v004)() = nullptr;

  uint64_t (*User_GetSteamID)(void* self) = nullptr;
  bool (*User_BLoggedOn)(void* self) = nullptr;

  uint64_t (*MM_CreateLobby)(void* self, int32_t type, int32_t max_members) = nullptr;
  uint64_t (*MM_JoinLobby)(void* self, uint64_t lobby) = nullptr;
  void (*MM_LeaveLobby)(void* self, uint64_t lobby) = nullptr;
  bool (*MM_SetLobbyData)(void* self, uint64_t lobby, const char* key, const char* val) = nullptr;
  const char* (*MM_GetLobbyData)(void* self, uint64_t lobby, const char* key) = nullptr;
  bool (*MM_SetLobbyJoinable)(void* self, uint64_t lobby, bool joinable) = nullptr;
  uint64_t (*MM_GetLobbyOwner)(void* self, uint64_t lobby) = nullptr;
  int32_t (*MM_GetNumLobbyMembers)(void* self, uint64_t lobby) = nullptr;
  uint64_t (*MM_GetLobbyMemberByIndex)(void* self, uint64_t lobby, int32_t idx) = nullptr;

  bool (*Friends_SetRichPresence)(void* self, const char* key, const char* val) = nullptr;
  void (*Friends_ClearRichPresence)(void* self) = nullptr;
  void (*Friends_ActivateGameOverlayInviteDialog)(void* self, uint64_t lobby) = nullptr;
  int32_t (*Friends_GetFriendCount)(void* self, int32_t flags) = nullptr;
  uint64_t (*Friends_GetFriendByIndex)(void* self, int32_t idx, int32_t flags) = nullptr;
  bool (*Friends_GetFriendGamePlayed)(void* self, uint64_t friend_id, FriendGameInfo_t* info) = nullptr;
  const char* (*Friends_GetPersonaName)(void* self) = nullptr;

  void (*Identity_SetSteamID64)(void* ident, uint64_t id) = nullptr;
  uint64_t (*Identity_GetSteamID64)(const void* ident) = nullptr;

  int32_t (*Net_SendMessageToUser)(void* self, const void* ident, const void* data, uint32_t size, int32_t flags,
                                   int32_t channel) = nullptr;
  int32_t (*Net_ReceiveMessagesOnChannel)(void* self, int32_t channel, void** out_msgs, int32_t max_msgs) = nullptr;
  bool (*Net_AcceptSessionWithUser)(void* self, const void* ident) = nullptr;
  bool (*Net_CloseSessionWithUser)(void* self, const void* ident) = nullptr;
  void (*Net_MessageRelease)(void* msg) = nullptr;

  bool (*Utils_SetGlobalConfigValueInt32)(void* self, int32_t cfg, int32_t val) = nullptr;
};

SteamApi g_api{};
void* g_user = nullptr;
void* g_mm = nullptr;
void* g_friends = nullptr;
void* g_net = nullptr;
void* g_utils = nullptr;

std::mutex g_mu;
std::atomic<bool> g_active{false};
std::atomic<bool> g_host{false};
std::atomic<uint64_t> g_self_id{0};
std::atomic<uint64_t> g_lobby_id{0};
std::atomic<uint64_t> g_peer_id{0};
std::atomic<bool> g_peer_changed{false};

bool g_auto_join = false;
bool g_join_in_flight = false;
bool g_lobby_created_handled = false;
uint64_t g_last_friend_scan_ms = 0;
uint64_t g_last_skipped_lobby = 0;
std::string g_last_error;

template <typename Fn>
bool load_sym(HMODULE dll, const char* name, Fn& out, std::string& missing) {
  FARPROC p = GetProcAddress(dll, name);
  if (!p) {
    if (!missing.empty()) missing += ", ";
    missing += name;
    return false;
  }
  out = reinterpret_cast<Fn>(p);
  return true;
}

bool env_flag(const char* name) {
  char buf[16]{};
  DWORD n = GetEnvironmentVariableA(name, buf, sizeof buf);
  return n > 0 && buf[0] != '0';
}

void copy_to_clipboard(const std::string& text) {
  if (text.empty()) return;
  if (!OpenClipboard(nullptr)) return;
  EmptyClipboard();
  HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
  if (mem) {
    void* ptr = GlobalLock(mem);
    if (ptr) {
      std::memcpy(ptr, text.c_str(), text.size() + 1);
      GlobalUnlock(mem);
      SetClipboardData(CF_TEXT, mem);
    } else {
      GlobalFree(mem);
    }
  }
  CloseClipboard();
}

void ensure_steam_appid_file(const std::filesystem::path& dir) {
  std::error_code ec;
  const auto p = dir / "steam_appid.txt";
  if (std::filesystem::exists(p, ec)) return;
  FILE* f = nullptr;
  if (fopen_s(&f, p.string().c_str(), "wb") == 0 && f) {
    std::fprintf(f, "%u\n", kSpacewarAppId);
    std::fclose(f);
  }
}

HMODULE load_steam_dll(std::string* loaded_path) {
  namespace fs = std::filesystem;
  std::vector<fs::path> candidates;

  char env_dll[MAX_PATH]{};
  DWORD n = GetEnvironmentVariableA("AC4_STEAM_DLL", env_dll, sizeof env_dll);
  if (n > 0 && n < sizeof env_dll) {
    candidates.emplace_back(env_dll);
  }

  char exe_buf[MAX_PATH]{};
  if (GetModuleFileNameA(nullptr, exe_buf, sizeof exe_buf)) {
    fs::path exe_dir = fs::path(exe_buf).parent_path();
    candidates.push_back(exe_dir / "steam_api64.dll");
  }
  candidates.push_back(fs::current_path() / "steam_api64.dll");

  for (const auto& c : candidates) {
    std::error_code ec;
    if (fs::exists(c, ec)) {
      HMODULE h = LoadLibraryA(c.string().c_str());
      if (h) {
        if (loaded_path) *loaded_path = c.string();
        return h;
      }
    }
  }
  HMODULE h = LoadLibraryA("steam_api64.dll");
  if (h && loaded_path) *loaded_path = "steam_api64.dll";
  return h;
}

bool is_lobby_member(uint64_t steam_id) {
  const uint64_t lid = g_lobby_id.load(std::memory_order_relaxed);
  if (!lid || !steam_id || !g_mm) return false;
  const int32_t count = g_api.MM_GetNumLobbyMembers(g_mm, lid);
  for (int32_t i = 0; i < count; ++i) {
    if (g_api.MM_GetLobbyMemberByIndex(g_mm, lid, i) == steam_id) return true;
  }
  return false;
}

void set_peer(uint64_t steam_id) {
  const uint64_t prev = g_peer_id.exchange(steam_id, std::memory_order_relaxed);
  if (prev != steam_id) {
    g_peer_changed.store(true, std::memory_order_relaxed);
    if (steam_id) {
      LOG_I("coop", "COOP STEAM: peer SteamID %llu", (unsigned long long)steam_id);
    } else if (prev) {
      LOG_I("coop", "COOP STEAM: peer SteamID %llu cleared", (unsigned long long)prev);
    }
  }
}

void close_peer_session(uint64_t steam_id) {
  if (!steam_id || !g_net) return;
  OpaqueIdentity ident{};
  g_api.Identity_SetSteamID64(ident.bytes, steam_id);
  g_api.Net_CloseSessionWithUser(g_net, ident.bytes);
}

void accept_peer_session(uint64_t steam_id) {
  if (!steam_id || !g_net) return;
  OpaqueIdentity ident{};
  g_api.Identity_SetSteamID64(ident.bytes, steam_id);
  g_api.Net_AcceptSessionWithUser(g_net, ident.bytes);
}

uint64_t parse_connect_lobby(const char* s) {
  if (!s) return 0;
  const char* p = std::strstr(s, "+connect_lobby");
  if (!p) return 0;
  p += std::strlen("+connect_lobby");
  while (*p == ' ' || *p == '\t' || *p == '=') ++p;
  return std::strtoull(p, nullptr, 10);
}

void begin_join_lobby(uint64_t lid) {
  if (!lid || !g_mm) return;
  const uint64_t cur = g_lobby_id.load(std::memory_order_relaxed);
  if (cur && cur != lid) {
    g_api.MM_LeaveLobby(g_mm, cur);
    g_lobby_id.store(0, std::memory_order_relaxed);
    close_peer_session(g_peer_id.load(std::memory_order_relaxed));
    set_peer(0);
  }
  g_join_in_flight = true;
  LOG_I("coop", "COOP STEAM: joining lobby %llu...", (unsigned long long)lid);
  g_api.MM_JoinLobby(g_mm, lid);
}

void scan_friends_for_lobby() {
  if (!g_auto_join || g_host.load() || g_join_in_flight || g_lobby_id.load() != 0 || !g_friends) return;
  const uint64_t now = GetTickCount64();
  if (g_last_friend_scan_ms && (now - g_last_friend_scan_ms) < 3000u) return;
  g_last_friend_scan_ms = now;

  const int32_t count = g_api.Friends_GetFriendCount(g_friends, kFriendFlagImmediate);
  for (int32_t i = 0; i < count; ++i) {
    const uint64_t fid = g_api.Friends_GetFriendByIndex(g_friends, i, kFriendFlagImmediate);
    FriendGameInfo_t info{};
    if (!g_api.Friends_GetFriendGamePlayed(g_friends, fid, &info)) continue;
    const uint32_t app_id = uint32_t(info.game_id & 0xffffffu);
    if (app_id == kSpacewarAppId && info.lobby_id != 0 && info.lobby_id != g_last_skipped_lobby) {
      LOG_I("coop", "COOP STEAM: found friend %llu in Spacewar lobby %llu",
            (unsigned long long)fid, (unsigned long long)info.lobby_id);
      begin_join_lobby(info.lobby_id);
      return;
    }
  }
}

void on_lobby_created(const LobbyCreated_t* ev) {
  if (!ev) return;
  // On real Steam, LobbyCreated arrives both as the CreateLobby API call result (703)
  // and as a broadcast callback (513). Handle idempotently.
  if (g_lobby_created_handled) return;
  g_lobby_created_handled = true;

  if (ev->result != kEResultOK || ev->lobby == 0) {
    std::lock_guard lk(g_mu);
    g_last_error = "Steam CreateLobby failed (result " + std::to_string(ev->result) + ")";
    LOG_E("coop", "COOP STEAM: %s", g_last_error.c_str());
    return;
  }

  g_lobby_id.store(ev->lobby, std::memory_order_relaxed);
  g_api.MM_SetLobbyData(g_mm, ev->lobby, "ac4coop", "1");
  g_api.MM_SetLobbyData(g_mm, ev->lobby, "ac4coop_ver", "1");
  g_api.MM_SetLobbyJoinable(g_mm, ev->lobby, true);

  const std::string lid_str = std::to_string(ev->lobby);
  const std::string connect_str = "+connect_lobby " + lid_str;
  if (g_friends) {
    g_api.Friends_SetRichPresence(g_friends, "connect", connect_str.c_str());
    g_api.Friends_SetRichPresence(g_friends, "status", "AC4 Co-op Lobby");
    if (!env_flag("AC4_COOP_STEAM_NO_OVERLAY")) {
      g_api.Friends_ActivateGameOverlayInviteDialog(g_friends, ev->lobby);
    }
  }
  if (!env_flag("AC4_COOP_STEAM_NO_CLIPBOARD")) {
    copy_to_clipboard(lid_str);
  }
  LOG_I("coop", "COOP STEAM: lobby created: %s (copied to clipboard; friends can join via invite, Join Game, or --coop-steam-join %s)",
        lid_str.c_str(), lid_str.c_str());
}

void on_lobby_enter(const LobbyEnter_t* ev) {
  if (!ev) return;
  g_join_in_flight = false;
  if (ev->response != kChatEnterSuccess || ev->lobby == 0) {
    std::lock_guard lk(g_mu);
    g_last_error = "Steam JoinLobby failed (response " + std::to_string(ev->response) + ")";
    LOG_E("coop", "COOP STEAM: %s", g_last_error.c_str());
    return;
  }
  if (g_host.load()) return;

  const char* tag = g_api.MM_GetLobbyData(g_mm, ev->lobby, "ac4coop");
  if (!tag || std::strcmp(tag, "1") != 0) {
    LOG_W("coop", "COOP STEAM: lobby %llu is not an AC4 co-op game (ac4coop='%s'); leaving",
          (unsigned long long)ev->lobby, tag ? tag : "");
    g_last_skipped_lobby = ev->lobby;
    g_api.MM_LeaveLobby(g_mm, ev->lobby);
    if (!g_auto_join) {
      std::lock_guard lk(g_mu);
      g_last_error = "that lobby is not an AC4 co-op game";
    }
    return;
  }

  g_lobby_id.store(ev->lobby, std::memory_order_relaxed);
  {
    std::lock_guard lk(g_mu);
    g_last_error.clear();
  }
  const uint64_t owner = g_api.MM_GetLobbyOwner(g_mm, ev->lobby);
  LOG_I("coop", "COOP STEAM: joined AC4 co-op lobby %llu (host SteamID %llu)",
        (unsigned long long)ev->lobby, (unsigned long long)owner);
  if (owner && owner != g_self_id.load()) {
    accept_peer_session(owner);
    set_peer(owner);
  }
}

void on_lobby_chat_update(const LobbyChatUpdate_t* ev) {
  if (!ev || ev->lobby != g_lobby_id.load()) return;
  const uint64_t self = g_self_id.load();
  if (ev->user_changed == self) return;

  if (ev->state_change & kChatStateEntered) {
    LOG_I("coop", "COOP STEAM: user %llu entered lobby %llu",
          (unsigned long long)ev->user_changed, (unsigned long long)ev->lobby);
    if (g_host.load()) {
      accept_peer_session(ev->user_changed);
      set_peer(ev->user_changed);
    }
  } else if (ev->state_change & kChatStateLeaveMask) {
    LOG_I("coop", "COOP STEAM: user %llu left lobby %llu (flags 0x%x)",
          (unsigned long long)ev->user_changed, (unsigned long long)ev->lobby, ev->state_change);
    if (ev->user_changed == g_peer_id.load()) {
      close_peer_session(ev->user_changed);
      set_peer(0);
      if (!g_host.load()) {
        // Host left the lobby; leave too so auto-join / invite can reconnect cleanly.
        g_api.MM_LeaveLobby(g_mm, ev->lobby);
        g_lobby_id.store(0, std::memory_order_relaxed);
      }
    }
  }
}

void handle_callback(int32_t cb_id, const uint8_t* param, int32_t param_size) {
  switch (cb_id) {
    case kCbLobbyCreated:
      if (param && param_size >= int32_t(sizeof(LobbyCreated_t))) {
        on_lobby_created(reinterpret_cast<const LobbyCreated_t*>(param));
      }
      break;
    case kCbLobbyEnter:
      if (param && param_size >= int32_t(sizeof(LobbyEnter_t))) {
        on_lobby_enter(reinterpret_cast<const LobbyEnter_t*>(param));
      }
      break;
    case kCbLobbyChatUpdate:
      if (param && param_size >= int32_t(sizeof(LobbyChatUpdate_t))) {
        on_lobby_chat_update(reinterpret_cast<const LobbyChatUpdate_t*>(param));
      }
      break;
    case kCbGameLobbyJoinRequested:
      if (!g_host.load() && param && param_size >= int32_t(sizeof(GameLobbyJoinRequested_t))) {
        const auto* ev = reinterpret_cast<const GameLobbyJoinRequested_t*>(param);
        LOG_I("coop", "COOP STEAM: invite accepted for lobby %llu from friend %llu",
              (unsigned long long)ev->lobby, (unsigned long long)ev->friend_id);
        begin_join_lobby(ev->lobby);
      }
      break;
    case kCbGameRichPresenceJoinRequested:
      if (!g_host.load() && param && param_size >= int32_t(sizeof(GameRichPresenceJoinRequested_t))) {
        const auto* ev = reinterpret_cast<const GameRichPresenceJoinRequested_t*>(param);
        const uint64_t lid = parse_connect_lobby(ev->connect);
        LOG_I("coop", "COOP STEAM: rich presence join from %llu ('%s' -> lobby %llu)",
              (unsigned long long)ev->friend_id, ev->connect, (unsigned long long)lid);
        if (lid) begin_join_lobby(lid);
      }
      break;
    case kCbSessionRequest:
      if (param && param_size >= 16) {
        const uint64_t sender = g_api.Identity_GetSteamID64(param);
        if (!sender || sender == g_self_id.load()) break;
        if (g_host.load()) {
          if (is_lobby_member(sender)) {
            g_api.Net_AcceptSessionWithUser(g_net, param);
            set_peer(sender);
          } else {
            LOG_W("coop", "COOP STEAM: ignoring session request from non-lobby SteamID %llu",
                  (unsigned long long)sender);
          }
        } else {
          const uint64_t expected_host = g_peer_id.load();
          if (expected_host == 0 || sender == expected_host) {
            g_api.Net_AcceptSessionWithUser(g_net, param);
            if (expected_host == 0) set_peer(sender);
          }
        }
      }
      break;
    case kCbSessionFailed:
      if (param && param_size >= 16) {
        const uint64_t sender = g_api.Identity_GetSteamID64(param);
        LOG_W("coop", "COOP STEAM: P2P session failed for SteamID %llu", (unsigned long long)sender);
        if (sender && sender == g_peer_id.load()) {
          close_peer_session(sender);
          // Keep g_peer_id on join if still in lobby so next send re-establishes relay; host waits for peer.
          if (g_host.load() && !is_lobby_member(sender)) set_peer(0);
        }
      }
      break;
    default:
      break;
  }
}

void run_callbacks() {
  const int32_t pipe = g_api.GetHSteamPipe();
  if (!pipe) return;
  g_api.ManualDispatch_RunFrame(pipe);
  CallbackMsg_t msg{};
  while (g_api.ManualDispatch_GetNextCallback(pipe, &msg)) {
    if (msg.callback == kCbSteamAPICallCompleted && msg.param &&
        msg.param_size >= int32_t(sizeof(SteamAPICallCompleted_t))) {
      const auto* done = reinterpret_cast<const SteamAPICallCompleted_t*>(msg.param);
      if (done->param_size > 0 && done->param_size <= 65536) {
        std::vector<uint8_t> buf(done->param_size);
        bool failed = false;
        if (g_api.ManualDispatch_GetAPICallResult(pipe, done->async_call, buf.data(), int32_t(done->param_size),
                                                  done->callback, &failed) &&
            !failed) {
          handle_callback(done->callback, buf.data(), int32_t(done->param_size));
        }
      }
    } else {
      handle_callback(msg.callback, msg.param, msg.param_size);
    }
    g_api.ManualDispatch_FreeLastCallback(pipe);
  }
}

}  // namespace

bool init(bool host, uint64_t join_lobby_id) {
  if (g_active.load()) return true;
  {
    std::lock_guard lk(g_mu);
    g_last_error.clear();
  }

  // Ensure Spacewar AppID 480 is configured via both environment and steam_appid.txt.
  SetEnvironmentVariableA("SteamAppId", "480");
  SetEnvironmentVariableA("SteamGameId", "480");
  char exe_buf[MAX_PATH]{};
  if (GetModuleFileNameA(nullptr, exe_buf, sizeof exe_buf)) {
    ensure_steam_appid_file(std::filesystem::path(exe_buf).parent_path());
  }
  ensure_steam_appid_file(std::filesystem::current_path());

  std::string dll_path;
  g_api.dll = load_steam_dll(&dll_path);
  if (!g_api.dll) {
    std::lock_guard lk(g_mu);
    g_last_error = "steam_api64.dll not found — run tools\\install_steam_api.ps1 to obtain it from Steamworks.NET";
    LOG_E("coop", "COOP STEAM: %s", g_last_error.c_str());
    return false;
  }

  std::string missing;
  load_sym(g_api.dll, "SteamAPI_IsSteamRunning", g_api.IsSteamRunning, missing);
  load_sym(g_api.dll, "SteamAPI_InitFlat", g_api.InitFlat, missing);
  load_sym(g_api.dll, "SteamAPI_Shutdown", g_api.Shutdown, missing);
  load_sym(g_api.dll, "SteamAPI_GetHSteamPipe", g_api.GetHSteamPipe, missing);
  load_sym(g_api.dll, "SteamAPI_ManualDispatch_Init", g_api.ManualDispatch_Init, missing);
  load_sym(g_api.dll, "SteamAPI_ManualDispatch_RunFrame", g_api.ManualDispatch_RunFrame, missing);
  load_sym(g_api.dll, "SteamAPI_ManualDispatch_GetNextCallback", g_api.ManualDispatch_GetNextCallback, missing);
  load_sym(g_api.dll, "SteamAPI_ManualDispatch_FreeLastCallback", g_api.ManualDispatch_FreeLastCallback, missing);
  load_sym(g_api.dll, "SteamAPI_ManualDispatch_GetAPICallResult", g_api.ManualDispatch_GetAPICallResult, missing);

  load_sym(g_api.dll, "SteamAPI_SteamUser_v023", g_api.SteamUser_v023, missing);
  load_sym(g_api.dll, "SteamAPI_SteamMatchmaking_v009", g_api.SteamMatchmaking_v009, missing);
  load_sym(g_api.dll, "SteamAPI_SteamFriends_v018", g_api.SteamFriends_v018, missing);
  load_sym(g_api.dll, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002", g_api.SteamNetworkingMessages_v002, missing);
  load_sym(g_api.dll, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004", g_api.SteamNetworkingUtils_v004, missing);

  load_sym(g_api.dll, "SteamAPI_ISteamUser_GetSteamID", g_api.User_GetSteamID, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamUser_BLoggedOn", g_api.User_BLoggedOn, missing);

  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_CreateLobby", g_api.MM_CreateLobby, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_JoinLobby", g_api.MM_JoinLobby, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_LeaveLobby", g_api.MM_LeaveLobby, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_SetLobbyData", g_api.MM_SetLobbyData, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_GetLobbyData", g_api.MM_GetLobbyData, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_SetLobbyJoinable", g_api.MM_SetLobbyJoinable, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_GetLobbyOwner", g_api.MM_GetLobbyOwner, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers", g_api.MM_GetNumLobbyMembers, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex", g_api.MM_GetLobbyMemberByIndex, missing);

  load_sym(g_api.dll, "SteamAPI_ISteamFriends_SetRichPresence", g_api.Friends_SetRichPresence, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamFriends_ClearRichPresence", g_api.Friends_ClearRichPresence, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog",
           g_api.Friends_ActivateGameOverlayInviteDialog, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamFriends_GetFriendCount", g_api.Friends_GetFriendCount, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamFriends_GetFriendByIndex", g_api.Friends_GetFriendByIndex, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamFriends_GetFriendGamePlayed", g_api.Friends_GetFriendGamePlayed, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamFriends_GetPersonaName", g_api.Friends_GetPersonaName, missing);

  load_sym(g_api.dll, "SteamAPI_SteamNetworkingIdentity_SetSteamID64", g_api.Identity_SetSteamID64, missing);
  load_sym(g_api.dll, "SteamAPI_SteamNetworkingIdentity_GetSteamID64", g_api.Identity_GetSteamID64, missing);

  load_sym(g_api.dll, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser", g_api.Net_SendMessageToUser, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel",
           g_api.Net_ReceiveMessagesOnChannel, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser", g_api.Net_AcceptSessionWithUser,
           missing);
  load_sym(g_api.dll, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser", g_api.Net_CloseSessionWithUser,
           missing);
  load_sym(g_api.dll, "SteamAPI_SteamNetworkingMessage_t_Release", g_api.Net_MessageRelease, missing);
  load_sym(g_api.dll, "SteamAPI_ISteamNetworkingUtils_SetGlobalConfigValueInt32",
           g_api.Utils_SetGlobalConfigValueInt32, missing);

  if (!missing.empty()) {
    std::lock_guard lk(g_mu);
    g_last_error = "steam_api64.dll is too old or incompatible (missing exports: " + missing +
                   ") — run tools\\install_steam_api.ps1";
    LOG_E("coop", "COOP STEAM: %s", g_last_error.c_str());
    FreeLibrary(g_api.dll);
    g_api = SteamApi{};
    return false;
  }

  char err_msg[1024]{};
  const int32_t rc = g_api.InitFlat(err_msg);
  if (rc != 0) {
    std::lock_guard lk(g_mu);
    if (rc == 2) {
      g_last_error = "Steam is not running — start Steam and log in first";
    } else {
      g_last_error = "SteamAPI_InitFlat failed (" + std::to_string(rc) + "): " + err_msg;
    }
    LOG_E("coop", "COOP STEAM: %s", g_last_error.c_str());
    FreeLibrary(g_api.dll);
    g_api = SteamApi{};
    return false;
  }

  g_api.ManualDispatch_Init();
  g_user = g_api.SteamUser_v023();
  g_mm = g_api.SteamMatchmaking_v009();
  g_friends = g_api.SteamFriends_v018();
  g_net = g_api.SteamNetworkingMessages_v002();
  g_utils = g_api.SteamNetworkingUtils_v004();

  if (!g_user || !g_mm || !g_net) {
    std::lock_guard lk(g_mu);
    g_last_error = "Steamworks interfaces unavailable after InitFlat";
    LOG_E("coop", "COOP STEAM: %s", g_last_error.c_str());
    g_api.Shutdown();
    FreeLibrary(g_api.dll);
    g_api = SteamApi{};
    return false;
  }

  if (g_utils) {
    constexpr int32_t kBufBytes = 4 * 1024 * 1024;
    g_api.Utils_SetGlobalConfigValueInt32(g_utils, kCfgSendBufferSize, kBufBytes);
    g_api.Utils_SetGlobalConfigValueInt32(g_utils, kCfgRecvBufferSize, kBufBytes);
    g_api.Utils_SetGlobalConfigValueInt32(g_utils, kCfgRecvMaxMessageSize, kBufBytes);
  }

  const uint64_t self_id = g_api.User_GetSteamID(g_user);
  const char* persona = g_friends ? g_api.Friends_GetPersonaName(g_friends) : "";
  g_self_id.store(self_id, std::memory_order_relaxed);
  g_host.store(host, std::memory_order_relaxed);
  g_lobby_id.store(0, std::memory_order_relaxed);
  g_peer_id.store(0, std::memory_order_relaxed);
  g_lobby_created_handled = false;
  g_join_in_flight = false;
  g_last_friend_scan_ms = 0;
  g_last_skipped_lobby = 0;
  g_auto_join = (!host && join_lobby_id == 0);
  g_active.store(true, std::memory_order_relaxed);

  LOG_I("coop", "COOP STEAM: initialized via %s (Spacewar 480) as '%s' (%llu), role=%s",
        dll_path.c_str(), persona ? persona : "", (unsigned long long)self_id, host ? "HOST" : "JOIN");

  if (host) {
    LOG_I("coop", "COOP STEAM: creating friends-only 2-player lobby...");
    g_api.MM_CreateLobby(g_mm, kLobbyTypeFriendsOnly, 2);
  } else if (join_lobby_id != 0) {
    begin_join_lobby(join_lobby_id);
  } else {
    LOG_I("coop", "COOP STEAM: auto-join mode (scanning friends playing Spacewar and listening for Steam invites)");
    scan_friends_for_lobby();
  }
  return true;
}

void shutdown() {
  if (!g_active.exchange(false)) return;
  const uint64_t peer = g_peer_id.exchange(0);
  if (peer) close_peer_session(peer);
  const uint64_t lid = g_lobby_id.exchange(0);
  if (lid && g_mm) g_api.MM_LeaveLobby(g_mm, lid);
  if (g_friends) g_api.Friends_ClearRichPresence(g_friends);
  if (g_api.Shutdown) g_api.Shutdown();
  if (g_api.dll) FreeLibrary(g_api.dll);
  g_api = SteamApi{};
  g_user = g_mm = g_friends = g_net = g_utils = nullptr;
  LOG_I("coop", "COOP STEAM: shut down cleanly");
}

bool active() { return g_active.load(std::memory_order_relaxed); }
bool is_host() { return g_active.load(std::memory_order_relaxed) && g_host.load(std::memory_order_relaxed); }
uint64_t lobby_id() { return g_lobby_id.load(std::memory_order_relaxed); }
uint64_t peer_steam_id() { return g_peer_id.load(std::memory_order_relaxed); }

std::string status_label() {
  if (!active()) return {};
  std::string err = last_error();
  if (!err.empty()) return "Steam: " + err;
  const uint64_t lid = g_lobby_id.load(std::memory_order_relaxed);
  if (lid) return "Steam lobby " + std::to_string(lid);
  return g_host.load() ? "Steam: creating lobby..." : "Steam: waiting for friend/invite...";
}

std::string last_error() {
  std::lock_guard lk(g_mu);
  return g_last_error;
}

bool send(const void* data, size_t len) {
  if (!active() || !g_net || !data || len == 0) return false;
  const uint64_t peer = g_peer_id.load(std::memory_order_relaxed);
  if (!peer) return false;
  OpaqueIdentity ident{};
  g_api.Identity_SetSteamID64(ident.bytes, peer);
  const int32_t rc = g_api.Net_SendMessageToUser(g_net, ident.bytes, data, uint32_t(len), kSendUnreliableNoNagle,
                                                 kChannelGame);
  return rc == kEResultOK;
}

void pump(const std::function<void(uint64_t sender_steam_id, const uint8_t* data, size_t len)>& on_datagram) {
  if (!active() || !g_net) return;
  run_callbacks();
  if (g_auto_join && g_lobby_id.load(std::memory_order_relaxed) == 0) {
    scan_friends_for_lobby();
  }

  void* msgs[64]{};
  const int32_t count = g_api.Net_ReceiveMessagesOnChannel(g_net, kChannelGame, msgs, 64);
  for (int32_t i = 0; i < count; ++i) {
    auto* raw = reinterpret_cast<const uint8_t*>(msgs[i]);
    if (!raw) continue;
    // SteamNetworkingMessage_t on Windows x64:
    //   offset 0:  void* m_pData (8 bytes)
    //   offset 8:  int32 m_cbSize (4 bytes)
    //   offset 16: SteamNetworkingIdentity m_identityPeer (136 bytes)
    const uint8_t* data = nullptr;
    int32_t size = 0;
    std::memcpy(&data, raw + 0, sizeof data);
    std::memcpy(&size, raw + 8, sizeof size);
    const uint64_t sender = g_api.Identity_GetSteamID64(raw + 16);

    if (data && size > 0 && sender && sender != g_self_id.load(std::memory_order_relaxed)) {
      bool allowed = false;
      if (g_host.load(std::memory_order_relaxed)) {
        if (is_lobby_member(sender)) {
          if (g_peer_id.load(std::memory_order_relaxed) != sender) set_peer(sender);
          allowed = true;
        }
      } else {
        const uint64_t host_id = g_peer_id.load(std::memory_order_relaxed);
        allowed = (host_id != 0 && sender == host_id);
      }
      if (allowed && on_datagram) {
        on_datagram(sender, data, size_t(size));
      }
    }
    g_api.Net_MessageRelease(msgs[i]);
  }
}

bool take_peer_changed() { return g_peer_changed.exchange(false, std::memory_order_relaxed); }

}  // namespace rt::game::coopsteam
