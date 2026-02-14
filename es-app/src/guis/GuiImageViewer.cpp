#include "guis/GuiImageViewer.h"
#include "SAStyle.h"

#include "components/TextComponent.h"
#include "renderers/Renderer.h"
#include "resources/Font.h"

GuiImageViewer::GuiImageViewer(Window* window, const std::string& imagePath, const std::string& title)
	: GuiComponent(window), mImage(window), mTitle(title)
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
	if (!mTitle.empty())
	{
		auto font = saFont(FONT_SIZE_MEDIUM);
		const float textW = font->sizeText(mTitle).x();
		const float textX = (Renderer::getScreenWidth() - textW) * 0.5f;
		const float textY = Renderer::getScreenHeight() * 0.03f;

		Transform4x4f textTrans = trans;
		textTrans.translate(Vector3f(textX, textY, 0.0f));
		Renderer::setMatrix(textTrans);
		TextCache* cache = font->buildTextCache(mTitle, 0.0f, 0.0f, 0xFFFFFFFF);
		font->renderTextCache(cache);
		delete cache;
	}

	// Render the image.
	mImage.render(trans);

	// "PRESS BACK TO CLOSE" centered below the image.
	{
		auto font = saFont(FONT_SIZE_SMALL);
		const std::string hint = "PRESS BACK TO CLOSE";
		const float hintW = font->sizeText(hint).x();
		const float hintX = (Renderer::getScreenWidth() - hintW) * 0.5f;
		const float hintY = Renderer::getScreenHeight() * 0.93f;

		Transform4x4f hintTrans = trans;
		hintTrans.translate(Vector3f(hintX, hintY, 0.0f));
		Renderer::setMatrix(hintTrans);
		TextCache* hintCache = font->buildTextCache(hint, 0.0f, 0.0f, SA_SCRAPER_SUBTITLE_COLOR);
		font->renderTextCache(hintCache);
		delete hintCache;
	}
}

std::vector<HelpPrompt> GuiImageViewer::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("b", "close"));
	return prompts;
}
