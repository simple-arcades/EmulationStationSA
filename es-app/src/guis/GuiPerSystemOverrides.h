#pragma once
#ifndef ES_APP_GUIS_GUI_PER_SYSTEM_OVERRIDES_H
#define ES_APP_GUIS_GUI_PER_SYSTEM_OVERRIDES_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "components/OptionListComponent.h"
#include <string>
#include <vector>
#include <map>

class GuiControllerSettings;

// ============================================================================
//  GuiPerSystemOverrides
//
//  Shows a scrollable list of systems, each with a dropdown:
//    DEFAULT / BUILT-INS FIRST / EXTERNAL TAKEOVER / EXTERNAL ONLY
//
//  On save, writes overrides back to the parent GuiControllerSettings.
// ============================================================================

class GuiPerSystemOverrides : public GuiComponent
{
public:
	GuiPerSystemOverrides(Window* window,
		const std::string& defaultMode,
		const std::map<std::string, std::string>& currentOverrides,
		std::function<void(const std::map<std::string, std::string>&)> onSave);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	MenuComponent mMenu;

	struct SystemRow
	{
		std::string systemName;
		std::string systemLabel;
		std::shared_ptr< OptionListComponent<std::string> > selector;
		std::string originalValue;  // for unsaved-changes detection
	};

	std::vector<SystemRow> mRows;
	std::string mDefaultMode;
	std::function<void(const std::map<std::string, std::string>&)> mOnSave;

	void save();
	bool hasUnsavedChanges();
	std::string getModeLabel(const std::string& modeId);
};

#endif // ES_APP_GUIS_GUI_PER_SYSTEM_OVERRIDES_H
