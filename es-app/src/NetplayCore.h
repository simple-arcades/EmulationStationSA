#pragma once
#ifndef ES_APP_NETPLAY_CORE_H
#define ES_APP_NETPLAY_CORE_H

#include <string>
#include <vector>
#include <map>

class FileData;

// ============================================================================
//  NetplayCore
//
//  Determines netplay compatibility by:
//  1. Reading the system's emulators.cfg to find the default core .so
//  2. Checking that .so against a whitelist of netplay-capable cores
//  3. Verifying the game has players >= 2
//
//  The whitelist is by core filename (e.g. "fceumm_libretro.so"), NOT by
//  system name. This automatically handles LG variants, system aliases
//  (genesis/megadrive, snes/sfc), and any new systems that share cores.
// ============================================================================

enum class NetplaySafety
{
	NONE,    // No netplay support
	OPEN,    // Cross-platform safe (Pi can play vs PC)
	STRICT   // Same architecture only (Pi-to-Pi)
};

struct NetplayGameInfo
{
	std::string corePath;    // e.g. "/opt/simplearcades/cores/nes/fceumm_libretro.so"
	std::string configPath;  // e.g. "/home/pi/simplearcades/config/retroarch/systems/nes.cfg"
	std::string romPath;     // Full path to the ROM
	std::string systemName;  // e.g. "nes"
	NetplaySafety safety;    // OPEN, STRICT, or NONE
};

class NetplayCore
{
public:
	// Check if a game supports netplay.
	// Reads emulators.cfg for the system, checks core against whitelist,
	// and verifies players >= 2.
	static bool isGameNetplayCompatible(FileData* game);

	// Get full game info needed for launching (core path, config, safety).
	// Returns a populated struct, or one with safety=NONE if not compatible.
	static NetplayGameInfo getGameInfo(FileData* game);

	// Get the safety level for a core .so filename (used for lobby matching)
	static NetplaySafety getSafetyForCore(const std::string& coreFilename);

	// Human-readable safety label
	static std::string getSafetyLabel(NetplaySafety safety);

	// Parse player count from metadata ("2", "1-2", "1-4", etc.)
	static int parsePlayerCount(const std::string& playersStr);

	// Extract core .so filename from a full path or launch command
	static std::string extractCoreFilename(const std::string& fullPath);

private:
	// The whitelist: core .so filename -> safety level
	static const std::map<std::string, NetplaySafety>& getCoreWhitelist();

	// Parse emulators.cfg for a system to get the default emulator's
	// launch command, then extract core path and config path from it.
	static bool getDefaultEmulatorInfo(const std::string& systemName,
	                                    std::string& corePath,
	                                    std::string& configPath);
};

#endif // ES_APP_NETPLAY_CORE_H
