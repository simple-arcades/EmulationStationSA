#pragma once
#ifndef ES_APP_GUIS_GUI_IMAGE_VIEWER_H
#define ES_APP_GUIS_GUI_IMAGE_VIEWER_H

#include "GuiComponent.h"
#include "components/ImageComponent.h"
#include <memory>

class TextCache;
class Font;

// Simple fullscreen image viewer. Displays a single image centered on a dark
// background and closes on any button press (B, A, or Start).
class GuiImageViewer : public GuiComponent
{
public:
	// imagePath: absolute path to a PNG/JPG image file.
	// title: optional text shown at top of screen (empty = no title).
	GuiImageViewer(Window* window, const std::string& imagePath, const std::string& title = "");
	~GuiImageViewer();

	bool input(InputConfig* config, Input input) override;
	void render(const Transform4x4f& parentTrans) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	ImageComponent mImage;
	std::string mTitle;

	// Cached text rendering — built once in constructor, not per-frame
	std::shared_ptr<Font> mTitleFont;
	std::shared_ptr<Font> mHintFont;
	std::unique_ptr<TextCache> mTitleCache;
	std::unique_ptr<TextCache> mHintCache;
	float mTitleX;
	float mTitleY;
	float mHintX;
	float mHintY;
};

#endif // ES_APP_GUIS_GUI_IMAGE_VIEWER_H