// ============================================================================
//  GuiTextInput.cpp
//
//  Full-screen text input popup with on-screen keyboard.
//  Displays a title, a text field showing the typed value, button legend,
//  and the OSK grid.
// ============================================================================
#include "guis/GuiTextInput.h"
#include "SAStyle.h"

#include "components/TextComponent.h"
#include "resources/Font.h"
#include "renderers/Renderer.h"
#include "Window.h"
#include "guis/GuiMsgBox.h"

GuiTextInput::GuiTextInput(Window* window,
	const std::string& title,
	const std::string& initialValue,
	std::function<void(const std::string&)> okCallback,
	bool passwordMode,
	int minChars)
	: GuiComponent(window),
	  mBackground(window, ":/frame.png"),
	  mTitle(title),
	  mValue(initialValue),
	  mPasswordMode(passwordMode),
	  mMinChars(minChars),
	  mCursorBlink(0),
	  mTitleFont(saFont(FONT_SIZE_MEDIUM)),
	  mTextFont(saFont(FONT_SIZE_LARGE)),
	  mKeyboard(window),
	  mOkCallback(okCallback)
{
	addChild(&mBackground);

	float sw = (float)Renderer::getScreenWidth();
	float sh = (float)Renderer::getScreenHeight();
	setSize(sw, sh);

	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	// Keyboard sizing — same width as panel, anchored to bottom
	float kbWidth = sw * 0.88f;
	float kbX = (sw - kbWidth) / 2.0f;
	mKeyboard.setSize(kbWidth, 0);  // height auto-calculated
	float kbY = sh - mKeyboard.getSize().y() - sh * 0.03f;
	mKeyboard.setPosition(kbX, kbY);

	// Wire up keyboard callbacks
	mKeyboard.setOnCharTyped([this](const std::string& ch) {
		mValue += ch;
		updateTextDisplay();
	});

	mKeyboard.setOnBackspace([this]() {
		if (!mValue.empty())
		{
			size_t newLen = mValue.length();
			if (newLen > 0)
			{
				while (newLen > 0 && (mValue[newLen - 1] & 0xC0) == 0x80)
					newLen--;
				if (newLen > 0)
					newLen--;
				mValue = mValue.substr(0, newLen);
			}
		}
		updateTextDisplay();
	});

	mKeyboard.setOnSubmit([this]() {
		if (mMinChars > 0 && (int)mValue.length() < mMinChars)
		{
			mWindow->pushGui(new GuiMsgBox(mWindow,
				"NEED AT LEAST " + std::to_string(mMinChars) + " CHARACTERS.",
				"OK", nullptr));
			return;
		}
		if (mOkCallback) mOkCallback(mValue);
		delete this;
	});

	mKeyboard.setOnCancel([this]() {
		delete this;
	});

	mKeyboard.setPasswordMode(passwordMode);
	updateTextDisplay();
}

bool GuiTextInput::input(InputConfig* config, Input input)
{
	return mKeyboard.input(config, input);
}

void GuiTextInput::update(int deltaTime)
{
	mCursorBlink += deltaTime;
	if (mCursorBlink > 1000) mCursorBlink -= 1000;
	GuiComponent::update(deltaTime);
}

void GuiTextInput::updateTextDisplay()
{
	mTitleCache = std::unique_ptr<TextCache>(
		mTitleFont->buildTextCache(mTitle, 0, 0, SA_TEXT_COLOR));

	std::string display;
	if (mPasswordMode && !mValue.empty())
	{
		for (size_t i = 0; i < mValue.length() - 1; i++)
			display += "*";
		display += mValue.substr(mValue.length() - 1);
	}
	else
	{
		display = mValue;
	}

	if (mCursorBlink < 500)
		display += "|";

	mTextCache = std::unique_ptr<TextCache>(
		mTextFont->buildTextCache(display, 0, 0, 0xFFFFFFFF));
}

void GuiTextInput::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();

	float sw = mSize.x();
	float sh = mSize.y();

	// 1. Full-screen dim overlay
	Renderer::setMatrix(trans);
	Renderer::drawRect(0.0f, 0.0f, sw, sh, 0x000000D0, 0x000000D0);

	// Refresh text cache with current blink
	updateTextDisplay();

	// Use same X and width as the keyboard so they line up
	float panelX = mKeyboard.getPosition().x();
	float panelW = mKeyboard.getSize().x();
	float kbTop = mKeyboard.getPosition().y();

	// Calculate panel contents height to size it tightly
	float padding = 12.0f;
	float titleH = mTitleFont->getHeight();
	float fieldH = mTextFont->getHeight() * 1.6f;
	float gap = 8.0f;

	// Min chars row height (only if needed)
	auto smallFont = saFont(FONT_SIZE_SMALL);
	float minCharsH = (mMinChars > 0) ? (smallFont->getHeight() + 4.0f) : 0.0f;

	// Total panel height: padding + title + gap + field + gap + minChars + padding
	float panelH = padding + titleH + gap + fieldH + gap + minCharsH + padding;

	// Legend sits between panel and keyboard
	float legendFont_H = smallFont->getHeight();
	float legendGap = 10.0f;

	// Position panel above legend above keyboard
	float legendY = kbTop - legendFont_H - legendGap;
	float panelY = legendY - legendGap - panelH;

	// 2. Panel background — same width as keyboard
	Renderer::setMatrix(trans);
	Renderer::drawRect(panelX, panelY, panelW, panelH, 0x1A1A1AFF, 0x1A1A1AFF);

	// 3. Title
	float contentX = panelX + padding;
	float curY = panelY + padding;

	if (mTitleCache)
	{
		Transform4x4f titleTrans = trans;
		titleTrans.translate(Vector3f(contentX, curY, 0));
		Renderer::setMatrix(titleTrans);
		mTitleFont->renderTextCache(mTitleCache.get());
	}
	curY += titleH + gap;

	// 4. Text input field
	float fieldX = panelX + padding;
	float fieldW = panelW - padding * 2.0f;

	Renderer::setMatrix(trans);
	Renderer::drawRect(fieldX, curY, fieldW, fieldH, 0x333333FF, 0x333333FF);

	// Field border
	Renderer::drawRect(fieldX, curY, fieldW, 2.0f, 0x555555FF, 0x555555FF);
	Renderer::drawRect(fieldX, curY + fieldH - 2.0f, fieldW, 2.0f, 0x555555FF, 0x555555FF);
	Renderer::drawRect(fieldX, curY, 2.0f, fieldH, 0x555555FF, 0x555555FF);
	Renderer::drawRect(fieldX + fieldW - 2.0f, curY, 2.0f, fieldH, 0x555555FF, 0x555555FF);

	// Text value inside field
	if (mTextCache)
	{
		float textX = fieldX + 10.0f;
		float textY = curY + (fieldH - mTextCache->metrics.size.y()) / 2.0f;
		Transform4x4f textTrans = trans;
		textTrans.translate(Vector3f(textX, textY, 0));
		Renderer::setMatrix(textTrans);
		mTextFont->renderTextCache(mTextCache.get());
	}
	curY += fieldH + gap;

	// 5. Character count (if minimum set)
	if (mMinChars > 0)
	{
		std::string hint = std::to_string(mValue.length()) + "/" + std::to_string(mMinChars) + " MIN";
		unsigned int hintColor = ((int)mValue.length() >= mMinChars) ? 0x44DD44FF : 0xDD4444FF;
		auto hintCache = std::unique_ptr<TextCache>(
			smallFont->buildTextCache(hint, 0, 0, hintColor));
		float hintX = fieldX + fieldW - hintCache->metrics.size.x();
		Transform4x4f hintTrans = trans;
		hintTrans.translate(Vector3f(hintX, curY, 0));
		Renderer::setMatrix(hintTrans);
		smallFont->renderTextCache(hintCache.get());
	}

	// 6. Button legend — centered between panel and keyboard
	{
		std::string legend = "A:TYPE  B:DELETE  Y:OK  X:CANCEL  L/R:LAYOUT";
		auto legendCache = std::unique_ptr<TextCache>(
			smallFont->buildTextCache(legend, 0, 0, 0x888888FF));
		float legendX = (sw - legendCache->metrics.size.x()) / 2.0f;
		Transform4x4f legendTrans = trans;
		legendTrans.translate(Vector3f(legendX, legendY, 0));
		Renderer::setMatrix(legendTrans);
		smallFont->renderTextCache(legendCache.get());
	}

	// 7. Render keyboard manually
	mKeyboard.render(trans);
}

std::vector<HelpPrompt> GuiTextInput::getHelpPrompts()
{
	return mKeyboard.getHelpPrompts();
}
