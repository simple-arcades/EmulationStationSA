#pragma once
#ifndef ES_APP_GUIS_GUI_CONTROLLER_SETTINGS_H
#define ES_APP_GUIS_GUI_CONTROLLER_SETTINGS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "components/OptionListComponent.h"
#include <string>
#include <map>
#include <vector>

// ============================================================================
//  GuiControllerSettings
//
//  ES GUI for managing controller behavior modes.
//  Reads/writes /home/pi/simplearcades/config/controller_priority/controller_behavior.conf
//
//  Features:
//    - Set default behavior mode (BUILTIN_FIRST / EXTERNAL_TAKEOVER / EXTERNAL_ONLY)
//    - Add/remove/change per-system overrides
//    - View summary of current settings
// ============================================================================

class GuiControllerSettings : public GuiComponent
{
public:
	GuiControllerSettings(Window* window);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	// Config paths
	static const std::string BEHAVIOR_CONF;
	static const std::string HARDWARE_CONF;

	// Behavior modes
	struct ModeInfo
	{
		std::string id;       // e.g. "BUILTIN_FIRST"
		std::string label;    // e.g. "BUILT-INS FIRST"
		std::string desc;     // short description for display
	};
	static const std::vector<ModeInfo> MODES;

	// Config state
	std::string mDefaultMode;
	std::map<std::string, std::string> mOverrides;  // system -> mode
	int mCabinetPlayers;

	// UI
	MenuComponent mMenu;
	std::shared_ptr< OptionListComponent<std::string> > mDefaultModeSelector;

	// Load / Save
	void loadConfig();
	void saveConfig();

	// UI builders
	void buildMenu();
	void openPerSystemMenu();
	void openModeHelp();

	// Helpers
	std::string getModeLabel(const std::string& modeId);
	std::string getSystemLabel(const std::string& systemName);
	bool hasUnsavedChanges();
	std::string mOriginalDefaultMode;
	std::map<std::string, std::string> mOriginalOverrides;
};

#endif // ES_APP_GUIS_GUI_CONTROLLER_SETTINGS_H
