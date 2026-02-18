#include "NetplayCore.h"
#include "FileData.h"
#include "SystemData.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <fstream>
#include <algorithm>

// ============================================================================
//  Core Whitelist
//
//  Maps core .so filenames to their netplay safety level.
//  This is the single source of truth for netplay compatibility.
//
//  OPEN   = Cross-platform safe. Platform-independent serialization,
//           so a Pi can play against a PC.
//  STRICT = Same architecture only. Platform-dependent serialization,
//           so both players must be on identical hardware (Pi-to-Pi).
// ============================================================================

const std::map<std::string, NetplaySafety>& NetplayCore::getCoreWhitelist()
{
	static const std::map<std::string, NetplaySafety> whitelist = {
		// ---- OPEN (cross-platform safe) ----
		{ "fceumm_libretro.so",            NetplaySafety::OPEN },   // NES/Famicom/FDS
		{ "nestopia_libretro.so",          NetplaySafety::OPEN },   // NES (alternative)
		{ "snes9x_libretro.so",            NetplaySafety::OPEN },   // SNES/SFC
		{ "snes9x2005_libretro.so",        NetplaySafety::OPEN },   // SNES (lightweight)
		{ "snes9x2010_libretro.so",        NetplaySafety::OPEN },   // SNES (alternative)
		{ "genesis_plus_gx_libretro.so",   NetplaySafety::OPEN },   // Genesis/MD/SMS/GG/SCD/SG-1000
		{ "picodrive_libretro.so",         NetplaySafety::OPEN },   // Genesis/MD/SCD/32X
		{ "mednafen_pce_fast_libretro.so", NetplaySafety::OPEN },   // PC Engine/TG-16/PCE-CD
		{ "beetle_pce_fast_libretro.so",   NetplaySafety::OPEN },   // PC Engine (alt name)
		{ "gambatte_libretro.so",          NetplaySafety::OPEN },   // Game Boy / Game Boy Color
		{ "stella2014_libretro.so",        NetplaySafety::OPEN },   // Atari 2600
		{ "mednafen_ngp_libretro.so",      NetplaySafety::OPEN },   // Neo Geo Pocket / Color
		{ "beetle_ngp_libretro.so",        NetplaySafety::OPEN },   // Neo Geo Pocket (alt name)
		{ "mednafen_wswan_libretro.so",    NetplaySafety::OPEN },   // WonderSwan / Color
		{ "beetle_wswan_libretro.so",      NetplaySafety::OPEN },   // WonderSwan (alt name)
		{ "mednafen_vb_libretro.so",       NetplaySafety::OPEN },   // Virtual Boy
		{ "beetle_vb_libretro.so",         NetplaySafety::OPEN },   // Virtual Boy (alt name)
		{ "mednafen_supergrafx_libretro.so", NetplaySafety::OPEN }, // SuperGrafx
		{ "beetle_supergrafx_libretro.so", NetplaySafety::OPEN },   // SuperGrafx (alt name)

		// ---- STRICT (same architecture only) ----
		{ "fbneo_libretro.so",             NetplaySafety::STRICT },  // Neo Geo / FBA / Arcade
		{ "mame_libretro.so",              NetplaySafety::STRICT },  // MAME (current)
		{ "mame2003_libretro.so",          NetplaySafety::STRICT },  // MAME 2003
		{ "mame2003_plus_libretro.so",     NetplaySafety::STRICT },  // MAME 2003+
		{ "duckstation_libretro.so",       NetplaySafety::STRICT },  // PlayStation
		{ "swanstation_libretro.so",       NetplaySafety::STRICT },  // PlayStation (SwanStation)
		{ "pcsx_rearmed_libretro.so",      NetplaySafety::STRICT },  // PlayStation (PCSX-ReARMed)
		{ "flycast_libretro.so",           NetplaySafety::STRICT },  // Atomiswave/Naomi
		{ "mgba_libretro.so",              NetplaySafety::STRICT },  // Game Boy Advance
		{ "gpsp_libretro.so",              NetplaySafety::STRICT },  // Game Boy Advance (gpSP)
		{ "vba_next_libretro.so",          NetplaySafety::STRICT },  // Game Boy Advance (VBA-Next)
		{ "bluemsx_libretro.so",           NetplaySafety::STRICT },  // MSX
		{ "prosystem_libretro.so",         NetplaySafety::STRICT },  // Atari 7800
		{ "neocd_libretro.so",             NetplaySafety::STRICT },  // Neo Geo CD
	};
	return whitelist;
}

// ============================================================================
//  getDefaultEmulatorInfo
//
//  Reads /opt/retropie/configs/<systemName>/emulators.cfg to find:
//  1. The "default" emulator name
//  2. That emulator's launch command
//  3. Extracts the core path (-L ...) and config path (--config ...)
//
//  Returns true if successful, populating corePath and configPath.
// ============================================================================

bool NetplayCore::getDefaultEmulatorInfo(const std::string& systemName,
                                          std::string& corePath,
                                          std::string& configPath)
{
	std::string cfgPath = "/opt/retropie/configs/" + systemName + "/emulators.cfg";

	if (!Utils::FileSystem::exists(cfgPath))
	{
		LOG(LogDebug) << "NetplayCore: No emulators.cfg for " << systemName;
		return false;
	}

	std::ifstream file(cfgPath);
	if (!file.is_open())
		return false;

	// First pass: find the default emulator name
	std::string defaultEmu;
	std::map<std::string, std::string> emulators;
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

		// Strip surrounding quotes
		if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
			val = val.substr(1, val.size() - 2);

		if (key == "default")
			defaultEmu = val;
		else
			emulators[key] = val;
	}

	if (defaultEmu.empty())
	{
		LOG(LogDebug) << "NetplayCore: No default emulator in " << cfgPath;
		return false;
	}

	// Find the launch command for the default emulator
	auto it = emulators.find(defaultEmu);
	if (it == emulators.end())
	{
		LOG(LogDebug) << "NetplayCore: Default emulator '" << defaultEmu
		              << "' not found in " << cfgPath;
		return false;
	}

	const std::string& cmd = it->second;

	// Extract core path: everything after "-L " up to the next space or quote
	size_t lPos = cmd.find("-L ");
	if (lPos == std::string::npos)
		return false;

	lPos += 3; // skip "-L "
	size_t lEnd = cmd.find(' ', lPos);
	if (lEnd == std::string::npos)
		lEnd = cmd.size();
	corePath = cmd.substr(lPos, lEnd - lPos);

	// Extract config path: everything after "--config " up to the next space or quote
	size_t cPos = cmd.find("--config ");
	if (cPos == std::string::npos)
	{
		// Fallback: construct from system name
		configPath = "/opt/retropie/configs/" + systemName + "/retroarch.cfg";
	}
	else
	{
		cPos += 9; // skip "--config "
		size_t cEnd = cmd.find(' ', cPos);
		if (cEnd == std::string::npos)
			cEnd = cmd.size();
		configPath = cmd.substr(cPos, cEnd - cPos);
	}

	return true;
}

// ============================================================================
//  extractCoreFilename
//
//  Given a full path like:
//    /opt/retropie/libretrocores/lr-fceumm/fceumm_libretro.so
//  Returns: fceumm_libretro.so
//
//  Also handles just a filename, or a lobby's "core_name" field
//  by normalizing it.
// ============================================================================

std::string NetplayCore::extractCoreFilename(const std::string& fullPath)
{
	size_t lastSlash = fullPath.rfind('/');
	if (lastSlash != std::string::npos)
		return fullPath.substr(lastSlash + 1);
	return fullPath;
}

// ============================================================================
//  isGameNetplayCompatible
// ============================================================================

bool NetplayCore::isGameNetplayCompatible(FileData* game)
{
	if (!game || game->getType() != GAME)
		return false;

	// Check player count first (cheap check)
	std::string playersStr = game->metadata.get("players");
	int maxPlayers = parsePlayerCount(playersStr);
	if (maxPlayers < 2)
		return false;

	// Get the system name
	std::string systemName = game->getSystem()->getName();

	// Skip non-game systems
	if (systemName == "retropie" || systemName == "savestates")
		return false;

	// Get the default core for this system
	std::string corePath, configPath;
	if (!getDefaultEmulatorInfo(systemName, corePath, configPath))
		return false;

	// Check if the core is in the whitelist
	std::string coreFilename = extractCoreFilename(corePath);
	const auto& wl = getCoreWhitelist();
	return wl.find(coreFilename) != wl.end();
}

// ============================================================================
//  getGameInfo
// ============================================================================

NetplayGameInfo NetplayCore::getGameInfo(FileData* game)
{
	NetplayGameInfo info;
	info.safety = NetplaySafety::NONE;

	if (!game || game->getType() != GAME)
		return info;

	info.systemName = game->getSystem()->getName();
	info.romPath = game->getPath();

	// Get core and config from emulators.cfg
	if (!getDefaultEmulatorInfo(info.systemName, info.corePath, info.configPath))
		return info;

	// Look up safety level
	std::string coreFilename = extractCoreFilename(info.corePath);
	const auto& wl = getCoreWhitelist();
	auto it = wl.find(coreFilename);
	if (it != wl.end())
		info.safety = it->second;

	return info;
}

// ============================================================================
//  getSafetyForCore — used when matching lobby entries
// ============================================================================

NetplaySafety NetplayCore::getSafetyForCore(const std::string& coreFilename)
{
	// First try exact match
	const auto& wl = getCoreWhitelist();
	auto it = wl.find(coreFilename);
	if (it != wl.end())
		return it->second;

	// Fuzzy match: normalize and check for known substrings
	std::string norm = Utils::String::toLower(coreFilename);
	std::replace(norm.begin(), norm.end(), '-', '_');
	std::replace(norm.begin(), norm.end(), ' ', '_');

	static const std::vector<std::string> openCores = {
		"snes9x", "fceumm", "genesis_plus_gx", "picodrive",
		"mednafen_pce_fast"
	};

	for (const auto& safe : openCores)
	{
		if (norm.find(safe) != std::string::npos)
			return NetplaySafety::OPEN;
	}

	// Default to STRICT for unknown cores (conservative)
	return NetplaySafety::STRICT;
}

// ============================================================================
//  getSafetyLabel
// ============================================================================

std::string NetplayCore::getSafetyLabel(NetplaySafety safety)
{
	switch (safety)
	{
		case NetplaySafety::OPEN:   return "COMPATIBLE";
		case NetplaySafety::STRICT: return "SAME HARDWARE ONLY";
		default:                    return "NOT SUPPORTED";
	}
}

// ============================================================================
//  parsePlayerCount
//
//  Handles: "2", "1-2", "1-4", empty string, etc.
// ============================================================================

int NetplayCore::parsePlayerCount(const std::string& playersStr)
{
	if (playersStr.empty())
		return 1;

	// If it contains a dash, take the second number (max players)
	size_t dash = playersStr.find('-');
	if (dash != std::string::npos)
	{
		std::string maxStr = playersStr.substr(dash + 1);
		try { return std::stoi(maxStr); }
		catch (...) { return 1; }
	}

	// Plain number
	try { return std::stoi(playersStr); }
	catch (...) { return 1; }
}
