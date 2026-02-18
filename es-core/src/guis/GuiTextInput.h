#pragma once
#ifndef ES_CORE_GUIS_GUI_TEXT_INPUT_H
#define ES_CORE_GUIS_GUI_TEXT_INPUT_H

#include "GuiComponent.h"
#include "components/NinePatchComponent.h"
#include "components/OnScreenKeyboard.h"
#include <functional>
#include <string>
#include <memory>

class TextComponent;
class Font;
class TextCache;

// ============================================================================
//  GuiTextInput
//
//  A full-screen popup with:
//    - Title/prompt at top
//    - Text display field (shows what you've typed)
//    - On-screen keyboard at bottom
//
//  Usage:
//    mWindow->pushGui(new GuiTextInput(mWindow,
//        "Enter Wi-Fi Password:", "",
//        [](const std::string& result) { /* use result */ },
//        true,  // password mode (show dots)
//        8));   // minimum characters
// ============================================================================

class GuiTextInput : public GuiComponent
{
public:
	GuiTextInput(Window* window,
		const std::string& title,
		const std::string& initialValue,
		std::function<void(const std::string&)> okCallback,
		bool passwordMode = false,
		int minChars = 0);

	bool input(InputConfig* config, Input input) override;
	void render(const Transform4x4f& parentTrans) override;
	void update(int deltaTime) override;

	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void updateTextDisplay();

	NinePatchComponent mBackground;

	std::string mTitle;
	std::string mValue;
	bool mPasswordMode;
	int mMinChars;
	int mCursorBlink;  // for blinking cursor

	std::shared_ptr<Font> mTitleFont;
	std::shared_ptr<Font> mTextFont;

	std::unique_ptr<TextCache> mTitleCache;
	std::unique_ptr<TextCache> mTextCache;

	OnScreenKeyboard mKeyboard;

	std::function<void(const std::string&)> mOkCallback;
};

#endif // ES_CORE_GUIS_GUI_TEXT_INPUT_H
