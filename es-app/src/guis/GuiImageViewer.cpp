#include "guis/GuiImageViewer.h"
#include "SAStyle.h"

#include "components/TextComponent.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"

GuiImageViewer::GuiImageViewer(Window* window, const std::string& imagePath, const std::string& title)
	: GuiComponent(window), mImage(window), mTitle(title),
	  mTitleX(0), mTitleY(0), mHintX(0), mHintY(0)
{
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	// Load image and scale to fit within 80% of screen, preserving aspect ratio.
	mImage.setImage(imagePath);

	const float maxW = Renderer::getScreenWidth() * 0.80f;
	const float maxH = Renderer::getScreenHeight() * 0.70f;

	// setMaxSize scales preserving aspect ratio.
	mImage.setMaxSize(maxW, maxH);

	// Center the image on screen (offset down if title is shown).
	const float titleOffset = mTitle.empty() ? 0.0f : (Renderer::getScreenHeight() * 0.08f);
	const float imgX = (Renderer::getScreenWidth() - mImage.getSize().x()) * 0.5f;
	const float imgY = titleOffset + (Renderer::getScreenHeight() - titleOffset - mImage.getSize().y()) * 0.5f;
	mImage.setPosition(imgX, imgY);

	// Pre-build title text cache (built once, rendered every frame)
	if (!mTitle.empty())
	{
		mTitleFont = saFont(FONT_SIZE_MEDIUM);
		const float textW = mTitleFont->sizeText(mTitle).x();
		mTitleX = (Renderer::getScreenWidth() - textW) * 0.5f;
		mTitleY = Renderer::getScreenHeight() * 0.03f;
		mTitleCache = std::unique_ptr<TextCache>(
			mTitleFont->buildTextCache(mTitle, 0.0f, 0.0f, 0xFFFFFFFF));
	}

	// Pre-build hint text cache
	{
		mHintFont = saFont(FONT_SIZE_SMALL);
		const std::string hint = "PRESS BACK TO CLOSE";
		const float hintW = mHintFont->sizeText(hint).x();
		mHintX = (Renderer::getScreenWidth() - hintW) * 0.5f;
		mHintY = Renderer::getScreenHeight() * 0.93f;
		mHintCache = std::unique_ptr<TextCache>(
			mHintFont->buildTextCache(hint, 0.0f, 0.0f, SA_SCRAPER_SUBTITLE_COLOR));
	}
}

GuiImageViewer::~GuiImageViewer()
{
}

bool GuiImageViewer::input(InputConfig* config, Input input)
{
	if (input.value != 0)
	{
		if (config->isMappedTo("b", input) ||
			config->isMappedTo("a", input) ||
			config->isMappedTo("start", input))
		{
			delete this;
			return true;
		}
	}

	return GuiComponent::input(config, input);
}

void GuiImageViewer::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();

	// Dark background overlay.
	Renderer::setMatrix(trans);
	Renderer::drawRect(0.0f, 0.0f,
		(float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight(),
		0x000000E0, 0x000000E0);

	// Title text at top center if provided.
	if (mTitleCache)
	{
		Transform4x4f textTrans = trans;
		textTrans.translate(Vector3f(mTitleX, mTitleY, 0.0f));
		Renderer::setMatrix(textTrans);
		mTitleFont->renderTextCache(mTitleCache.get());
	}

	// Render the image.
	mImage.render(trans);

	// "PRESS BACK TO CLOSE" centered below the image.
	if (mHintCache)
	{
		Transform4x4f hintTrans = trans;
		hintTrans.translate(Vector3f(mHintX, mHintY, 0.0f));
		Renderer::setMatrix(hintTrans);
		mHintFont->renderTextCache(mHintCache.get());
	}
}

std::vector<HelpPrompt> GuiImageViewer::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("b", "close"));
	return prompts;
}