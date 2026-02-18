#pragma once
#ifndef ES_APP_GUIS_GUI_NETPLAY_LOBBY_H
#define ES_APP_GUIS_GUI_NETPLAY_LOBBY_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "NetplayCore.h"

#include <vector>
#include <string>

class FileData;

// ============================================================================
//  GuiNetplayLobby
//
//  Fetches the libretro lobby (lobby.libretro.com/list), parses the JSON,
//  matches games against local gamelists, and lets the user join a session.
//
//  Two modes:
//  - Browse all:  filterGame = "" (show all compatible sessions)
//  - Find match:  filterGame = specific ROM name (show only matches for
//                 the game the user was browsing when they hit FIND A MATCH)
// ============================================================================

struct LobbySession
{
	std::string gameName;
	std::string hostName;
	std::string ip;
	std::string port;
	std::string coreName;      // Remote core name from lobby
	std::string remoteFilename; // ROM filename from lobby
	std::string remoteCRC;     // CRC32 from lobby (hex string, e.g. "A23DE148")
	std::string connType;      // "DIRECT" or "RELAY"
	NetplaySafety safety;

	// Local match info (populated if we found the game locally)
	bool hasLocalMatch;
	std::string localCorePath;
	std::string localConfigPath;
	std::string localRomPath;
	std::string localSystemName;
};

class GuiNetplayLobby : public GuiComponent
{
public:
	// filterGame: empty = browse all, non-empty = filter to this game name
	// filterSystem: hint for matching (the ES system name the user was in)
	GuiNetplayLobby(Window* window,
	                 const std::string& filterGame = "",
	                 const std::string& filterSystem = "");

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	void fetchAndBuild();
	void buildSessionList(const std::string& rawJson);
	void joinSession(const LobbySession& session);

	// Try to match a lobby entry against local gamelists
	bool findLocalMatch(LobbySession& session);

	// Load CRC database for a system (from .netplay_crc files)
	void loadCRCDatabase();

	MenuComponent mMenu;
	std::string mFilterGame;
	std::string mFilterSystem;
	std::vector<LobbySession> mSessions;

	// CRC database: maps CRC (uppercase hex) -> {corePath, configPath, romPath, systemName}
	struct CRCEntry {
		std::string corePath;
		std::string configPath;
		std::string romPath;
		std::string systemName;
	};
	std::map<std::string, CRCEntry> mCRCDatabase;
	bool mCRCLoaded;
};

#endif // ES_APP_GUIS_GUI_NETPLAY_LOBBY_H
