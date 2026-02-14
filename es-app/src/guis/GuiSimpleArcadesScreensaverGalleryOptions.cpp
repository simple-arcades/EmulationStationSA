#include "guis/GuiSimpleArcadesScreensaverGalleryOptions.h"
#include "SAStyle.h"

#include "Window.h"
#include "components/SwitchComponent.h"
#include "components/ComponentList.h"
#include "components/TextComponent.h"
#include "guis/GuiMsgBox.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "SimpleArcadesScreensaverUtil.h"

#include <memory>
#include <unordered_map>
#include <vector>


GuiSimpleArcadesScreensaverGalleryOptions::GuiSimpleArcadesScreensaverGalleryOptions(Window* window, const char* title)
	: GuiScreensaverOptions(window, title)
{
	if (!initPaths())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow,
			"Simple Arcades screensaver folder was not found.\n\n"
			"Expected:\n"
			"  /home/pi/simplearcades/media/videos/screensavers\n\n"
			"Nothing was changed.",
			"OK", [] { return; }));
		return;
	}

	// Discover + sync allow-list, then build UI toggles
	std::vector<std::string> allRel;
	std::unordered_map<std::string, bool> enabledByRel;
	loadAndSync(allRel, enabledByRel);

	// Bulk actions (no ON/OFF toggle shown for these rows)
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(
			mWindow, "SELECT TO ENABLE ALL", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);

		row.makeAcceptInputHandler([this] {
			for (auto& e : mEntries)
				e.toggle->setState(true);
		});

		addRow(row);
	}

	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(
			mWindow, "SELECT TO DISABLE ALL", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);

		row.makeAcceptInputHandler([this] {
			for (auto& e : mEntries)
				e.toggle->setState(false);
		});

		addRow(row);
	}

	// Help / Info
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(
			mWindow, "HELP / INFO", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);

		row.makeAcceptInputHandler([this] {
			std::string msg;
			msg += "Choose which screensaver videos are allowed to play.\n\n";
			msg += "ON = Video can play\n";
			msg += "OFF = Video will not play\n\n";
			msg += "Tip: Use 'Enable All' or 'Disable All' for quick changes.\n\n";
			msg += "Changes are saved when you exit this menu.";
			mWindow->pushGui(new GuiMsgBox(mWindow, msg, "OK", nullptr));
		});

		addRow(row);
	}


	for (const auto& rel : allRel)
	{
		auto it = enabledByRel.find(rel);
		if (it == enabledByRel.end())
			continue;

		auto sw = std::make_shared<SwitchComponent>(mWindow);
		sw->setState(it->second);

		addWithLabel(prettyLabelFromRel(rel), sw);

		mEntries.push_back({ rel, sw });
	}

	// Save: rewrite allow-list based on toggles
	// IMPORTANT: base class destructor triggers save(); do NOT capture `this`.
	const auto entriesForSave = mEntries;

	// Snapshot initial state so we can avoid rewriting the allowlist when nothing changed.
	std::unordered_map<std::string, bool> initialByRel;
	initialByRel.reserve(entriesForSave.size());
	for (const auto& e : entriesForSave)
		initialByRel[e.relPath] = e.toggle->getState();

	addSaveFunc([entriesForSave, initialByRel] {
		std::vector<std::string> allRel;
		allRel.reserve(entriesForSave.size());

		std::unordered_map<std::string, bool> enabledByRel;
		enabledByRel.reserve(entriesForSave.size());

		for (const auto& e : entriesForSave)
		{
			allRel.push_back(e.relPath);
			enabledByRel[e.relPath] = e.toggle->getState();
		}

		// If user didn't change anything, don't touch the file.
		if (enabledByRel == initialByRel)
			return;

		SimpleArcadesScreensaverUtil::writeSelection(allRel, enabledByRel);
	});
}

GuiSimpleArcadesScreensaverGalleryOptions::~GuiSimpleArcadesScreensaverGalleryOptions()
{
}

bool GuiSimpleArcadesScreensaverGalleryOptions::initPaths()
{
	mRootDir = SimpleArcadesScreensaverUtil::getRootDir();
	mAllowListPath = SimpleArcadesScreensaverUtil::getConfigPath();

	if (!Utils::FileSystem::exists(mRootDir))
	{
		LOG(LogError) << "Simple Arcades Screensaver Gallery - media root not found: " << mRootDir;
		return false;
	}

	return true;
}

std::string GuiSimpleArcadesScreensaverGalleryOptions::prettyLabelFromRel(const std::string& rel)
{
	std::string s = rel;
	const std::string prefix = "generic_screensavers/";
	if (s.compare(0, prefix.size(), prefix) == 0)
		s = s.substr(prefix.size());
	return s;
}

void GuiSimpleArcadesScreensaverGalleryOptions::loadAndSync(
	std::vector<std::string>& allRel,
	std::unordered_map<std::string, bool>& enabledByRel)
{
	if (!SimpleArcadesScreensaverUtil::syncSelection(allRel, enabledByRel))
	{
		LOG(LogError) << "Simple Arcades Screensaver Gallery - failed to load/sync allowlist.";
		mWindow->pushGui(new GuiMsgBox(mWindow,
			"Unable to load the Simple Arcades screensaver list.\n\nSee es_log.txt for details.",
			"OK", nullptr));
		allRel.clear();
		enabledByRel.clear();
	}
}
