// ============================================================================
//  GuiShowHideSystems.h
//
//  Dialog that lets users toggle system visibility on the carousel.
//  Each system gets a toggle switch (ON = visible, OFF = hidden).
//  Protected systems (savestates, settings) cannot be hidden.
//
//  Stores hidden system names in Settings as a semicolon-delimited string
//  ("HiddenSystems"). On save, reloads the system view to apply changes
//  instantly — no reboot needed.
//
//  Accessible in kiosk mode.
// ============================================================================
#pragma once
#ifndef ES_APP_GUIS_GUI_SHOW_HIDE_SYSTEMS_H
#define ES_APP_GUIS_GUI_SHOW_HIDE_SYSTEMS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"

class SwitchComponent;
class SystemData;
class Window;

class GuiShowHideSystems : public GuiComponent
{
public:
	GuiShowHideSystems(Window* window);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	void save();
	bool hasUnsavedChanges();

	MenuComponent mMenu;

	struct SystemToggle
	{
		SystemData* system;
		std::shared_ptr<SwitchComponent> toggle;
		bool originalState;
	};

	std::vector<SystemToggle> mToggles;
};

#endif // ES_APP_GUIS_GUI_SHOW_HIDE_SYSTEMS_H
