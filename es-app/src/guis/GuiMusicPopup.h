#pragma once
#ifndef ES_APP_GUIS_GUI_MUSIC_POPUP_H
#define ES_APP_GUIS_GUI_MUSIC_POPUP_H

#include "GuiComponent.h"
#include "Window.h"
#include "components/ImageComponent.h"
#include "resources/Font.h"
#include <memory>

// Custom "Now Playing" popup for Simple Arcades music.
// Displays a cover-art thumbnail with track info text at top-right of screen.
class GuiMusicPopup : public GuiComponent, public Window::InfoPopup
{
public:
	// soundtrack: cleaned soundtrack/folder name
	// trackName:  cleaned track filename
	// coverPath:  absolute path to cover art image (or fallback)
	GuiMusicPopup(Window* window,
		const std::string& soundtrack,
		const std::string& trackName,
		const std::string& coverPath,
		int duration = 4000,
		int fadein = 500,
		int fadeout = 500);

	~GuiMusicPopup();

	void render(const Transform4x4f& parentTrans) override;
	inline void stop() override { mRunning = false; }

private:
	bool updateState();

	// Text content.
	std::string mSoundtrack;
	std::string mTrackName;

	// Cover art.
	ImageComponent mImage;

	// Font (custom TinyUnicode if available, else default).
	std::shared_ptr<Font> mFont;

	// Timing.
	int mDuration;
	int mFadein;
	int mFadeout;
	int mStartTime;
	int mAlpha;
	bool mRunning;

	// Layout metrics (computed once in constructor).
	float mPopupX, mPopupY;
	float mPopupW, mPopupH;
	float mThumbSize;
	float mTextX;
	float mTextMaxW;
};

#endif // ES_APP_GUIS_GUI_MUSIC_POPUP_H
