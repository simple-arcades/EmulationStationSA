#pragma once
#ifndef ES_CORE_WINDOW_H
#define ES_CORE_WINDOW_H

#include "HelpPrompt.h"
#include "InputConfig.h"
#include "Settings.h"

#include <memory>
#include <string>

class SystemData;
class FileData;
class Font;
class GuiComponent;
class HelpComponent;
class ImageComponent;
class InputConfig;
class TextCache;
class Transform4x4f;
struct HelpStyle;

class Window
{
public:
	class ScreenSaver {
	public:
		virtual void startScreenSaver(SystemData* system=NULL) = 0;
		virtual void stopScreenSaver(bool toResume=false) = 0;
		virtual void renderScreenSaver() = 0;
		virtual bool allowSleep() = 0;
		virtual void update(int deltaTime) = 0;
		virtual bool isScreenSaverActive() = 0;
		virtual FileData* getCurrentGame() = 0;
		virtual void selectGame(bool launch) = 0;
		virtual bool inputDuringScreensaver(InputConfig* config, Input input) = 0;
	};

	class InfoPopup {
	public:
		virtual void render(const Transform4x4f& parentTrans) = 0;
		virtual void stop() = 0;
		virtual ~InfoPopup() {};
	};

	Window();
	~Window();

	void pushGui(GuiComponent* gui);
	void removeGui(GuiComponent* gui);
	GuiComponent* peekGui();
	inline int getGuiStackSize() { return (int)mGuiStack.size(); }

	void textInput(const char* text);
	void input(InputConfig* config, Input input);
	void update(int deltaTime);
	void render();

	bool init();
	void deinit();

	void normalizeNextUpdate();

	inline bool isSleeping() const { return mSleeping; }
	bool getAllowSleep();
	void setAllowSleep(bool sleep);

	void renderLoadingScreen(std::string text, float percent = -1, unsigned char opacity = 255);

	void renderHelpPromptsEarly(); // used to render HelpPrompts before a fade
	void setHelpPrompts(const std::vector<HelpPrompt>& prompts, const HelpStyle& style);

	void setScreenSaver(ScreenSaver* screenSaver) { mScreenSaver = screenSaver; }
	void setInfoPopup(InfoPopup* infoPopup) { delete mInfoPopup; mInfoPopup = infoPopup; }
	inline void stopInfoPopup() { if (mInfoPopup) mInfoPopup->stop(); };

	void startScreenSaver(SystemData* system=NULL);
	bool cancelScreenSaver();
	void renderScreenSaver();

	// Restart reason: reads ~/.restart_reason on startup to determine
	// which boot image and loading text to show. The file is deleted
	// after reading. If no file exists, it's a normal (cold) boot.
	void readRestartReason();
	const std::string& getRestartReason() const { return mRestartReason; }
	bool hasRestartReason() const { return !mRestartReason.empty(); }

	// Returns the loading text appropriate for the current restart reason.
	// If no reason is set, returns the provided default text unchanged.
	std::string getRestartText(const std::string& defaultText) const;

	// Returns the resolved boot image path (set once during readRestartReason).
	const std::string& getBootImagePath() const { return mBootImagePath; }

private:
	void onSleep();
	void onWake();

	// Returns true if at least one component on the stack is processing
	bool isProcessing();

	HelpComponent*	mHelp;
	ImageComponent* mBackgroundOverlay;
	ScreenSaver*	mScreenSaver;
	InfoPopup*	mInfoPopup;
	bool		mRenderScreenSaver;

	std::vector<GuiComponent*> mGuiStack;

	std::vector< std::shared_ptr<Font> > mDefaultFonts;

	int mFrameTimeElapsed;
	int mFrameCountElapsed;
	int mAverageDeltaTime;

	std::unique_ptr<TextCache> mFrameDataText;

	bool mNormalizeNextUpdate;

	bool mAllowSleep;
	bool mSleeping;
	unsigned int mTimeSinceLastInput;

	bool mRenderedHelpPrompts;

	std::string mRestartReason;
	std::string mBootImagePath;  // resolved once in readRestartReason()
	std::unique_ptr<ImageComponent> mSplashImage; // cached loading screen image
};

#endif // ES_CORE_WINDOW_H
