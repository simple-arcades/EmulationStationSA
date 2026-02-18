// ============================================================================
//  GuiSavedGames.cpp
//
//  Shows all save states for a specific ROM in a menu dialog.
//  Each entry shows a small thumbnail + slot label with timestamp.
//  Selecting an entry opens GuiSaveStatePreview with a large screenshot
//  and LOAD / DELETE / CANCEL buttons.
//
//  Launched from the Select menu on any non-savestates system.
// ============================================================================
#include "guis/GuiSavedGames.h"
#include "guis/GuiSaveStatePreview.h"
#include "SAStyle.h"

#include "guis/GuiMsgBox.h"
#include "views/ViewController.h"
#include "views/gamelist/IGameListView.h"
#include "components/ImageComponent.h"
#include "SystemData.h"
#include "Window.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "FileFilterIndex.h"
#include "Gamelist.h"

#include <pugixml.hpp>

// Thumbnail height in rows (pixels) — adjust for your font/screen
#define THUMB_HEIGHT 48.0f

// ============================================================================
//  Constructor
// ============================================================================
GuiSavedGames::GuiSavedGames(Window* window, const std::string& romPath, const std::string& romName)
	: GuiComponent(window),
	  mMenu(window, "SAVED GAMES"),
	  mRomPath(romPath),
	  mRomName(romName)
{
	addChild(&mMenu);
	populateList();
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2, (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  Helper: extract timestamp from description
// ============================================================================
static std::string extractTimestamp(const std::string& description)
{
	if (description.empty())
		return "";

	// Description format from watcher:
	// "Save state (slot N) for GameName on System created MM/DD/YYYY at HH:MMpm TZ"
	size_t createdPos = description.find("created ");
	if (createdPos != std::string::npos)
		return description.substr(createdPos + 8);

	return "";
}

// ============================================================================
//  populateList
//
//  Scans for saves matching mRomPath and builds menu rows.
//  Each row has: [thumbnail] [slot label + timestamp] [arrow]
// ============================================================================
void GuiSavedGames::populateList()
{
	mSaves = SaveStateDeleteHelper::findSavesForRom(mRomPath);

	if (mSaves.empty())
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow, "NO SAVED GAMES FOUND",
			saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row);
		return;
	}

	for (const auto& save : mSaves)
	{
		ComponentListRow row;

		// Thumbnail with right padding
		if (!save.imagePath.empty() && Utils::FileSystem::exists(save.imagePath))
		{
			auto thumb = std::make_shared<ImageComponent>(mWindow);
			thumb->setImage(save.imagePath);
			// Scale to fixed height, preserving aspect ratio
			thumb->setMaxSize(THUMB_HEIGHT * 1.5f, THUMB_HEIGHT);
			row.addElement(thumb, false);

			// Spacer between thumbnail and text
			auto spacer = std::make_shared<GuiComponent>(mWindow);
			spacer->setSize(20, 0);
			row.addElement(spacer, false);
		}

		// Label: "SLOT 1 — 02/14/2026 at 09:22pm"
		std::string label = "SLOT " + std::to_string(save.slotNumber);
		std::string ts = extractTimestamp(save.description);
		if (!ts.empty())
			label += " - " + ts;

		row.addElement(std::make_shared<TextComponent>(mWindow, label,
			saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.addElement(makeArrow(mWindow), false);

		// Capture save by value for lambda
		SaveStateDeleteHelper::SaveEntryInfo capturedSave = save;

		row.makeAcceptInputHandler([this, capturedSave]() {
			openPreview(capturedSave);
		});

		mMenu.addRow(row);
	}
}

// ============================================================================
//  openPreview
//
//  Opens GuiSaveStatePreview with large screenshot + LOAD/DELETE/CANCEL.
// ============================================================================
void GuiSavedGames::openPreview(const SaveStateDeleteHelper::SaveEntryInfo& save)
{
	// Build detail text
	std::string detail = extractTimestamp(save.description);

	auto loadFunc = [this, save]() {
		launchSave(save);
	};

	auto deleteFunc = [this, save]() {
		deleteSave(save);
	};

	mWindow->pushGui(new GuiSaveStatePreview(
		mWindow,
		save.displayName,
		save.imagePath,
		detail,
		loadFunc,
		deleteFunc
	));
}

// ============================================================================
//  launchSave
//
//  Launches the save state by finding the entry in the savestates system
//  and calling launchGame() on it.
// ============================================================================
void GuiSavedGames::launchSave(const SaveStateDeleteHelper::SaveEntryInfo& entry)
{
	// Find the savestates system
	SystemData* savestatesSystem = nullptr;
	for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
	{
		if ((*it)->getName() == "savestates")
		{
			savestatesSystem = *it;
			break;
		}
	}

	if (!savestatesSystem)
	{
		LOG(LogError) << "GuiSavedGames: Could not find savestates system";
		mWindow->pushGui(new GuiMsgBox(mWindow, "ERROR: SAVED GAMES SYSTEM NOT FOUND", "OK", nullptr));
		return;
	}

	// Find the FileData for this entry
	FileData* rootFolder = savestatesSystem->getRootFolder();
	std::string targetFilename = Utils::FileSystem::getFileName(entry.entryPath);
	FileData* targetFile = nullptr;

	std::vector<FileData*> allGames = rootFolder->getFilesRecursive(GAME);
	for (auto* game : allGames)
	{
		if (Utils::FileSystem::getFileName(game->getPath()) == targetFilename)
		{
			targetFile = game;
			break;
		}
	}

	if (!targetFile)
	{
		LOG(LogError) << "GuiSavedGames: Could not find FileData for: " << entry.entryPath;
		mWindow->pushGui(new GuiMsgBox(mWindow, "ERROR: SAVE STATE ENTRY NOT FOUND", "OK", nullptr));
		return;
	}

	// Close this dialog
	Window* window = mWindow;
	delete this;

	// Launch the save state
	targetFile->launchGame(window);
}

// ============================================================================
//  deleteSave
//
//  Deletes a save state using the same logic as the savestates system's
//  delete feature, then refreshes this dialog.
// ============================================================================
void GuiSavedGames::deleteSave(const SaveStateDeleteHelper::SaveEntryInfo& entry)
{
	std::string confirmMsg = "DELETE SAVE STATE?\n\n\"" + entry.displayName + "\"\n\nTHIS CANNOT BE UNDONE.";

	// Capture what we need — we'll delete 'this' before the actual
	// deletion runs so we can reopen a fresh dialog afterward.
	Window* window = mWindow;
	std::string romPath = mRomPath;
	std::string romName = mRomName;
	std::string entryPath = entry.entryPath;

	auto doDelete = [window, entryPath, romPath, romName]()
	{
		std::string savestatesDirStr = Utils::FileSystem::getParent(entryPath);
		std::string gamelistPath = savestatesDirStr + "/gamelist.xml";
		std::string savefilesDir = savestatesDirStr + "/savefiles";

		std::string entryFilename = Utils::FileSystem::getFileName(entryPath);
		std::string gamelistRelPath = "./" + entryFilename;

		// Read the .metadata file for ROM path
		std::string entrySuffix = ".entry";
		std::string basePath = entryPath;
		if (basePath.length() > entrySuffix.length() &&
		    basePath.compare(basePath.length() - entrySuffix.length(), entrySuffix.length(), entrySuffix) == 0)
		{
			basePath = basePath.substr(0, basePath.length() - entrySuffix.length());
		}
		std::string metadataPath = basePath + ".metadata";

		SaveStateDeleteHelper::MetadataInfo metaInfo;
		bool hasMetadata = SaveStateDeleteHelper::parseMetadataFile(metadataPath, metaInfo);

		// Check if last save BEFORE deleting
		bool lastSave = false;
		if (hasMetadata)
		{
			lastSave = SaveStateDeleteHelper::isLastSaveForRom(
				savestatesDirStr, metaInfo.romPath, metadataPath);
		}

		// Get video path before removal
		std::string videoPath;
		{
			pugi::xml_document doc;
			if (doc.load_file(gamelistPath.c_str()))
			{
				for (pugi::xml_node game = doc.child("gameList").child("game"); game; game = game.next_sibling("game"))
				{
					if (std::string(game.child("path").text().as_string()) == gamelistRelPath)
					{
						videoPath = game.child("video").text().as_string();
						break;
					}
				}
			}
		}

		// Phase 1: Delete watcher files
		SaveStateDeleteHelper::deleteWatcherFiles(entryPath);

		// Handle video deletion
		if (!videoPath.empty())
		{
			int otherRefs = SaveStateDeleteHelper::countVideoReferences(
				gamelistPath, videoPath, gamelistRelPath);

			if (otherRefs == 0)
			{
				std::string fullVideoPath;
				if (videoPath.length() > 2 && videoPath.substr(0, 2) == "./")
					fullVideoPath = savestatesDirStr + "/" + videoPath.substr(2);
				else
					fullVideoPath = savestatesDirStr + "/" + videoPath;

				if (Utils::FileSystem::exists(fullVideoPath))
				{
					Utils::FileSystem::removeFile(fullVideoPath);
					LOG(LogInfo) << "GuiSavedGames: Deleted video: " << fullVideoPath;
				}
			}
		}

		// Remove gamelist entry
		SaveStateDeleteHelper::removeGamelistEntry(gamelistPath, gamelistRelPath);

		// Remove from ES's in-memory data
		SystemData* saveSystem = nullptr;
		for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
		{
			if ((*it)->getName() == "savestates")
			{
				saveSystem = *it;
				break;
			}
		}

		if (saveSystem)
		{
			// Find and remove the FileData
			FileData* rootFolder = saveSystem->getRootFolder();
			std::string targetFilename = Utils::FileSystem::getFileName(entryPath);
			FileData* targetFile = nullptr;

			std::vector<FileData*> allGames = rootFolder->getFilesRecursive(GAME);
			for (auto* game : allGames)
			{
				if (Utils::FileSystem::getFileName(game->getPath()) == targetFilename)
				{
					targetFile = game;
					break;
				}
			}

			if (targetFile)
			{
				ViewController::get()->getGameListView(saveSystem).get()->remove(
					targetFile, true, true);
			}

			// If last save, unhide placeholder
			if (lastSave)
			{
				pugi::xml_document doc;
				if (doc.load_file(gamelistPath.c_str()))
				{
					pugi::xml_node root = doc.child("gameList");
					bool hasRealEntries = false;
					pugi::xml_node placeholderNode;

					for (pugi::xml_node game = root.child("game"); game; game = game.next_sibling("game"))
					{
						std::string path = game.child("path").text().as_string();
						if (path == "./.donotdelete.entry")
							placeholderNode = game;
						else
							hasRealEntries = true;
					}

					if (!hasRealEntries && placeholderNode)
					{
						pugi::xml_node hiddenNode = placeholderNode.child("hidden");
						if (hiddenNode)
							hiddenNode.text().set("false");
						else
							placeholderNode.append_child("hidden").text().set("false");

						doc.save_file(gamelistPath.c_str());
						LOG(LogInfo) << "GuiSavedGames: Last real save deleted, unhid placeholder";

						FileData* rf = saveSystem->getRootFolder();
						if (rf)
						{
							std::vector<FileData*> files = rf->getFilesRecursive(GAME);
							for (auto* f : files)
							{
								if (Utils::FileSystem::getFileName(f->getPath()) == ".donotdelete.entry")
								{
									f->metadata.set("hidden", "false");
									break;
								}
							}
						}
						ViewController::get()->reloadGameListView(saveSystem, false);
					}
				}
			}

			// Rebuild filter index
			FileFilterIndex* idx = saveSystem->getIndex();
			idx->resetIndex();
			FileData* rf = saveSystem->getRootFolder();
			std::vector<FileData*> allFiles = rf->getFilesRecursive(GAME);
			for (auto* game : allFiles)
			{
				if (game->metadata.get("hidden").empty())
					game->metadata.set("hidden", "false");
				idx->addToIndex(game);
			}
			idx->setUIModeFilters();

			ViewController::get()->reloadSystemListView();
		}

		// Phase 2: save-RAM check
		if (lastSave && hasMetadata)
		{
			std::string romFilename = SaveStateDeleteHelper::getFilename(metaInfo.romPath);
			std::vector<std::string> saveRamFiles = SaveStateDeleteHelper::findSaveRamFiles(savefilesDir, romFilename);

			if (!saveRamFiles.empty())
			{
				std::string fileListStr;
				for (const auto& f : saveRamFiles)
					fileListStr += "  " + SaveStateDeleteHelper::getFilename(f) + "\n";

				std::string phase2Msg =
					"SAVE-RAM FILES FOUND\n\n"
					"THIS WAS YOUR LAST SAVE STATE FOR THIS GAME.\n"
					"THE FOLLOWING IN-GAME SAVE FILES WERE FOUND:\n\n" +
					fileListStr +
					"\nDELETE THESE FILES TOO?\n\n"
					"THESE ARE IN-GAME PROGRESS FILES\n"
					"(MEMORY CARDS, BATTERY SAVES, ETC.)";

				auto deleteRamFiles = [saveRamFiles, window, romPath, romName]()
				{
					for (const auto& f : saveRamFiles)
					{
						if (Utils::FileSystem::removeFile(f))
							LOG(LogInfo) << "GuiSavedGames: Deleted save-RAM file: " << f;
					}
					// Check if more saves remain, reopen dialog if so
					auto remaining = SaveStateDeleteHelper::findSavesForRom(romPath);
					if (!remaining.empty())
						window->pushGui(new GuiSavedGames(window, romPath, romName));
				};

				auto skipRamFiles = [window, romPath, romName]()
				{
					// Check if more saves remain, reopen dialog if so
					auto remaining = SaveStateDeleteHelper::findSavesForRom(romPath);
					if (!remaining.empty())
						window->pushGui(new GuiSavedGames(window, romPath, romName));
				};

				window->pushGui(new GuiMsgBox(window, phase2Msg, "YES", deleteRamFiles, "NO", skipRamFiles));
				return;
			}
		}

		// No save-RAM phase — check if more saves remain, reopen dialog if so
		auto remaining = SaveStateDeleteHelper::findSavesForRom(romPath);
		if (!remaining.empty())
			window->pushGui(new GuiSavedGames(window, romPath, romName));
	};

	// Close this GuiSavedGames dialog BEFORE showing the confirm dialog.
	// The doDelete lambda will reopen a fresh one if saves remain.
	Window* win = mWindow;
	std::string rp = mRomPath;
	std::string rn = mRomName;
	delete this;

	win->pushGui(new GuiMsgBox(win, confirmMsg, "YES", doDelete, "NO", [win, rp, rn]() {
		// User cancelled — reopen the saved games dialog
		win->pushGui(new GuiSavedGames(win, rp, rn));
	}));
}

// ============================================================================
//  Input handling
// ============================================================================
bool GuiSavedGames::input(InputConfig* config, Input input)
{
	if ((config->isMappedTo("b", input) || config->isMappedTo("select", input)) && input.value)
	{
		delete this;
		return true;
	}

	return mMenu.input(config, input);
}

HelpStyle GuiSavedGames::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	return style;
}

std::vector<HelpPrompt> GuiSavedGames::getHelpPrompts()
{
	auto prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}
