// Optional Spacewar (AppID 480) Steamworks P2P transport for AC4 co-op.
//
// Dynamically loads steam_api64.dll at runtime (obtained at install time from
// Steamworks.NET releases; never bundled in the repository) and uses only the
// Steamworks flat C API — no SDK headers or import libs required.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace rt::game::coopsteam {

// Initialize Steamworks (AppID 480 / Spacewar).
//   host == true:  create a friends-only 2-player lobby, set rich presence, copy lobby ID to clipboard.
//   host == false: join `join_lobby_id` if non-zero, otherwise scan friends playing Spacewar and
//                  listen for Steam invites / Join Game callbacks.
bool init(bool host, uint64_t join_lobby_id);

// Cleanly leave lobby, clear rich presence, close P2P session, and shut down Steamworks.
void shutdown();

bool active();
bool is_host();

// Current lobby ID (0 until created/joined) and active remote peer SteamID (0 until connected).
uint64_t lobby_id();
uint64_t peer_steam_id();

// Short human-readable status suffix for the window title / logs (e.g. "Steam lobby 10977524...").
std::string status_label();
// Non-empty if an error occurred (e.g. missing steam_api64.dll, Steam not running, wrong lobby).
std::string last_error();

// Send one AC4C datagram to the active peer over ISteamNetworkingMessages (channel 0, UnreliableNoNagle).
bool send(const void* data, size_t len);

// Run Steam manual callback dispatch and drain incoming channel-0 messages, invoking `on_datagram`
// for each message from a valid lobby peer.
void pump(const std::function<void(uint64_t sender_steam_id, const uint8_t* data, size_t len)>& on_datagram);

// True once when the remote peer changes (reconnect / new partner), so coop_net resets seq guards.
bool take_peer_changed();

}  // namespace rt::game::coopsteam
