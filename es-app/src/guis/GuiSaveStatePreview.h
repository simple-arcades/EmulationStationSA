// ============================================================================
//  GuiSaveStatePreview.h
//
//  A popup dialog that shows a large screenshot of a save state with
//  LOAD, DELETE, and CANCEL buttons. Used by GuiSavedGames when the
//  user selects a save slot.
// ============================================================================
#pragma once
#ifndef ES_APP_GUIS_GUI_SAVE_STATE_PREVIEW_H
#define ES_APP_GUIS_GUI_SAVE_STATE_PREVIEW_H

#include "GuiComponent.h"
#include "components/ComponentGrid.h"
#include "components/NinePatchComponent.h"

class ButtonComponent;
class ImageComponent;
class TextComponent;

class GuiSaveStatePreview : public GuiComponent
{
public:
	GuiSaveStatePreview(Window* window,
	                    const std::string& title,
	                    const std::string& imagePath,
	                    const std::string& detailText,
	                    const std::function<void()>& loadFunc,
	                    const std::function<void()>& deleteFunc);

	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void deleteMeAndCall(const std::function<void()>& func);

	NinePatchComponent mBackground;
	ComponentGrid mGrid;

	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<ImageComponent> mImage;
	std::shared_ptr<TextComponent> mDetail;
	std::shared_ptr<ComponentGrid> mButtonGrid;
	std::vector<std::shared_ptr<ButtonComponent>> mButtons;
	std::function<void()> mCancelFunc;
};

#endif // ES_APP_GUIS_GUI_SAVE_STATE_PREVIEW_H
