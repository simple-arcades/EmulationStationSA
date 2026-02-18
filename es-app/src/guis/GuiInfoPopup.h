#pragma once
#ifndef ES_APP_GUIS_GUI_INFO_POPUP_H
#define ES_APP_GUIS_GUI_INFO_POPUP_H

#include "GuiComponent.h"
#include "Window.h"

class ComponentGrid;
class NinePatchComponent;

// Popup screen position.
enum PopupPosition
{
	POPUP_TOP_CENTER,
	POPUP_TOP_RIGHT,
	POPUP_CENTER,
	POPUP_BOTTOM_CENTER,
	POPUP_BOTTOM_LEFT,
	POPUP_BOTTOM_RIGHT
};

class GuiInfoPopup : public GuiComponent, public Window::InfoPopup
{
public:
	GuiInfoPopup(Window* window, std::string message, int duration, int fadein = 500, int fadeout = 500, PopupPosition pos = POPUP_TOP_CENTER, bool dimBackground = false);
	~GuiInfoPopup();
	void render(const Transform4x4f& parentTrans) override;
	inline void stop() override { running = false; };
private:
	std::string mMessage;
	int mDuration;
	int mFadein;
	int mFadeout;
	int alpha;
	bool updateState();
	int mStartTime;
	ComponentGrid* mGrid;
	NinePatchComponent* mFrame;
	bool running;
	bool mDimBackground;
};

#endif // ES_APP_GUIS_GUI_INFO_POPUP_H
