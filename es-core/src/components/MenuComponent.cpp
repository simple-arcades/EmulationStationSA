#include "components/MenuComponent.h"

#include "components/ButtonComponent.h"
#include "SAStyle.h"

#define BUTTON_GRID_VERT_PADDING 32
#define BUTTON_GRID_HORIZ_PADDING 10

#define TITLE_HEIGHT (mTitle->getFont()->getLetterHeight() + TITLE_VERT_PADDING)
#define SUBTITLE_LINE_HEIGHT (saFont(FONT_SIZE_SMALL)->getHeight() * 1.3f)
#define SUBTITLE_PADDING 8.0f

MenuComponent::MenuComponent(Window* window, const char* title, const std::shared_ptr<Font>& titleFont) : GuiComponent(window),
	mBackground(window), mGrid(window, Vector2i(1, 4)),
	mHasSubtitle(false), mSubtitleLineCount(0)
{
	addChild(&mBackground);
	addChild(&mGrid);

	mBackground.setImagePath(":/frame.png");

	// Row 0: title
	mTitle = std::make_shared<TextComponent>(mWindow);
	mTitle->setHorizontalAlignment(ALIGN_CENTER);
	mTitle->setColor(SA_TITLE_COLOR);
	setTitle(title, titleFont);
	mGrid.setEntry(mTitle, Vector2i(0, 0), false);

	// Row 1: subtitle — empty initially
	// Row 2: list
	mList = std::make_shared<ComponentList>(mWindow);
	mGrid.setEntry(mList, Vector2i(0, 2), true);

	// Row 3: buttons
	updateGrid();
	updateSize();

	mGrid.resetCursor();
}

void MenuComponent::setTitle(const char* title, const std::shared_ptr<Font>& font)
{
	mTitle->setText(Utils::String::toUpper(title));
	mTitle->setFont(font);
}

// ============================================================================
//  Subtitle
// ============================================================================

void MenuComponent::setSubtitle(const std::string& line1, unsigned int line1Color,
                                const std::string& line2, unsigned int line2Color)
{
	if (mSubtitleGrid)
		mGrid.removeEntry(mSubtitleGrid);

	mSubtitleLineCount = 1;
	bool hasLine2 = !line2.empty();
	if (hasLine2) mSubtitleLineCount = 2;

	auto subtitleFont = saFont(FONT_SIZE_SMALL);

	// Simple grid: 1 column x N rows, one TextComponent per line
	mSubtitleGrid = std::make_shared<ComponentGrid>(mWindow, Vector2i(1, mSubtitleLineCount));

	auto text1 = std::make_shared<TextComponent>(mWindow, line1, subtitleFont, line1Color);
	text1->setHorizontalAlignment(ALIGN_CENTER);
	mSubtitleGrid->setEntry(text1, Vector2i(0, 0), false, false);

	if (hasLine2)
	{
		auto text2 = std::make_shared<TextComponent>(mWindow, line2, subtitleFont, line2Color);
		text2->setHorizontalAlignment(ALIGN_CENTER);
		mSubtitleGrid->setEntry(text2, Vector2i(0, 1), false, false);
		mSubtitleGrid->setRowHeightPerc(0, 0.5f, false);
	}

	float totalW = (float)Math::min((int)Renderer::getScreenHeight(), (int)(Renderer::getScreenWidth() * 0.90f));
	float subH = getSubtitleHeight();
	mSubtitleGrid->setSize(totalW, subH);

	mGrid.setEntry(mSubtitleGrid, Vector2i(0, 1), false);

	mHasSubtitle = true;
	updateSize();
}

void MenuComponent::clearSubtitle()
{
	if (mSubtitleGrid)
	{
		mGrid.removeEntry(mSubtitleGrid);
		mSubtitleGrid.reset();
	}
	mHasSubtitle = false;
	mSubtitleLineCount = 0;
	updateSize();
}

float MenuComponent::getSubtitleHeight() const
{
	if (!mHasSubtitle || mSubtitleLineCount == 0) return 0;
	return mSubtitleLineCount * SUBTITLE_LINE_HEIGHT + SUBTITLE_PADDING;
}

// ============================================================================
//  Size and grid management
// ============================================================================

float MenuComponent::getButtonGridHeight() const
{
	return (mButtonGrid ? mButtonGrid->getSize().y() : saFont(FONT_SIZE_MEDIUM)->getHeight() + BUTTON_GRID_VERT_PADDING);
}

void MenuComponent::updateSize()
{
	const float maxHeight = Renderer::getScreenHeight() * 0.75f;
	float subtitleH = getSubtitleHeight();
	float height = TITLE_HEIGHT + subtitleH + mList->getTotalRowHeight() + getButtonGridHeight() + 2;
	if(height > maxHeight)
	{
		height = TITLE_HEIGHT + subtitleH + getButtonGridHeight();
		int i = 0;
		while(i < mList->size())
		{
			float rowHeight = mList->getRowHeight(i);
			if(height + rowHeight < maxHeight)
				height += rowHeight;
			else
				break;
			i++;
		}
	}

	float width = (float)Math::min((int)Renderer::getScreenHeight(), (int)(Renderer::getScreenWidth() * 0.90f));
	setSize(width, height);
}

void MenuComponent::onSizeChanged()
{
	mBackground.fitTo(mSize, Vector3f::Zero(), Vector2f(-32, -32));

	float subtitleH = getSubtitleHeight();

	mGrid.setRowHeightPerc(0, TITLE_HEIGHT / mSize.y(), false);

	if (subtitleH > 0 && mSize.y() > 0)
		mGrid.setRowHeightPerc(1, subtitleH / mSize.y(), false);
	else
		mGrid.setRowHeightPerc(1, 0.0001f, false);

	mGrid.setRowHeightPerc(3, getButtonGridHeight() / mSize.y(), false);

	mGrid.setSize(mSize);
}

void MenuComponent::addButton(const std::string& name, const std::string& helpText, const std::function<void()>& callback)
{
	mButtons.push_back(std::make_shared<ButtonComponent>(mWindow, Utils::String::toUpper(name), helpText, callback));
	updateGrid();
	updateSize();
}

void MenuComponent::updateGrid()
{
	if(mButtonGrid)
		mGrid.removeEntry(mButtonGrid);

	mButtonGrid.reset();

	if(mButtons.size())
	{
		mButtonGrid = makeButtonGrid(mWindow, mButtons);
		mGrid.setEntry(mButtonGrid, Vector2i(0, 3), true, false);
	}
}

std::vector<HelpPrompt> MenuComponent::getHelpPrompts()
{
	return mGrid.getHelpPrompts();
}

std::shared_ptr<ComponentGrid> makeButtonGrid(Window* window, const std::vector< std::shared_ptr<ButtonComponent> >& buttons)
{
	std::shared_ptr<ComponentGrid> buttonGrid = std::make_shared<ComponentGrid>(window, Vector2i((int)buttons.size(), 2));

	float buttonGridWidth = (float)BUTTON_GRID_HORIZ_PADDING * buttons.size();
	for(int i = 0; i < (int)buttons.size(); i++)
	{
		buttonGrid->setEntry(buttons.at(i), Vector2i(i, 0), true, false);
		buttonGridWidth += buttons.at(i)->getSize().x();
	}
	for(unsigned int i = 0; i < buttons.size(); i++)
	{
		buttonGrid->setColWidthPerc(i, (buttons.at(i)->getSize().x() + BUTTON_GRID_HORIZ_PADDING) / buttonGridWidth);
	}

	buttonGrid->setSize(buttonGridWidth, buttons.at(0)->getSize().y() + BUTTON_GRID_VERT_PADDING + 2);
	buttonGrid->setRowHeightPerc(1, 2 / buttonGrid->getSize().y());

	return buttonGrid;
}

std::shared_ptr<ImageComponent> makeArrow(Window* window)
{
	auto bracket = std::make_shared<ImageComponent>(window);
	bracket->setImage(":/arrow.svg");
	bracket->setResize(0, Math::round(saFont(FONT_SIZE_MEDIUM)->getLetterHeight()));
	return bracket;
}
