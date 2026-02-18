// ============================================================================
//  GuiPerSystemOverrides.cpp
//
//  Scrollable list of systems with mode dropdowns.
//  Each system shows: DEFAULT / BUILT-INS FIRST / EXTERNAL TAKEOVER / EXTERNAL ONLY
//  "DEFAULT" means no override — uses the global default mode.
// ============================================================================
#include "guis/GuiPerSystemOverrides.h"
#include "SAStyle.h"

#include "guis/GuiMsgBox.h"
#include "SystemData.h"
#include "Window.h"

#include <algorithm>

// Systems to exclude from the override list
static bool isExcludedSystem(const std::string& name)
{
	return (name == "savestates" || name == "retropie" || name == "settings");
}

// ============================================================================
//  Constructor
// ============================================================================

GuiPerSystemOverrides::GuiPerSystemOverrides(Window* window,
	const std::string& defaultMode,
	const std::map<std::string, std::string>& currentOverrides,
	std::function<void(const std::map<std::string, std::string>&)> onSave)
	: GuiComponent(window),
	  mMenu(window, "PER-SYSTEM OVERRIDES"),
	  mDefaultMode(defaultMode),
	  mOnSave(onSave)
{
	addChild(&mMenu);

	std::string defaultLabel = "DEFAULT (" + getModeLabel(mDefaultMode) + ")";

	// Build a sorted list of installed systems
	struct SysPair {
		std::string name;
		std::string label;
	};
	std::vector<SysPair> sortedSystems;

	for (auto it = SystemData::sSystemVector.cbegin();
	     it != SystemData::sSystemVector.cend(); it++)
	{
		SystemData* sys = *it;
		std::string name = sys->getName();

		if (isExcludedSystem(name))
			continue;

		// Skip ALL collections (Favorites, Last Played, custom collections)
		if (sys->isCollection())
			continue;

		SysPair sp;
		sp.name = name;
		sp.label = sys->getFullName();
		sortedSystems.push_back(sp);
	}

	std::sort(sortedSystems.begin(), sortedSystems.end(),
		[](const SysPair& a, const SysPair& b) {
			return a.label < b.label;
		});

	// Build rows
	for (const auto& sys : sortedSystems)
	{
		auto selector = std::make_shared< OptionListComponent<std::string> >(
			mWindow, sys.label, false);

		// What is this system currently set to?
		std::string currentValue = "DEFAULT";
		auto it = currentOverrides.find(sys.name);
		if (it != currentOverrides.end() && !it->second.empty())
			currentValue = it->second;

		selector->add(defaultLabel,          "DEFAULT",            currentValue == "DEFAULT");
		selector->add("BUILT-INS FIRST",     "BUILTIN_FIRST",     currentValue == "BUILTIN_FIRST");
		selector->add("EXTERNAL TAKEOVER",   "EXTERNAL_TAKEOVER", currentValue == "EXTERNAL_TAKEOVER");
		selector->add("EXTERNAL ONLY",       "EXTERNAL_ONLY",     currentValue == "EXTERNAL_ONLY");

		mMenu.addWithLabel(sys.label, selector);

		SystemRow row;
		row.systemName = sys.name;
		row.systemLabel = sys.label;
		row.selector = selector;
		row.originalValue = currentValue;
		mRows.push_back(row);
	}

	mMenu.addButton("SAVE", "save", [this]() { save(); });
	mMenu.addButton("BACK", "back", [this]() { delete this; });

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition(
		(Renderer::getScreenWidth() - mMenu.getSize().x()) / 2,
		Renderer::getScreenHeight() * 0.15f);
}

// ============================================================================
//  save — collect overrides and pass back to parent
// ============================================================================

void GuiPerSystemOverrides::save()
{
	std::map<std::string, std::string> overrides;

	for (const auto& row : mRows)
	{
		std::string selected = row.selector->getSelected();
		if (selected != "DEFAULT")
		{
			overrides[row.systemName] = selected;
		}
		// If DEFAULT, we simply don't add it (no override)
	}

	mOnSave(overrides);

	mWindow->pushGui(new GuiMsgBox(mWindow,
		"PER-SYSTEM OVERRIDES SAVED.",
		"OK", [this]() { delete this; }));
}

// ============================================================================
//  hasUnsavedChanges
// ============================================================================

bool GuiPerSystemOverrides::hasUnsavedChanges()
{
	for (const auto& row : mRows)
	{
		if (row.selector->getSelected() != row.originalValue)
			return true;
	}
	return false;
}

// ============================================================================
//  getModeLabel
// ============================================================================

std::string GuiPerSystemOverrides::getModeLabel(const std::string& modeId)
{
	if (modeId == "BUILTIN_FIRST")     return "BUILT-INS FIRST";
	if (modeId == "EXTERNAL_TAKEOVER") return "EXTERNAL TAKEOVER";
	if (modeId == "EXTERNAL_ONLY")     return "EXTERNAL ONLY";
	return modeId;
}

// ============================================================================
//  Input / Help
// ============================================================================

bool GuiPerSystemOverrides::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value)
	{
		if (hasUnsavedChanges())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow,
				"YOU HAVE UNSAVED CHANGES.\n\nDO YOU WANT TO SAVE?",
				"YES", [this]() { save(); },
				"NO", [this]() { delete this; }));
		}
		else
		{
			delete this;
		}
		return true;
	}

	return mMenu.input(config, input);
}

std::vector<HelpPrompt> GuiPerSystemOverrides::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}
