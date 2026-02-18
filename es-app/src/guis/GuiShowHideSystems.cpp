// ============================================================================
//  GuiShowHideSystems.cpp
//
//  Shows a list of all systems with ON/OFF toggles.
//  Protected systems are excluded from the list entirely.
//  On save, writes the hidden list to Settings and reloads the carousel.
// ============================================================================
#include "guis/GuiShowHideSystems.h"
#include "SAStyle.h"

#include "components/SwitchComponent.h"
#include "guis/GuiMsgBox.h"
#include "views/ViewController.h"
#include "SystemData.h"
#include "Settings.h"
#include "Window.h"
#include "Log.h"

#include <set>
#include <sstream>

// Systems that cannot be hidden
static bool isProtectedSystem(const std::string& name)
{
	return (name == "savestates" || name == "retropie" || name == "settings");
}

// ============================================================================
//  Constructor — build the toggle list
// ============================================================================
GuiShowHideSystems::GuiShowHideSystems(Window* window)
	: GuiComponent(window),
	  mMenu(window, "SHOW / HIDE SYSTEMS")
{
	// Parse current hidden systems from settings
	std::set<std::string> hiddenSet;
	{
		std::string hiddenStr = Settings::getInstance()->getString("HiddenSystems");
		if (!hiddenStr.empty())
		{
			std::istringstream ss(hiddenStr);
			std::string token;
			while (std::getline(ss, token, ';'))
			{
				if (!token.empty())
					hiddenSet.insert(token);
			}
		}
	}

	// Build a row for each non-protected system
	for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
	{
		SystemData* sys = *it;
		std::string name = sys->getName();

		// Skip protected systems — they're always visible
		if (isProtectedSystem(name))
			continue;

		// Skip collection systems that have no games (they won't show anyway)
		if (sys->isCollection() && !sys->isVisible())
			continue;

		// Create toggle: ON = visible (not hidden)
		auto toggle = std::make_shared<SwitchComponent>(mWindow);
		bool isHidden = hiddenSet.count(name) > 0;
		toggle->setState(!isHidden);

		// Use the system's full display name
		std::string displayName = sys->getFullName();

		mMenu.addWithLabel(displayName, toggle);

		SystemToggle st;
		st.system = sys;
		st.toggle = toggle;
		st.originalState = !isHidden;
		mToggles.push_back(st);
	}

	// Add SAVE button
	mMenu.addButton("SAVE", "SAVE", [this]() { save(); });

	addChild(&mMenu);
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2, (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  hasUnsavedChanges — compare current toggle states to originals
// ============================================================================
bool GuiShowHideSystems::hasUnsavedChanges()
{
	for (const auto& st : mToggles)
	{
		if (st.toggle->getState() != st.originalState)
			return true;
	}
	return false;
}

// ============================================================================
//  save — collect toggled-off systems and write to Settings
// ============================================================================
void GuiShowHideSystems::save()
{
	// Build semicolon-delimited string of hidden system names
	std::string hiddenStr;
	bool anyChanged = false;

	// Parse old hidden set for comparison
	std::set<std::string> oldHiddenSet;
	{
		std::string oldStr = Settings::getInstance()->getString("HiddenSystems");
		if (!oldStr.empty())
		{
			std::istringstream ss(oldStr);
			std::string token;
			while (std::getline(ss, token, ';'))
			{
				if (!token.empty())
					oldHiddenSet.insert(token);
			}
		}
	}

	std::set<std::string> newHiddenSet;
	for (const auto& st : mToggles)
	{
		if (!st.toggle->getState())  // OFF = hidden
		{
			std::string name = st.system->getName();
			newHiddenSet.insert(name);

			if (!hiddenStr.empty())
				hiddenStr += ";";
			hiddenStr += name;
		}
	}

	// Check if anything actually changed
	if (newHiddenSet != oldHiddenSet)
		anyChanged = true;

	if (!anyChanged)
	{
		delete this;
		return;
	}

	// Save to settings
	Settings::getInstance()->setString("HiddenSystems", hiddenStr);
	Settings::getInstance()->saveFile();

	LOG(LogInfo) << "GuiShowHideSystems: Hidden systems set to: "
		<< (hiddenStr.empty() ? "(none)" : hiddenStr);

	// Close this dialog first
	Window* window = mWindow;
	delete this;

	// Reload the system carousel to apply changes
	ViewController::get()->ReloadAndGoToStart();
}

// ============================================================================
//  Input handling
// ============================================================================
bool GuiShowHideSystems::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value)
	{
		// Check if any toggles changed from the original state
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

HelpStyle GuiShowHideSystems::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	return style;
}

std::vector<HelpPrompt> GuiShowHideSystems::getHelpPrompts()
{
	auto prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}
