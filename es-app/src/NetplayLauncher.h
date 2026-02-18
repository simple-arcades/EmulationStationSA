#pragma once
#ifndef ES_APP_NETPLAY_LAUNCHER_H
#define ES_APP_NETPLAY_LAUNCHER_H

#include "NetplayCore.h"
#include <string>

class Window;
class FileData;

// ============================================================================
//  NetplayLauncher
//
//  Handles the full netplay launch lifecycle:
//  1. Writes the safeguard appendconfig (disables saves/pause/FF/etc.)
//  2. Builds the RetroArch command with netplay flags
//  3. Deinits ES, runs RetroArch, reinits ES
//  4. Parses RetroArch log for errors and shows user-friendly messages
//  5. Cleans up temp files
//
//  This bypasses runcommand entirely — we launch RetroArch directly,
//  same as the original netplay_arcade.sh script did, because runcommand's
//  built-in netplay support is too bare-bones for our needs.
// ============================================================================

class NetplayLauncher
{
public:
	// Host a game: launch RetroArch with --host
	// Shows error messages if the launch fails.
	static void launchAsHost(Window* window, FileData* game);

	// Join a game: launch RetroArch with --connect
	// hostIp and hostPort specify where to connect.
	static void launchAsClient(Window* window, FileData* game,
	                            const std::string& hostIp,
	                            const std::string& hostPort);

	// Join by direct IP (when we don't have a FileData — e.g. from lobby
	// with manual game selection). Uses explicit core/config/rom paths.
	static void launchAsClientDirect(Window* window,
	                                  const NetplayGameInfo& info,
	                                  const std::string& hostIp,
	                                  const std::string& hostPort);

private:
	// Write the safeguard appendconfig file
	// Disables save states, pause, FF, rewind, menu toggle, etc.
	// Redirects saves to /dev/shm to protect real save files.
	static void writeSafeguardAppendConfig(const std::string& path,
	                                        const std::string& corePath,
	                                        const std::string& role);

	// Build the full RetroArch launch command
	static std::string buildCommand(const NetplayGameInfo& info,
	                                 const std::string& role,
	                                 const std::string& hostIp = "",
	                                 const std::string& hostPort = "");

	// Run the command (deinit ES, show boot image, execute, reinit ES, update metadata)
	static int executeCommand(Window* window, FileData* game,
	                           const std::string& command,
	                           const std::string& role = "");

	// Parse RetroArch log and show appropriate error message
	static void handlePostLaunch(Window* window,
	                              int exitCode,
	                              const std::string& role,
	                              NetplaySafety safety,
	                              const std::string& hostIp = "",
	                              const std::string& hostPort = "");

	// Clean up temp files
	static void cleanup();

	// LAN broadcaster — starts a background Python process that
	// sends UDP broadcast announcements so other cabinets can find us
	static void startLanBroadcaster(const NetplayGameInfo& info,
	                                 const std::string& gameName);
	static void stopLanBroadcaster();

	// File paths
	static std::string getSafeguardPath();
	static std::string getLogPath();
	static std::string getSaveDirPath();
	static std::string getStateDirPath();
};

#endif // ES_APP_NETPLAY_LAUNCHER_H
