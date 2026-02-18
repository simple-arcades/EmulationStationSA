#include "guis/GuiControllerSettings.h"
#include "guis/GuiPerSystemOverrides.h"
#include "SAStyle.h"
#include "views/UIModeController.h"

#include "components/SwitchComponent.h"
#include "components/OptionListComponent.h"
#include "components/TextComponent.h"
#include "components/ComponentList.h"
#include "guis/GuiMsgBox.h"
#include "SystemData.h"
#include "Window.h"
#include "Log.h"

#include <fstream>
#include <sstream>
#include <algorithm>

// ============================================================================
//  Static Data
// ============================================================================

const std::string GuiControllerSettings::BEHAVIOR_CONF =
	"/home/pi/simplearcades/config/controller_priority/controller_behavior.conf";

const std::string GuiControllerSettings::HARDWARE_CONF =
	"/home/pi/simplearcades/config/controller_priority/cabinet_hardware.conf";

const std::vector<GuiControllerSettings::ModeInfo> GuiControllerSettings::MODES = {
	{ "BUILTIN_FIRST",     "BUILT-INS FIRST",     "Built-in controls are P1-P4, externals expand" },
	{ "EXTERNAL_TAKEOVER", "EXTERNAL TAKEOVER",    "External replaces built-in per station" },
	{ "EXTERNAL_ONLY",     "EXTERNAL ONLY",        "Only external controllers work" }
};

// ============================================================================
//  Constructor
// ============================================================================

GuiControllerSettings::GuiControllerSettings(Window* window) :
	GuiComponent(window),
	mMenu(window, "CONTROLLER SETTINGS"),
	mCabinetPlayers(2)
{
	addChild(&mMenu);

	loadConfig();

	// Store originals for unsaved-changes detection
	mOriginalDefaultMode = mDefaultMode;
	mOriginalOverrides = mOverrides;

	buildMenu();

	mMenu.setPosition(
		(Renderer::getScreenWidth() - mMenu.getSize().x()) / 2,
		Renderer::getScreenHeight() * 0.15f);
}

// ============================================================================
//  loadConfig — parse controller_behavior.conf
// ============================================================================

void GuiControllerSettings::loadConfig()
{
	mDefaultMode = "BUILTIN_FIRST";
	mOverrides.clear();

	// Read cabinet players from hardware conf
	{
		std::ifstream f(HARDWARE_CONF);
		if (f.is_open())
		{
			std::string line;
			while (std::getline(f, line))
			{
				// Skip comments and empty lines
				if (line.empty() || line[0] == '#')
					continue;

				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;

				std::string key = line.substr(0, eq);
				std::string val = line.substr(eq + 1);

				// Strip quotes
				if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
					val = val.substr(1, val.size() - 2);

				if (key == "CABINET_PLAYERS")
				{
					try { mCabinetPlayers = std::stoi(val); }
					catch (...) { mCabinetPlayers = 2; }
				}
			}
		}
	}

	// Read behavior conf
	std::ifstream f(BEHAVIOR_CONF);
	if (!f.is_open())
	{
		LOG(LogWarning) << "GuiControllerSettings: could not open " << BEHAVIOR_CONF;
		return;
	}

	std::string line;
	while (std::getline(f, line))
	{
		// Skip comments and empty lines
		if (line.empty() || line[0] == '#')
			continue;

		size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);

		// Strip quotes
		if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
			val = val.substr(1, val.size() - 2);

		if (key == "DEFAULT_MODE")
		{
			mDefaultMode = val;
		}
		else if (key.substr(0, 5) == "MODE_")
		{
			std::string system = key.substr(5);
			if (!val.empty())
				mOverrides[system] = val;
		}
	}
}

// ============================================================================
//  saveConfig — write controller_behavior.conf
// ============================================================================

void GuiControllerSettings::saveConfig()
{
	std::ofstream f(BEHAVIOR_CONF);
	if (!f.is_open())
	{
		LOG(LogError) << "GuiControllerSettings: could not write " << BEHAVIOR_CONF;
		return;
	}

	f << "# Simple Arcades - Controller Behavior Configuration" << std::endl;
	f << "#" << std::endl;
	f << "# Available modes:" << std::endl;
	f << "#   BUILTIN_FIRST     - Built-ins are P1-P4, externals for expansion" << std::endl;
	f << "#   EXTERNAL_TAKEOVER - External replaces built-in if connected" << std::endl;
	f << "#   EXTERNAL_ONLY     - Only external controllers work" << std::endl;
	f << std::endl;
	f << "DEFAULT_MODE=\"" << mDefaultMode << "\"" << std::endl;
	f << std::endl;
	f << "# Per-system overrides:" << std::endl;

	for (const auto& pair : mOverrides)
	{
		f << "MODE_" << pair.first << "=\"" << pair.second << "\"" << std::endl;
	}

	f.close();

	// Update originals
	mOriginalDefaultMode = mDefaultMode;
	mOriginalOverrides = mOverrides;
}

// ============================================================================
//  buildMenu — construct the main menu UI
// ============================================================================

void GuiControllerSettings::buildMenu()
{
	// Note: This is called once from constructor.
	// If overrides change, the count display won't update until re-entering the menu.
	// This is acceptable since save writes to disk and user gets confirmation.

	// --- Default Mode Selector ---
	mDefaultModeSelector = std::make_shared< OptionListComponent<std::string> >(
		mWindow, "DEFAULT MODE", false);

	for (const auto& mode : MODES)
	{
		mDefaultModeSelector->add(mode.label, mode.id, mDefaultMode == mode.id);
	}

	mMenu.addWithLabel("DEFAULT MODE", mDefaultModeSelector);

	// --- Current Cabinet Info (read-only, builder-only) ---
	// Only show in full UI mode — kiosk users don't need to see/change this
	if (UIModeController::getInstance()->isUIModeFull())
	{
		std::string cabinetDesc = std::to_string(mCabinetPlayers) + "-PLAYER CABINET";
		auto cabinetText = std::make_shared<TextComponent>(
			mWindow, cabinetDesc,
			saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR);
		mMenu.addWithLabel("CABINET TYPE", cabinetText);
	}

	// --- Per-System Overrides Summary ---
	std::string overrideDesc;
	if (mOverrides.empty())
		overrideDesc = "NONE";
	else
		overrideDesc = std::to_string(mOverrides.size()) + " OVERRIDE" + (mOverrides.size() > 1 ? "S" : "");

	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(
			mWindow, "PER-SYSTEM OVERRIDES (" + overrideDesc + ")",
			saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.makeAcceptInputHandler([this]() { openPerSystemMenu(); });
		mMenu.addRow(row);
	}

	// --- Help ---
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(
			mWindow, "MODE DESCRIPTIONS",
			saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.makeAcceptInputHandler([this]() { openModeHelp(); });
		mMenu.addRow(row);
	}

	// --- Buttons ---
	mMenu.addButton("SAVE", "save", [this]()
	{
		mDefaultMode = mDefaultModeSelector->getSelected();
		saveConfig();

		mWindow->pushGui(new GuiMsgBox(mWindow,
			"CONTROLLER SETTINGS SAVED.\n\nCHANGES TAKE EFFECT NEXT TIME YOU START A GAME.",
			"OK", [this]() { delete this; }));
	});

	mMenu.addButton("BACK", "back", [this]() { delete this; });
}

// ============================================================================
//  openPerSystemMenu — submenu for managing overrides
// ============================================================================

void GuiControllerSettings::openPerSystemMenu()
{
	// Sync current default mode selection before opening
	mDefaultMode = mDefaultModeSelector->getSelected();

	mWindow->pushGui(new GuiPerSystemOverrides(mWindow,
		mDefaultMode,
		mOverrides,
		[this](const std::map<std::string, std::string>& newOverrides)
		{
			mOverrides = newOverrides;
			// Also write immediately so it's not lost
			saveConfig();
		}));
}

// ============================================================================
//  openModeHelp — display mode descriptions
// ============================================================================

void GuiControllerSettings::openModeHelp()
{
	std::string h;

	h += "BUILT-INS FIRST:\n";
	h += "Built-in controls are P1-P" + std::to_string(mCabinetPlayers) + ".";
	if (mCabinetPlayers == 2)
		h += " Plug in controllers for P3/P4.";
	else
		h += " External controllers are ignored.";

	h += "\n\nEXTERNAL TAKEOVER:\n";
	h += "Plugged-in controllers replace built-ins.";
	if (mCabinetPlayers == 2)
		h += " Built-ins shift to P3/P4.";
	h += " If nothing is plugged in, built-ins are used. Best for console games.";

	h += "\n\nEXTERNAL ONLY:\n";
	h += "Only plugged-in controllers work. Built-ins are disabled. Use for special cases only.";

	mWindow->pushGui(new GuiMsgBox(mWindow, h, "OK", nullptr));
}

// ============================================================================
//  Helpers
// ============================================================================

std::string GuiControllerSettings::getModeLabel(const std::string& modeId)
{
	for (const auto& mode : MODES)
	{
		if (mode.id == modeId)
			return mode.label;
	}
	return modeId;
}

std::string GuiControllerSettings::getSystemLabel(const std::string& systemName)
{
	for (auto it = SystemData::sSystemVector.cbegin();
	     it != SystemData::sSystemVector.cend(); it++)
	{
		if ((*it)->getName() == systemName)
			return (*it)->getFullName();
	}
	return systemName;
}

bool GuiControllerSettings::hasUnsavedChanges()
{
	if (mDefaultModeSelector->getSelected() != mOriginalDefaultMode)
		return true;
	if (mOverrides != mOriginalOverrides)
		return true;
	return false;
}

// ============================================================================
//  Input / Help
// ============================================================================

bool GuiControllerSettings::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value)
	{
		// Sync selector to mDefaultMode before checking changes
		mDefaultMode = mDefaultModeSelector->getSelected();

		if (hasUnsavedChanges())
		{
			mWindow->pushGui(new GuiMsgBox(mWindow,
				"YOU HAVE UNSAVED CHANGES.\n\nDO YOU WANT TO SAVE?",
				"YES", [this]()
				{
					saveConfig();
					delete this;
				},
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

std::vector<HelpPrompt> GuiControllerSettings::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}
