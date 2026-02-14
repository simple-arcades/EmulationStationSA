#include "guis/GuiMusicPopup.h"
#include "SAStyle.h"

#include "renderers/Renderer.h"
#include "utils/FileSystemUtil.h"
#include <SDL_timer.h>

// ============================================================

// Truncate a string so it fits within maxWidth pixels using the given font.
// Appends "..." if truncated.
static std::string truncateToFit(const std::shared_ptr<Font>& font, const std::string& text, float maxWidth)
{
	if (font->sizeText(text).x() <= maxWidth)
		return text;

	const std::string ellipsis = "...";
	const float ellipsisW = font->sizeText(ellipsis).x();
	if (maxWidth <= ellipsisW)
		return ellipsis;

	std::string result = text;
	while (!result.empty())
	{
		result.pop_back();
		if (font->sizeText(result).x() + ellipsisW <= maxWidth)
			return result + ellipsis;
	}

	return ellipsis;
}

GuiMusicPopup::GuiMusicPopup(Window* window,
	const std::string& soundtrack,
	const std::string& trackName,
	const std::string& coverPath,
	int duration, int fadein, int fadeout)
	: GuiComponent(window)
	, mSoundtrack(soundtrack)
	, mTrackName(trackName)
	, mImage(window)
	, mDuration(duration)
	, mFadein(fadein)
	, mFadeout(fadeout)
	, mStartTime(0)
	, mAlpha(0)
	, mRunning(true)
{
	// Load custom font, fall back to built-in if not found.
	if (Utils::FileSystem::exists(SA_FONT_PATH))
		mFont = Font::get(FONT_SIZE_EX_MINI, SA_FONT_PATH);
	else
		mFont = saFont(FONT_SIZE_EX_MINI);

	const float screenW = (float)Renderer::getScreenWidth();
	const float screenH = (float)Renderer::getScreenHeight();

	// Layout metrics.
	const float margin  = screenH * 0.010f;
	const float padding = screenH * 0.012f;
	const float fontH   = mFont->sizeText("A").y();
	const float lineH   = fontH * 1.15f;  // line height with spacing

	// Thumbnail: square, height = 3 lines of text + small padding.
	mThumbSize = lineH * 2.0f + padding;

	// Popup dimensions.
	mPopupW = screenW * 0.20f;
	mPopupH = mThumbSize + padding * 2.0f;

	// Position: top-right corner.
	mPopupX = margin;
	mPopupY = screenH - mPopupH - margin;

	// Text area: right of thumbnail.
	const float thumbPad = padding * 0.8f;
	mTextX    = padding + mThumbSize + thumbPad;
	mTextMaxW = mPopupW - mTextX - padding;

	// Load cover art.
	if (!coverPath.empty() && Utils::FileSystem::exists(coverPath))
	{
		mImage.setImage(coverPath);
	}
	mImage.setMaxSize(mThumbSize, mThumbSize);
}

GuiMusicPopup::~GuiMusicPopup()
{
}

void GuiMusicPopup::render(const Transform4x4f& /*parentTrans*/)
{
	if (!mRunning || !updateState())
		return;

	Transform4x4f trans = Transform4x4f::Identity();
	trans.translate(Vector3f(mPopupX, mPopupY, 0.0f));

	Renderer::setMatrix(trans);

	// Compute alpha-blended colors.
	const unsigned char a = (unsigned char)mAlpha;
	const unsigned int bgColor = (SA_MUSIC_BG_COLOR & 0xFFFFFF00) | a;

	// Background.
	Renderer::drawRect(0.0f, 0.0f, mPopupW, mPopupH, bgColor, bgColor);

	// Cover art thumbnail.
	const float padding = (float)Renderer::getScreenHeight() * 0.012f;
	if (mImage.hasImage())
	{
		const float imgX = padding;
		const float imgY = (mPopupH - mImage.getSize().y()) * 0.5f;
		mImage.setPosition(imgX, imgY);
		mImage.setOpacity(a);
		mImage.render(trans);
	}

	// Text lines.
	const float fontH = mFont->sizeText("A").y();
	const float lineH = fontH * 0.5f;
	float textY = fontH * 0.3f;

	// Helper lambda: render one line of text.
	auto renderLine = [&](const std::string& text, unsigned int baseColor, float y)
	{
		const std::string display = truncateToFit(mFont, text, mTextMaxW);
		const unsigned int color = (baseColor & 0xFFFFFF00) | a;

		Transform4x4f textTrans = trans;
		textTrans.translate(Vector3f(mTextX, y, 0.0f));
		Renderer::setMatrix(textTrans);
		TextCache* cache = mFont->buildTextCache(display, 0.0f, 0.0f, color);
		mFont->renderTextCache(cache);
		delete cache;
	};

	// Line 1: "NOW PLAYING" in teal.
	renderLine("NOW PLAYING", SA_MUSIC_LABEL_COLOR, textY);
	textY += lineH;

	// Line 2: Soundtrack name in white.
	renderLine(mSoundtrack, SA_MUSIC_TEXT_COLOR, textY);
	textY += lineH;

	// Line 3: Track name in white.
	renderLine(mTrackName, SA_MUSIC_TEXT_COLOR, textY);
}

bool GuiMusicPopup::updateState()
{
	int curTime = SDL_GetTicks();

	if (mStartTime == 0)
		mStartTime = curTime;

	const int elapsed = curTime - mStartTime;

	if (elapsed > mDuration)
	{
		mRunning = false;
		return false;
	}
	if (curTime < mStartTime)
	{
		// SDL timer wrapped.
		mRunning = false;
		return false;
	}

	if (elapsed <= mFadein)
		mAlpha = elapsed * 255 / mFadein;
	else if (elapsed < mDuration - mFadeout)
		mAlpha = 255;
	else
		mAlpha = -(elapsed - mDuration) * 255 / mFadeout;

	return true;
}
