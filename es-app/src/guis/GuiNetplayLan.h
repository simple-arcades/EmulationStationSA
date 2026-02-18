#pragma once
#ifndef ES_APP_GUIS_GUI_NETPLAY_LAN_H
#define ES_APP_GUIS_GUI_NETPLAY_LAN_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "NetplayCore.h"

#include <vector>
#include <string>

// ============================================================================
//  GuiNetplayLan
//
//  Listens for LAN broadcast announcements from other Simple Arcades
//  cabinets that are hosting games. Uses the same Python UDP listener
//  as netplay_arcade.sh.
//
//  Shows a list of discovered sessions, matches them against local
//  gamelists, and lets the user join.
// ============================================================================

struct LanSession
{
	std::string ip;
	std::string port;
	std::string hostName;
	std::string systemName;
	std::string gameName;
	std::string romFile;      // The ROM filename being hosted
	std::string coreFile;     // The core the host is using

	// Local match info
	bool hasLocalMatch;
	std::string localCorePath;
	std::string localConfigPath;
	std::string localRomPath;
	std::string localSystemName;
	NetplaySafety safety;
};

class GuiNetplayLan : public GuiComponent
{
public:
	GuiNetplayLan(Window* window);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	void discoverAndBuild();
	void buildSessionList(const std::string& rawTsv);
	void joinSession(const LanSession& session);
	bool findLocalMatch(LanSession& session);

	MenuComponent mMenu;
	std::vector<LanSession> mSessions;
};

#endif // ES_APP_GUIS_GUI_NETPLAY_LAN_H
