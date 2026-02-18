#pragma once
#ifndef ES_APP_GUIS_GUI_NETPLAY_SETTINGS_H
#define ES_APP_GUIS_GUI_NETPLAY_SETTINGS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"

// ============================================================================
//  GuiNetplaySettings
//
//  Main netplay settings menu accessible from the ES main menu.
//  Provides:
//    - Player name (OSK input)
//    - Browse Online Games (lobby browser — Phase 3)
//    - Browse LAN Games (LAN discovery — Phase 3)
//    - Connection Type (Relay / Direct)
//    - Port
//    - Advanced Options submenu
//    - Restore Defaults
//
//  Follows the same pattern as GuiWifiSettings / GuiBluetoothSettings.
// ============================================================================

class GuiNetplaySettings : public GuiComponent
{
public:
	GuiNetplaySettings(Window* window);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	void buildMenu();
	void rebuildMenu();

	// Menu actions
	void changePlayerName();
	void changeConnectionType();
	void changePort();
	void changeMode();
	void openAdvancedOptions();
	void restoreDefaults();
	void browseOnlineGames();
	void browseLanGames();

	MenuComponent mMenu;
};

#endif // ES_APP_GUIS_GUI_NETPLAY_SETTINGS_H
