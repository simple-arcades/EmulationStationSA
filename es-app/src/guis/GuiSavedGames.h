// ============================================================================
//  GuiSavedGames.h
//
//  Dialog that lists all save states for a specific ROM.
//  Opened from the Select menu on any non-savestates system when saves exist.
//  Shows each save slot with screenshot, timestamp, and allows launching
//  or deleting individual saves.
//
//  Visible in kiosk mode (no isUIModeFull() gating).
// ============================================================================
#pragma once
#ifndef ES_APP_GUIS_GUI_SAVED_GAMES_H
#define ES_APP_GUIS_GUI_SAVED_GAMES_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "SaveStateDeleteHelper.h"

class SystemData;
class Window;

class GuiSavedGames : public GuiComponent
{
public:
	// romPath: full path to the source ROM (e.g. /home/pi/RetroPie/roms/snes/Super Mario World.sfc)
	// romName: display name of the ROM (e.g. "Super Mario World [US]")
	GuiSavedGames(Window* window, const std::string& romPath, const std::string& romName);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

private:
	void populateList();
	void openPreview(const SaveStateDeleteHelper::SaveEntryInfo& entry);
	void launchSave(const SaveStateDeleteHelper::SaveEntryInfo& entry);
	void deleteSave(const SaveStateDeleteHelper::SaveEntryInfo& entry);

	MenuComponent mMenu;
	std::string mRomPath;
	std::string mRomName;
	std::vector<SaveStateDeleteHelper::SaveEntryInfo> mSaves;
};

#endif // ES_APP_GUIS_GUI_SAVED_GAMES_H
