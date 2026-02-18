// ============================================================================
//  OnScreenKeyboard.cpp
//
//  Joystick-navigable on-screen keyboard for EmulationStation.
//  Renders a grid of keys, navigated with dpad/joystick.
//  A = type character, B = backspace, L/R = cycle layouts.
// ============================================================================
#include "components/OnScreenKeyboard.h"
#include "SAStyle.h"
#include "resources/Font.h"
#include "renderers/Renderer.h"
#include "utils/StringUtil.h"

const std::string OnScreenKeyboard::KEY_BACKSPACE = "\u2190";
const std::string OnScreenKeyboard::KEY_SPACE     = "SPACE";
const std::string OnScreenKeyboard::KEY_ENTER     = "ENTER";
const std::string OnScreenKeyboard::KEY_SHIFT     = "SHIFT";
const std::string OnScreenKeyboard::KEY_SYMBOLS   = "!@#";
const std::string OnScreenKeyboard::KEY_CANCEL    = "CANCEL";

OnScreenKeyboard::OnScreenKeyboard(Window* window)
	: GuiComponent(window),
	  mCols(10),
	  mRows(0),
	  mCursorCol(0),
	  mCursorRow(0),
	  mCurrentLayout(LOWER),
	  mFont(saFont(FONT_SIZE_SMALL)),
	  mFocused(false),
	  mPasswordMode(false),
	  mKeyWidth(0),
	  mKeyHeight(0),
	  mKeyPadding(2.0f)
{
	buildLayouts();
	onSizeChanged();
}

void OnScreenKeyboard::buildLayouts()
{
	mLayouts.resize(LAYOUT_COUNT);

	mLayouts[LOWER] = {
		"1","2","3","4","5","6","7","8","9","0",
		"q","w","e","r","t","y","u","i","o","p",
		"a","s","d","f","g","h","j","k","l",KEY_BACKSPACE,
		"z","x","c","v","b","n","m",".","-","@",
		KEY_SHIFT,KEY_SYMBOLS,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_ENTER,KEY_CANCEL
	};

	mLayouts[UPPER] = {
		"1","2","3","4","5","6","7","8","9","0",
		"Q","W","E","R","T","Y","U","I","O","P",
		"A","S","D","F","G","H","J","K","L",KEY_BACKSPACE,
		"Z","X","C","V","B","N","M",".","-","@",
		KEY_SHIFT,KEY_SYMBOLS,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_ENTER,KEY_CANCEL
	};

	mLayouts[SYMBOLS] = {
		"!","@","#","$","%","^","&","*","(",")",
		"~","`","+","=","[","]","{","}","|","\\",
		";",":","'","\"",",","<",">","/","?",KEY_BACKSPACE,
		"_","-","+","=",".",",","!","@","#","$",
		KEY_SHIFT,KEY_SYMBOLS,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_SPACE,KEY_ENTER,KEY_CANCEL
	};

	mRows = (int)mLayouts[LOWER].size() / mCols;
}

void OnScreenKeyboard::switchLayout(Layout layout) { mCurrentLayout = layout; }

void OnScreenKeyboard::nextLayout()
{
	switchLayout((Layout)(((int)mCurrentLayout + 1) % LAYOUT_COUNT));
}

void OnScreenKeyboard::prevLayout()
{
	switchLayout((Layout)(((int)mCurrentLayout - 1 + LAYOUT_COUNT) % LAYOUT_COUNT));
}

bool OnScreenKeyboard::isSpecialKey(const std::string& key) const
{
	return (key == KEY_BACKSPACE || key == KEY_SPACE || key == KEY_ENTER ||
	        key == KEY_SHIFT || key == KEY_SYMBOLS || key == KEY_CANCEL);
}

void OnScreenKeyboard::pressKey(const std::string& key)
{
	if (key == KEY_BACKSPACE)       { if (mOnBackspace) mOnBackspace(); }
	else if (key == KEY_SPACE)      { if (mOnCharTyped) mOnCharTyped(" "); }
	else if (key == KEY_ENTER)      { if (mOnSubmit) mOnSubmit(); }
	else if (key == KEY_SHIFT)
	{
		switchLayout(mCurrentLayout == UPPER ? LOWER : (mCurrentLayout == SYMBOLS ? LOWER : UPPER));
	}
	else if (key == KEY_SYMBOLS)
	{
		switchLayout(mCurrentLayout == SYMBOLS ? LOWER : SYMBOLS);
	}
	else if (key == KEY_CANCEL)     { if (mOnCancel) mOnCancel(); }
	else
	{
		if (mOnCharTyped) mOnCharTyped(key);
		if (mCurrentLayout == UPPER) switchLayout(LOWER);
	}
}

bool OnScreenKeyboard::input(InputConfig* config, Input input)
{
	if (input.value == 0) return false;

	if (config->isMappedLike("up", input))
	{
		mCursorRow = (mCursorRow - 1 + mRows) % mRows;
		const std::string& key = currentKeys()[mCursorRow * mCols + mCursorCol];
		if (key == KEY_SPACE)
			for (int c = 0; c < mCols; c++)
				if (currentKeys()[mCursorRow * mCols + c] == KEY_SPACE) { mCursorCol = c; break; }
		return true;
	}

	if (config->isMappedLike("down", input))
	{
		mCursorRow = (mCursorRow + 1) % mRows;
		const std::string& key = currentKeys()[mCursorRow * mCols + mCursorCol];
		if (key == KEY_SPACE)
			for (int c = 0; c < mCols; c++)
				if (currentKeys()[mCursorRow * mCols + c] == KEY_SPACE) { mCursorCol = c; break; }
		return true;
	}

	if (config->isMappedLike("left", input))
	{
		mCursorCol = (mCursorCol - 1 + mCols) % mCols;
		const std::string& key = currentKeys()[mCursorRow * mCols + mCursorCol];
		if (key == KEY_SPACE)
			for (int c = mCursorCol; c >= 0; c--)
				if (currentKeys()[mCursorRow * mCols + c] != KEY_SPACE) { mCursorCol = c; return true; }
		return true;
	}

	if (config->isMappedLike("right", input))
	{
		mCursorCol = (mCursorCol + 1) % mCols;
		const std::string& key = currentKeys()[mCursorRow * mCols + mCursorCol];
		if (key == KEY_SPACE)
		{
			for (int c = mCursorCol; c < mCols; c++)
				if (currentKeys()[mCursorRow * mCols + c] != KEY_SPACE) { mCursorCol = c; return true; }
			mCursorCol = 0;
		}
		return true;
	}

	if (config->isMappedTo("a", input))
	{
		int idx = mCursorRow * mCols + mCursorCol;
		if (idx >= 0 && idx < (int)currentKeys().size()) pressKey(currentKeys()[idx]);
		return true;
	}

	if (config->isMappedTo("b", input))          { if (mOnBackspace) mOnBackspace(); return true; }
	if (config->isMappedTo("leftshoulder", input))  { prevLayout(); return true; }
	if (config->isMappedTo("rightshoulder", input)) { nextLayout(); return true; }
	if (config->isMappedTo("x", input))           { if (mOnCancel) mOnCancel(); return true; }
	if (config->isMappedTo("y", input))           { if (mOnSubmit) mOnSubmit(); return true; }

	return false;
}

void OnScreenKeyboard::onSizeChanged()
{
	if (mCols <= 0) return;

	mKeyWidth = (mSize.x() - mKeyPadding * (mCols + 1)) / mCols;
	mKeyHeight = mFont->getHeight() * 1.4f;

	// Include space above for the layout indicator
	auto indicatorFont = saFont(FONT_SIZE_SMALL);
	float indicatorH = indicatorFont->getHeight() + 6.0f;

	float totalHeight = indicatorH + mRows * (mKeyHeight + mKeyPadding) + mKeyPadding;
	mSize = Vector2f(mSize.x(), totalHeight);
}

void OnScreenKeyboard::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();

	// Layout indicator — rendered ABOVE the key grid, right-aligned
	auto indicatorFont = saFont(FONT_SIZE_SMALL);
	std::string layoutLabel;
	switch (mCurrentLayout)
	{
		case LOWER:   layoutLabel = "abc"; break;
		case UPPER:   layoutLabel = "ABC"; break;
		case SYMBOLS: layoutLabel = "!@#"; break;
		default: break;
	}

	float indicatorH = indicatorFont->getHeight() + 6.0f;
	Vector2f liSize = indicatorFont->sizeText(layoutLabel);
	Transform4x4f liTrans = trans;
	liTrans.translate(Vector3f(mSize.x() - liSize.x() - 8.0f, 0, 0));
	Renderer::setMatrix(liTrans);
	auto liCache = std::unique_ptr<TextCache>(indicatorFont->buildTextCache(layoutLabel, 0, 0, 0xAAAAAAFF));
	indicatorFont->renderTextCache(liCache.get());

	// Offset everything below the indicator
	float gridOffsetY = indicatorH;

	// Background behind key grid
	Renderer::setMatrix(trans);
	float gridH = mRows * (mKeyHeight + mKeyPadding) + mKeyPadding;
	Renderer::drawRect(0.0f, gridOffsetY, mSize.x(), gridH, 0x222222E0, 0x222222E0);

	const auto& keys = currentKeys();

	for (int row = 0; row < mRows; row++)
	{
		for (int col = 0; col < mCols; col++)
		{
			int idx = row * mCols + col;
			if (idx >= (int)keys.size()) break;

			const std::string& key = keys[idx];

			if (key == KEY_SPACE && col > 0 && keys[idx - 1] == KEY_SPACE)
				continue;

			float x = mKeyPadding + col * (mKeyWidth + mKeyPadding);
			float y = gridOffsetY + mKeyPadding + row * (mKeyHeight + mKeyPadding);
			float w = mKeyWidth;

			if (key == KEY_SPACE)
			{
				int span = 0;
				for (int c = col; c < mCols && keys[row * mCols + c] == KEY_SPACE; c++) span++;
				w = span * mKeyWidth + (span - 1) * mKeyPadding;
			}

			bool isCursor = false;
			if (key == KEY_SPACE)
			{
				int spaceStart = col, spaceEnd = col;
				for (int c = col; c < mCols && keys[row * mCols + c] == KEY_SPACE; c++) spaceEnd = c;
				isCursor = (mCursorRow == row && mCursorCol >= spaceStart && mCursorCol <= spaceEnd);
			}
			else
			{
				isCursor = (row == mCursorRow && col == mCursorCol);
			}

			unsigned int bgColor = 0x444444FF;
			unsigned int textColor = 0xDDDDDDFF;

			if (isCursor)      { bgColor = 0xDD0000FF; textColor = 0xFFFFFFFF; }
			else if (isSpecialKey(key)) { bgColor = 0x333333FF; textColor = 0xAAAAAAFF; }

			Renderer::setMatrix(trans);
			Renderer::drawRect(x, y, w, mKeyHeight, bgColor, bgColor);

			std::string label = key;
			if (key == KEY_BACKSPACE) label = "<-";
			else if (key == KEY_SPACE) label = "SPACE";
			else if (key == KEY_ENTER) label = "OK";
			else if (key == KEY_SHIFT)  label = (mCurrentLayout == UPPER) ? "SHIFT" : ((mCurrentLayout == SYMBOLS) ? "abc" : "SHIFT");
			else if (key == KEY_SYMBOLS) label = (mCurrentLayout == SYMBOLS) ? "abc" : "!@#";
			else if (key == KEY_CANCEL) label = "X";

			Vector2f textSize = mFont->sizeText(label);
			float textX = x + (w - textSize.x()) / 2.0f;
			float textY = y + (mKeyHeight - textSize.y()) / 2.0f;

			Transform4x4f textTrans = trans;
			textTrans.translate(Vector3f(textX, textY, 0));
			Renderer::setMatrix(textTrans);

			auto tc = std::unique_ptr<TextCache>(mFont->buildTextCache(label, 0, 0, textColor));
			mFont->renderTextCache(tc.get());
		}
	}
}

void OnScreenKeyboard::onFocusGained() { mFocused = true; }
void OnScreenKeyboard::onFocusLost() { mFocused = false; }

std::vector<HelpPrompt> OnScreenKeyboard::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("a", "type"));
	prompts.push_back(HelpPrompt("b", "backspace"));
	prompts.push_back(HelpPrompt("y", "submit"));
	prompts.push_back(HelpPrompt("x", "cancel"));
	prompts.push_back(HelpPrompt("l/r", "layout"));
	return prompts;
}
