// ============================================================================
//  GuiImagePopup.h
//
//  A framed popup dialog that shows:
//    Row 0: Title text
//    Row 1: Image (scaled to fit within the popup)
//    Row 2: Detail text
//    Row 3: CLOSE button
//
//  Modeled after GuiSaveStatePreview for visual consistency.
//  Replaces the full-screen GuiImageViewer for a uniform look.
// ============================================================================
#pragma once
#ifndef ES_APP_GUIS_GUI_IMAGE_POPUP_H
#define ES_APP_GUIS_GUI_IMAGE_POPUP_H

#include "GuiComponent.h"
#include "components/ComponentGrid.h"
#include "components/NinePatchComponent.h"

class ButtonComponent;
class ImageComponent;
class TextComponent;

class GuiImagePopup : public GuiComponent
{
public:
	// title:      shown at the top of the popup
	// imagePath:  path to PNG/JPG (if empty or missing, image row is skipped)
	// detailText: shown below the image
	GuiImagePopup(Window* window,
	              const std::string& title,
	              const std::string& imagePath,
	              const std::string& detailText);

	bool input(InputConfig* config, Input input) override;
	void onSizeChanged() override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	NinePatchComponent mBackground;
	ComponentGrid mGrid;

	std::shared_ptr<TextComponent> mTitle;
	std::shared_ptr<ImageComponent> mImage;
	std::shared_ptr<TextComponent> mDetail;
	std::shared_ptr<ComponentGrid> mButtonGrid;
	std::vector<std::shared_ptr<ButtonComponent>> mButtons;
};

#endif // ES_APP_GUIS_GUI_IMAGE_POPUP_H
