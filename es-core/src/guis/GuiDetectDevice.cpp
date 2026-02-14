#include "guis/GuiDetectDevice.h"
#include "SAStyle.h"
#include <SDL_joystick.h>

#include "components/TextComponent.h"
#include "guis/GuiInputConfig.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "InputManager.h"
#include "PowerSaver.h"
#include "Window.h"

#define HOLD_TIME 1000

static bool isBlacklistedDeviceName(const std::string& name)
{
	// Treat any DragonRise device as non-configurable (built-ins + extra DragonRise controllers)
	const std::string upper = Utils::String::toUpper(name);
	return (upper.find("DRAGONRISE") != std::string::npos);
}

GuiDetectDevice::GuiDetectDevice(Window* window, bool firstRun, const std::function<void()>& doneCallback) : GuiComponent(window), mFirstRun(firstRun),
	mBackground(window, ":/frame.png"), mGrid(window, Vector2i(1, 5))
{
	mHoldingConfig = NULL;
	mHoldTime = 0;
	mDoneCallback = doneCallback;

	addChild(&mBackground);
	addChild(&mGrid);

	// title
	mTitle = std::make_shared<TextComponent>(mWindow, firstRun ? "WELCOME" : "CONFIGURE INPUT",
		saFont(FONT_SIZE_LARGE), SA_TITLE_COLOR, ALIGN_CENTER);
	mGrid.setEntry(mTitle, Vector2i(0, 0), false, true, Vector2i(1, 1), GridFlags::BORDER_BOTTOM);

	// device info
	std::stringstream deviceInfo;

	// Count only non-blacklisted devices
	int numDevices = 0;
	const int total = InputManager::getInstance()->getNumJoysticks();
	for(int i = 0; i < total; i++)
	{
		const char* nm = SDL_JoystickNameForIndex(i);
		if(nm && isBlacklistedDeviceName(nm))
			continue;

		numDevices++;
	}

	if(numDevices > 0)
		deviceInfo << numDevices << " EXTERNAL GAMEPAD" << (numDevices > 1 ? "S" : "") << " DETECTED";
	else
		deviceInfo << "NO EXTERNAL GAMEPADS DETECTED";
	
	mDeviceInfo = std::make_shared<TextComponent>(mWindow, deviceInfo.str(), saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR, ALIGN_CENTER);
	mGrid.setEntry(mDeviceInfo, Vector2i(0, 1), false, true);

	// message
	std::string msg1str =
		(!firstRun && numDevices == 0)
			? "RETURN TO THE MAIN MENU, CONNECT YOUR CONTROLLER THEN TRY AGAIN."
			: "HOLD A BUTTON ON YOUR DEVICE TO CONFIGURE IT.";

	mMsg1 = std::make_shared<TextComponent>(
		mWindow, msg1str,
		saFont(FONT_SIZE_SMALL), SA_TEXT_COLOR, ALIGN_CENTER);
	mGrid.setEntry(mMsg1, Vector2i(0, 2), false, true);

	const char* msg2str = firstRun ? "PRESS F4 TO QUIT AT ANY TIME."
		: (numDevices > 0 ? "PRESS BACK (OR ESC) TO CANCEL." : "PRESS BACK (OR ESC) TO RETURN.");

	mMsg2 = std::make_shared<TextComponent>(
		mWindow, msg2str,
		saFont(FONT_SIZE_SMALL), SA_TEXT_COLOR, ALIGN_CENTER);
	mGrid.setEntry(mMsg2, Vector2i(0, 3), false, true);

	// currently held device
	mDeviceHeld = std::make_shared<TextComponent>(mWindow, "", saFont(FONT_SIZE_MEDIUM), 0xFFFFFFFF, ALIGN_CENTER);
	mGrid.setEntry(mDeviceHeld, Vector2i(0, 4), false, true);

	setSize(Renderer::getScreenWidth() * 0.6f, Renderer::getScreenHeight() * 0.5f);
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2, (Renderer::getScreenHeight() - mSize.y()) / 2);
}

void GuiDetectDevice::onSizeChanged()
{
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	// grid
	mGrid.setSize(mSize);
	mGrid.setRowHeightPerc(0, mTitle->getFont()->getHeight() / mSize.y());
	//mGrid.setRowHeightPerc(1, mDeviceInfo->getFont()->getHeight() / mSize.y());
	mGrid.setRowHeightPerc(2, mMsg1->getFont()->getHeight() / mSize.y());
	mGrid.setRowHeightPerc(3, mMsg2->getFont()->getHeight() / mSize.y());
	//mGrid.setRowHeightPerc(4, mDeviceHeld->getFont()->getHeight() / mSize.y());
}

bool GuiDetectDevice::input(InputConfig* config, Input input)
{
    PowerSaver::pause();

    if(!mFirstRun)
    {
        // Keyboard ESC cancels
        if(input.device == DEVICE_KEYBOARD && input.type == TYPE_KEY && input.value && input.id == SDLK_ESCAPE)
        {
            PowerSaver::resume();
            delete this;
            return true;
        }

        // Controller BACK cancels
        if(config && config->getDeviceId() != DEVICE_KEYBOARD && input.value)
        {
            const bool isBL = isBlacklistedDeviceName(config->getDeviceName());

            // Normal controllers: "b" cancels
            // Blacklisted built-ins: allow "a" OR "b" to cancel
            if(config->isMappedTo("b", input) || (isBL && config->isMappedTo("a", input)))
            {
                PowerSaver::resume();
                delete this;
                return true;
            }
        }
    }

    // NOW ignore blacklisted devices so they can't be configured
    if(config && isBlacklistedDeviceName(config->getDeviceName()))
        return true;
		
	if(input.type == TYPE_BUTTON || input.type == TYPE_KEY ||input.type == TYPE_CEC_BUTTON)
	{
		if(input.value && mHoldingConfig == NULL)
		{
			// started holding
			mHoldingConfig = config;
			mHoldTime = HOLD_TIME;
			mDeviceHeld->setText(Utils::String::toUpper(config->getDeviceName()));
		}else if(!input.value && mHoldingConfig == config)
		{
			// cancel
			mHoldingConfig = NULL;
			mDeviceHeld->setText("");
		}
	}

	return true;
}

void GuiDetectDevice::update(int deltaTime)
{
	if(mHoldingConfig)
	{
		// If ES starts and if a known device is connected after startup skip controller configuration
		if(mFirstRun && Utils::FileSystem::exists(InputManager::getConfigPath()) && InputManager::getInstance()->getNumConfiguredDevices() > 0)
		{
			if(mDoneCallback)
				mDoneCallback();
			PowerSaver::resume();
			delete this; // delete GUI element
		}
		else
		{
			mHoldTime -= deltaTime;
			const float t = (float)mHoldTime / HOLD_TIME;
			unsigned int c = (unsigned char)(t * 255);
			mDeviceHeld->setColor((c << 24) | (c << 16) | (c << 8) | 0xFF);
			if(mHoldTime <= 0)
			{
				// picked one!
				mWindow->pushGui(new GuiInputConfig(mWindow, mHoldingConfig, true, mDoneCallback));
				PowerSaver::resume();
				delete this;
			}
		}
	}
}
