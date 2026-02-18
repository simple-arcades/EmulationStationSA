// ============================================================================
//  GuiSaveStatePreview.cpp
//
//  Custom popup with:
//    Row 0: Title text (save slot name)
//    Row 1: Large screenshot
//    Row 2: Detail text (timestamp)
//    Row 3: LOAD / DELETE / CANCEL buttons
// ============================================================================
#include "guis/GuiSaveStatePreview.h"
#include "SAStyle.h"

#include "components/ButtonComponent.h"
#include "components/ImageComponent.h"
#include "components/MenuComponent.h"  // for makeButtonGrid
#include "components/TextComponent.h"
#include "utils/StringUtil.h"

#define HORIZONTAL_PADDING_PX 20

GuiSaveStatePreview::GuiSaveStatePreview(Window* window,
                                         const std::string& title,
                                         const std::string& imagePath,
                                         const std::string& detailText,
                                         const std::function<void()>& loadFunc,
                                         const std::function<void()>& deleteFunc)
	: GuiComponent(window),
	  mBackground(window, ":/frame.png"),
	  mGrid(window, Vector2i(1, 4))  // title, image, detail, buttons
{
	// ---- Sizing constants ----
	float screenW = (float)Renderer::getScreenWidth();
	float screenH = (float)Renderer::getScreenHeight();
	float popupW = screenW * 0.65f;

	// Row 0: Title
	mTitle = std::make_shared<TextComponent>(mWindow, title,
		saFont(FONT_SIZE_MEDIUM), SA_TITLE_COLOR, ALIGN_CENTER);
	mGrid.setEntry(mTitle, Vector2i(0, 0), false, false);

	// Row 1: Screenshot
	mImage = std::make_shared<ImageComponent>(mWindow);
	if (!imagePath.empty())
	{
		mImage->setImage(imagePath);
		// Scale screenshot to fit within popup width, max height ~40% of screen
		float maxImgW = popupW - HORIZONTAL_PADDING_PX * 2;
		float maxImgH = screenH * 0.40f;
		mImage->setMaxSize(maxImgW, maxImgH);
	}
	mGrid.setEntry(mImage, Vector2i(0, 1), false, false);

	// Row 2: Detail text (timestamp etc.)
	std::string detail = detailText.empty() ? " " : detailText;
	mDetail = std::make_shared<TextComponent>(mWindow, detail,
		saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR, ALIGN_CENTER);
	mGrid.setEntry(mDetail, Vector2i(0, 2), false, false);

	// Row 3: Buttons
	mCancelFunc = [this]() { deleteMeAndCall(nullptr); };

	mButtons.push_back(std::make_shared<ButtonComponent>(mWindow, "LOAD", "LOAD",
		std::bind(&GuiSaveStatePreview::deleteMeAndCall, this, loadFunc)));
	mButtons.push_back(std::make_shared<ButtonComponent>(mWindow, "DELETE", "DELETE",
		std::bind(&GuiSaveStatePreview::deleteMeAndCall, this, deleteFunc)));
	mButtons.push_back(std::make_shared<ButtonComponent>(mWindow, "CANCEL", "CANCEL",
		mCancelFunc));

	mButtonGrid = makeButtonGrid(mWindow, mButtons);
	mGrid.setEntry(mButtonGrid, Vector2i(0, 3), true, false, Vector2i(1, 1), GridFlags::BORDER_TOP);

	// ---- Calculate final size ----
	float titleH = mTitle->getSize().y() * 1.2f;
	float imageH = mImage->getSize().y() > 0 ? mImage->getSize().y() + 10 : 0;
	float detailH = mDetail->getSize().y() * 1.2f;
	float buttonH = mButtonGrid->getSize().y();
	float totalH = titleH + imageH + detailH + buttonH;

	// Clamp
	float maxH = screenH * 0.85f;
	if (totalH > maxH) totalH = maxH;

	setSize(popupW + HORIZONTAL_PADDING_PX * 2, totalH);
	setPosition((screenW - mSize.x()) / 2.0f, (screenH - mSize.y()) / 2.0f);

	addChild(&mBackground);
	addChild(&mGrid);
}

bool GuiSaveStatePreview::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value != 0)
	{
		mCancelFunc();
		return true;
	}

	return GuiComponent::input(config, input);
}

void GuiSaveStatePreview::onSizeChanged()
{
	mGrid.setSize(mSize);

	float buttonH = mButtonGrid->getSize().y();
	float titleH = mTitle->getSize().y() * 1.2f;
	float detailH = mDetail->getSize().y() * 1.2f;

	// Proportional row heights
	float imageH = mSize.y() - titleH - detailH - buttonH;
	if (imageH < 0) imageH = 0;

	mGrid.setRowHeightPerc(0, titleH / mSize.y());
	mGrid.setRowHeightPerc(1, imageH / mSize.y());
	mGrid.setRowHeightPerc(2, detailH / mSize.y());
	mGrid.setRowHeightPerc(3, buttonH / mSize.y());

	mGrid.onSizeChanged();
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));
}

void GuiSaveStatePreview::deleteMeAndCall(const std::function<void()>& func)
{
	auto funcCopy = func;
	delete this;
	if (funcCopy)
		funcCopy();
}

std::vector<HelpPrompt> GuiSaveStatePreview::getHelpPrompts()
{
	return mGrid.getHelpPrompts();
}
