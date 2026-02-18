#pragma once
#ifndef ES_CORE_COMPONENTS_ON_SCREEN_KEYBOARD_H
#define ES_CORE_COMPONENTS_ON_SCREEN_KEYBOARD_H

#include "GuiComponent.h"
#include <functional>
#include <string>
#include <vector>

class Font;
class TextCache;

// ============================================================================
//  OnScreenKeyboard
//
//  A joystick-navigable on-screen keyboard grid.
//  Navigate with dpad, press A to type, B for backspace.
//  L/R shoulder to cycle layouts (lowercase, uppercase, symbols).
//
//  Usage:
//    auto osk = std::make_shared<OnScreenKeyboard>(mWindow);
//    osk->setOnCharTyped([this](const std::string& ch) { ... });
//    osk->setOnBackspace([this]() { ... });
//    osk->setOnSubmit([this]() { ... });   // ENTER key
//    osk->setOnCancel([this]() { ... });   // explicit cancel
// ============================================================================

class OnScreenKeyboard : public GuiComponent
{
public:
	OnScreenKeyboard(Window* window);

	bool input(InputConfig* config, Input input) override;
	void render(const Transform4x4f& parentTrans) override;

	void onFocusGained() override;
	void onFocusLost() override;
	void onSizeChanged() override;

	// Callbacks
	void setOnCharTyped(std::function<void(const std::string&)> cb) { mOnCharTyped = cb; }
	void setOnBackspace(std::function<void()> cb) { mOnBackspace = cb; }
	void setOnSubmit(std::function<void()> cb) { mOnSubmit = cb; }
	void setOnCancel(std::function<void()> cb) { mOnCancel = cb; }

	// State
	void setPasswordMode(bool pw) { mPasswordMode = pw; }

	virtual std::vector<HelpPrompt> getHelpPrompts() override;

private:
	// Layout
	enum Layout { LOWER = 0, UPPER, SYMBOLS, LAYOUT_COUNT };
	void buildLayouts();
	void switchLayout(Layout layout);
	void nextLayout();
	void prevLayout();

	// Grid dimensions
	int mCols;
	int mRows;
	int mCursorCol;
	int mCursorRow;

	// Current layout
	Layout mCurrentLayout;
	std::vector< std::vector<std::string> > mLayouts;  // [layout][row*cols + col] = key label
	std::vector<std::string>& currentKeys() { return mLayouts[mCurrentLayout]; }

	// Special key labels
	static const std::string KEY_BACKSPACE;
	static const std::string KEY_SPACE;
	static const std::string KEY_ENTER;
	static const std::string KEY_SHIFT;
	static const std::string KEY_SYMBOLS;
	static const std::string KEY_CANCEL;

	bool isSpecialKey(const std::string& key) const;
	void pressKey(const std::string& key);

	// Rendering
	std::shared_ptr<Font> mFont;
	bool mFocused;
	bool mPasswordMode;

	float mKeyWidth;
	float mKeyHeight;
	float mKeyPadding;

	// Callbacks
	std::function<void(const std::string&)> mOnCharTyped;
	std::function<void()> mOnBackspace;
	std::function<void()> mOnSubmit;
	std::function<void()> mOnCancel;
};

#endif // ES_CORE_COMPONENTS_ON_SCREEN_KEYBOARD_H
