#include "NetplayConfig.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <fstream>
#include <sstream>
#include <algorithm>

// ============================================================================
//  Singleton
// ============================================================================

NetplayConfig* NetplayConfig::sInstance = nullptr;

NetplayConfig& NetplayConfig::get()
{
	if (!sInstance)
	{
		sInstance = new NetplayConfig();
		sInstance->load();
	}
	return *sInstance;
}

// ============================================================================
//  Constructor — sets defaults
// ============================================================================

NetplayConfig::NetplayConfig()
{
	resetAllToDefaults();
}

// ============================================================================
//  Config file path
// ============================================================================

std::string NetplayConfig::getConfigPath()
{
	return "/home/pi/simplearcades/scripts/netplay/netplay_config.cfg";
}

// ============================================================================
//  Defaults
// ============================================================================

void NetplayConfig::resetAllToDefaults()
{
	nickname        = "Player";
	port            = "55435";
	mode            = "online";
	onlineMethod    = "relay";
	resetAdvancedToDefaults();
}

void NetplayConfig::resetAdvancedToDefaults()
{
	publicAnnounce   = "auto";
	natTraversal     = "false";
	allowSlaves      = "true";
	maxConnections   = "2";
	maxPing          = "0";
	password         = "";
	spectatePassword = "";
}

// ============================================================================
//  Load
// ============================================================================

void NetplayConfig::load()
{
	std::string path = getConfigPath();

	if (!Utils::FileSystem::exists(path))
	{
		LOG(LogInfo) << "NetplayConfig: No config file found, using defaults";
		return;
	}

	std::ifstream file(path);
	if (!file.is_open())
	{
		LOG(LogWarning) << "NetplayConfig: Could not open " << path;
		return;
	}

	std::string line;
	while (std::getline(file, line))
	{
		// Skip comments and blank lines
		if (line.empty() || line[0] == '#')
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);

		// Trim whitespace
		key.erase(0, key.find_first_not_of(" \t"));
		key.erase(key.find_last_not_of(" \t") + 1);
		val.erase(0, val.find_first_not_of(" \t"));
		val.erase(val.find_last_not_of(" \t") + 1);

		// Strip surrounding quotes (shell-style quoting from old config)
		if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'')
			val = val.substr(1, val.size() - 2);
		else if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
			val = val.substr(1, val.size() - 2);

		// Map keys to members
		if      (key == "SAVED_NICKNAME")       nickname        = val;
		else if (key == "SAVED_PORT")           port            = val;
		else if (key == "SAVED_MODE")           mode            = val;
		else if (key == "SAVED_ONLINE_METHOD")  onlineMethod    = val;
		else if (key == "ADV_PUBLIC_ANNOUNCE")   publicAnnounce  = val;
		else if (key == "ADV_NAT_TRAVERSAL")    natTraversal    = val;
		else if (key == "ADV_ALLOW_SLAVES")     allowSlaves     = val;
		else if (key == "ADV_MAX_CONNECTIONS")   maxConnections  = val;
		else if (key == "ADV_MAX_PING")         maxPing         = val;
		else if (key == "ADV_PASSWORD")          password        = val;
		else if (key == "ADV_SPECTATE_PASSWORD") spectatePassword = val;
	}

	// Sanitize loaded values
	nickname = sanitizeNickname(nickname);
	if (nickname.empty()) nickname = "Player";

	if (port.empty()) port = "55435";
	if (mode != "online" && mode != "lan") mode = "online";
	if (onlineMethod != "direct" && onlineMethod != "relay") onlineMethod = "relay";
	if (publicAnnounce != "auto" && publicAnnounce != "true" && publicAnnounce != "false")
		publicAnnounce = "auto";
	if (natTraversal != "true" && natTraversal != "false") natTraversal = "false";
	if (allowSlaves != "true" && allowSlaves != "false") allowSlaves = "true";

	LOG(LogInfo) << "NetplayConfig: Loaded — nickname=" << nickname
	             << " mode=" << mode << " method=" << onlineMethod;
}

// ============================================================================
//  Save
// ============================================================================

void NetplayConfig::save() const
{
	std::string path = getConfigPath();

	// Ensure directory exists
	std::string dir = Utils::FileSystem::getParent(path);
	if (!dir.empty())
		Utils::FileSystem::createDirectory(dir);

	std::ofstream file(path);
	if (!file.is_open())
	{
		LOG(LogError) << "NetplayConfig: Could not write to " << path;
		return;
	}

	file << "# Simple Arcades Netplay settings (auto-saved)\n";
	file << "# This file is safe to delete - defaults will be recreated.\n\n";

	file << "SAVED_NICKNAME="       << nickname        << "\n";
	file << "SAVED_PORT="           << port            << "\n\n";

	file << "SAVED_MODE="           << mode            << "\n";
	file << "SAVED_ONLINE_METHOD="  << onlineMethod    << "\n\n";

	file << "ADV_PUBLIC_ANNOUNCE="   << publicAnnounce  << "\n";
	file << "ADV_NAT_TRAVERSAL="    << natTraversal    << "\n";
	file << "ADV_ALLOW_SLAVES="     << allowSlaves     << "\n";
	file << "ADV_MAX_CONNECTIONS="   << maxConnections  << "\n";
	file << "ADV_MAX_PING="         << maxPing         << "\n";
	file << "ADV_PASSWORD="          << password        << "\n";
	file << "ADV_SPECTATE_PASSWORD=" << spectatePassword << "\n";

	LOG(LogInfo) << "NetplayConfig: Saved to " << path;
}

// ============================================================================
//  Helpers
// ============================================================================

std::string NetplayConfig::getModeLabel() const
{
	if (mode == "lan")
		return "LAN (Same Network)";
	return "Online (Internet)";
}

std::string NetplayConfig::getOnlineMethodLabel() const
{
	if (onlineMethod == "direct")
		return "Direct Connection";
	return "Relay Server";
}

std::string NetplayConfig::getSubtitleText() const
{
	// e.g. "PLAYER: Michael · RELAY · ONLINE"
	std::string result = "PLAYER: " + Utils::String::toUpper(nickname);

	if (mode == "online")
		result += " · " + Utils::String::toUpper(onlineMethod == "relay" ? "RELAY" : "DIRECT");

	result += " · " + Utils::String::toUpper(mode == "lan" ? "LAN" : "ONLINE");

	return result;
}

std::string NetplayConfig::sanitizeNickname(const std::string& raw)
{
	std::string result;
	result.reserve(raw.size());

	for (char c : raw)
	{
		if (std::isalnum(c) || c == ' ' || c == '-' || c == '_')
			result += c;
	}

	// Trim leading/trailing whitespace
	size_t start = result.find_first_not_of(' ');
	if (start == std::string::npos)
		return "";
	size_t end = result.find_last_not_of(' ');
	result = result.substr(start, end - start + 1);

	// Max 20 characters
	if (result.size() > 20)
		result = result.substr(0, 20);

	return result;
}
