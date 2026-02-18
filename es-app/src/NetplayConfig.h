#pragma once
#ifndef ES_APP_NETPLAY_CONFIG_H
#define ES_APP_NETPLAY_CONFIG_H

#include <string>

// ============================================================================
//  NetplayConfig
//
//  Manages persistent netplay settings: nickname, port, connection mode,
//  advanced hosting options, etc.  Reads/writes a simple key=value config
//  file at /home/pi/simplearcades/scripts/netplay/netplay_config.cfg.
//
//  Singleton — accessed via NetplayConfig::get().
// ============================================================================

class NetplayConfig
{
public:
	static NetplayConfig& get();

	// ---- Load / Save ----
	void load();
	void save() const;

	// ---- Basic Settings ----
	std::string nickname;       // Player name visible to others (max 20 chars)
	std::string port;           // Connection port (default "55435")
	std::string mode;           // "online" or "lan"
	std::string onlineMethod;   // "relay" or "direct"

	// ---- Advanced Settings (host-side) ----
	std::string publicAnnounce; // "auto", "true", or "false"
	std::string natTraversal;   // "true" or "false"
	std::string allowSlaves;    // "true" or "false"
	std::string maxConnections; // "2" to "4"
	std::string maxPing;        // "0" = no limit, else ms
	std::string password;       // Game session password
	std::string spectatePassword; // Spectator password

	// ---- Defaults ----
	void resetAdvancedToDefaults();
	void resetAllToDefaults();

	// ---- Helpers ----
	// Get human-readable labels for current settings
	std::string getModeLabel() const;
	std::string getOnlineMethodLabel() const;
	std::string getSubtitleText() const;

	// Sanitize nickname (alphanumeric, spaces, dashes, max 20 chars)
	static std::string sanitizeNickname(const std::string& raw);

	// Config file path
	static std::string getConfigPath();

private:
	NetplayConfig();
	static NetplayConfig* sInstance;
};

#endif // ES_APP_NETPLAY_CONFIG_H
