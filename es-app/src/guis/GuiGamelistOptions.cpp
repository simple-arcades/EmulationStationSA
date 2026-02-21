#include "GuiGamelistOptions.h"
#include "SAStyle.h"

#include "guis/GuiGamelistFilter.h"
#include "scrapers/Scraper.h"
#include "views/gamelist/IGameListView.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "FileFilterIndex.h"
#include "FileSorts.h"
#include "GuiMetaDataEd.h"
#include "SystemData.h"
#include "components/TextListComponent.h"

// --- Save state deletion support ---
#include "guis/GuiMsgBox.h"
#include "SaveStateDeleteHelper.h"
#include "utils/FileSystemUtil.h"

// --- Saved games context menu ---
#include "guis/GuiSavedGames.h"

// --- Netplay ---
#include "NetplayCore.h"
#include "NetplayLauncher.h"
#include "NetplayConfig.h"
#include "guis/GuiNetplayLobby.h"
#include "guis/GuiTextInput.h"

GuiGamelistOptions::GuiGamelistOptions(Window* window, SystemData* system) : GuiComponent(window),
	mSystem(system), mMenu(window, "OPTIONS"), mFromPlaceholder(false), mFiltersChanged(false),
	mJumpToSelected(false), mMetadataChanged(false), mSavedGamesCount(0)
{
	addChild(&mMenu);

	// check it's not a placeholder folder - if it is, only show "Filter Options"
	FileData* file = getGamelist()->getCursor();
	mFromPlaceholder = file->isPlaceHolder();
	ComponentListRow row;

	if (!mFromPlaceholder) {
		row.elements.clear();

		std::string currentSort = mSystem->getRootFolder()->getSortDescription();
		std::string reqSort = FileSorts::SortTypes.at(0).description;

		// "jump to letter" menuitem only available (and correct jumping) on sort order "name, asc"
		if (currentSort == reqSort) {
			bool outOfRange = false;
			char curChar = (char)toupper(getGamelist()->getCursor()->getSortName()[0]);
			// define supported character range
			// this range includes all numbers, capital letters, and most reasonable symbols
			char startChar = '!';
			char endChar = '_';
			if (curChar < startChar || curChar > endChar) {
				// most likely 8 bit ASCII or Unicode (Prefix: 0xc2 or 0xe2) value
				curChar = startChar;
				outOfRange = true;
			}

			mJumpToLetterList = std::make_shared<LetterList>(mWindow, "JUMP TO ...", false);
			for (char c = startChar; c <= endChar; c++)
			{
				// check if c is a valid first letter in current list
				const std::vector<FileData*>& files = getGamelist()->getCursor()->getParent()->getChildrenListToDisplay();
				for (auto file : files)
				{
					char candidate = (char)toupper(file->getSortName()[0]);
					if (c == candidate)
					{
						mJumpToLetterList->add(std::string(1, c), c, (c == curChar) || outOfRange);
						outOfRange = false; // only override selection on very first c == candidate match
						break;
					}
				}
			}

			row.addElement(std::make_shared<TextComponent>(mWindow, "JUMP TO ...", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
			row.addElement(mJumpToLetterList, false);
			row.input_handler = [&](InputConfig* config, Input input) {
				if(config->isMappedTo("a", input) && input.value)
				{
					jumpToLetter();
					return true;
				}
				else if(mJumpToLetterList->input(config, input))
				{
					return true;
				}
				return false;
			};
			mMenu.addRow(row);
		}

		// add launch system screensaver
		std::string screensaver_behavior = Settings::getInstance()->getString("ScreenSaverBehavior");
		bool useGamelistMedia = screensaver_behavior == "random video" || (screensaver_behavior == "slideshow" && !Settings::getInstance()->getBool("SlideshowScreenSaverCustomMediaSource"));
		bool rpConfigSelected = "settings" == mSystem->getName();
		bool collectionsSelected = mSystem->getName() == CollectionSystemManager::get()->getCustomCollectionsBundle()->getName();

		if (!rpConfigSelected && useGamelistMedia && (!collectionsSelected || collectionsSelected && file->getType() == GAME)) {
			row.elements.clear();
			row.addElement(std::make_shared<TextComponent>(mWindow, "LAUNCH SYSTEM SCREENSAVER", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
			row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::launchSystemScreenSaver, this));
			mMenu.addRow(row);
		}

		// "sort list by" menuitem
		mListSort = std::make_shared<SortList>(mWindow, "SORT GAMES BY", false);
		for(unsigned int i = 0; i < FileSorts::SortTypes.size(); i++)
		{
			const FileData::SortType& sort = FileSorts::SortTypes.at(i);
			mListSort->add(sort.description, &sort, sort.description == currentSort);
		}

		mMenu.addWithLabel("SORT GAMES BY", mListSort);

	}

	// show filtered menu
	if(!Settings::getInstance()->getBool("ForceDisableFilters"))
	{
		row.elements.clear();
		row.addElement(std::make_shared<TextComponent>(mWindow, "FILTER GAMELIST", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.addElement(makeArrow(mWindow), false);
		row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::openGamelistFilter, this));
		mMenu.addRow(row);
	}

	std::map<std::string, CollectionSystemData> customCollections = CollectionSystemManager::get()->getCustomCollectionSystems();

	if(UIModeController::getInstance()->isUIModeFull() &&
		((customCollections.find(system->getName()) != customCollections.cend() && CollectionSystemManager::get()->getEditingCollection() != system->getName()) ||
		CollectionSystemManager::get()->getCustomCollectionsBundle()->getName() == system->getName()))
	{
		row.elements.clear();
		row.addElement(std::make_shared<TextComponent>(mWindow, "ADD/REMOVE GAMES TO THIS GAME COLLECTION", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::startEditMode, this));
		mMenu.addRow(row);
	}

	if(UIModeController::getInstance()->isUIModeFull() && CollectionSystemManager::get()->isEditing())
	{
		row.elements.clear();
		row.addElement(std::make_shared<TextComponent>(mWindow, "FINISH EDITING '" + Utils::String::toUpper(CollectionSystemManager::get()->getEditingCollection()) + "' COLLECTION", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::exitEditMode, this));
		mMenu.addRow(row);
	}

	if(UIModeController::getInstance()->isUIModeFull() && system == CollectionSystemManager::get()->getRandomCollection())
	{
		row.elements.clear();
		row.addElement(std::make_shared<TextComponent>(mWindow, "GET NEW RANDOM GAMES", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::recreateCollection, this));
		mMenu.addRow(row);
	}

	if (UIModeController::getInstance()->isUIModeFull() && !mFromPlaceholder && !(mSystem->isCollection() && file->getType() == FOLDER))
	{
		row.elements.clear();
		std::string lblTxt = std::string("EDIT THIS ");
		lblTxt += std::string((file->getType() == FOLDER ? "FOLDER" : "GAME"));
		lblTxt += std::string("'S METADATA");
		row.addElement(std::make_shared<TextComponent>(mWindow, lblTxt, saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.addElement(makeArrow(mWindow), false);
		row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::openMetaDataEd, this));
		mMenu.addRow(row);
	}

	// ====================================================================
	//  "DELETE THIS SAVE" — only shown in the savestates system
	//
	//  Appears after all other menu items, before the menu is centered.
	//  Only for non-placeholder, non-folder items in the "savestates" system.
	// ====================================================================
	if (!mFromPlaceholder && mSystem->getName() == "savestates" && file->getType() != FOLDER
		&& Utils::FileSystem::getFileName(file->getPath()) != ".donotdelete.entry")
	{
		row.elements.clear();
		row.addElement(std::make_shared<TextComponent>(mWindow, "DELETE THIS SAVE", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::deleteSaveState, this));
		mMenu.addRow(row);
	}

	// ====================================================================
	//  "PLAY ONLINE" — shown when the current game supports netplay.
	//
	//  Requires: game is multiplayer (players >= 2), system's default
	//  core is in the netplay whitelist.
	//  Visible in kiosk mode (no isUIModeFull() gating).
	// ====================================================================
	if (!mFromPlaceholder && mSystem->getName() != "savestates"
		&& mSystem->getName() != "settings" && file->getType() == GAME)
	{
		if (NetplayCore::isGameNetplayCompatible(file))
		{
			row.elements.clear();
			row.addElement(std::make_shared<TextComponent>(mWindow, "PLAY ONLINE",
				saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
			row.addElement(makeArrow(mWindow), false);
			row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::openPlayOnline, this));
			mMenu.addRow(row);
		}
	}

	// ====================================================================
	//  "SAVED GAMES (N)" — shown on non-savestates systems when
	//  save states exist for the currently selected ROM.
	//
	//  Visible in kiosk mode (no isUIModeFull() gating).
	//  Opens GuiSavedGames dialog listing all saves for this ROM.
	// ====================================================================
	// ====================================================================
	if (!mFromPlaceholder && mSystem->getName() != "savestates"
		&& mSystem->getName() != "settings" && file->getType() == GAME)
	{
		std::string currentRomPath = file->getPath();
		std::vector<SaveStateDeleteHelper::SaveEntryInfo> saves =
			SaveStateDeleteHelper::findSavesForRom(currentRomPath);

		if (!saves.empty())
		{
			mSavedGamesRomPath = currentRomPath;
			mSavedGamesRomName = file->getName();
			mSavedGamesCount = (int)saves.size();

			row.elements.clear();
			std::string label = "SAVED GAMES (" + std::to_string(mSavedGamesCount) + ")";
			row.addElement(std::make_shared<TextComponent>(mWindow, label, saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
			row.addElement(makeArrow(mWindow), false);
			row.makeAcceptInputHandler(std::bind(&GuiGamelistOptions::openSavedGames, this));
			mMenu.addRow(row);
		}
	}

	// center the menu
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2, (mSize.y() - mMenu.getSize().y()) / 2);
}

GuiGamelistOptions::~GuiGamelistOptions()
{
	FileData* root = mSystem->getRootFolder();
	// apply sort
	if (!mFromPlaceholder) {
		const FileData::SortType selectedSort = mJumpToSelected ? FileSorts::SortTypes.at(0) /* force "name, asc" */ : *mListSort->getSelected();
		if (root->getSortDescription() != selectedSort.description) {
			root->sort(selectedSort); // will also recursively sort children
			// notify that the root folder was sorted
			getGamelist()->onFileChanged(root, FILE_SORTED);
		}
	}

	if (mFiltersChanged || mMetadataChanged)
	{
		// force refresh of cursor list position
		ViewController::get()->getGameListView(mSystem)->setViewportTop(TextListComponent<FileData>::REFRESH_LIST_CURSOR_POS);
		// re-display the elements for whatever new or renamed game is selected
		ViewController::get()->reloadGameListView(mSystem);
		if (mFiltersChanged) {
			// trigger repaint of cursor and list detail
			getGamelist()->onFileChanged(root, FILE_SORTED);
		}
	}
}

bool GuiGamelistOptions::launchSystemScreenSaver()
{
	SystemData* system = mSystem;
	std::string systemName = system->getName();
	// need to check if we're in a folder inside the collections bundle, to launch from there
	if(systemName == CollectionSystemManager::get()->getCustomCollectionsBundle()->getName())
	{
		FileData* file = getGamelist()->getCursor(); // is GAME otherwise menuentry would have been hidden
		// we are inside a specific collection. We want to launch for that one.
		system = file->getSystem();
	}
	mWindow->startScreenSaver(system);
	mWindow->renderScreenSaver();

	delete this;
	return true;
}

void GuiGamelistOptions::openGamelistFilter()
{
	mFiltersChanged = true;
	GuiGamelistFilter* ggf = new GuiGamelistFilter(mWindow, mSystem);
	mWindow->pushGui(ggf);
}

void GuiGamelistOptions::recreateCollection()
{
	CollectionSystemManager::get()->recreateCollection(mSystem);
	delete this;
}

void GuiGamelistOptions::startEditMode()
{
	std::string editingSystem = mSystem->getName();
	// need to check if we're editing the collections bundle, as we will want to edit the selected collection within
	if(editingSystem == CollectionSystemManager::get()->getCustomCollectionsBundle()->getName())
	{
		FileData* file = getGamelist()->getCursor();
		// do we have the cursor on a specific collection?
		if (file->getType() == FOLDER)
		{
			editingSystem = file->getName();
		}
		else
		{
			// we are inside a specific collection. We want to edit that one.
			editingSystem = file->getSystem()->getName();
		}
	}
	CollectionSystemManager::get()->setEditMode(editingSystem);
	delete this;
}

void GuiGamelistOptions::exitEditMode()
{
	CollectionSystemManager::get()->exitEditMode();
	delete this;
}

void GuiGamelistOptions::openMetaDataEd()
{
	// open metadata editor
	// get the FileData that hosts the original metadata
	FileData* file = getGamelist()->getCursor()->getSourceFileData();
	ScraperSearchParams p;
	p.game = file;
	p.system = file->getSystem();

	std::function<void()> saveBtnFunc;
	saveBtnFunc = [this, file] {
		ViewController::get()->getGameListView(mSystem)->setCursor(file, true);
		mMetadataChanged = true;
		ViewController::get()->getGameListView(file->getSystem())->onFileChanged(file, FILE_METADATA_CHANGED);
	};

	std::function<void()> deleteBtnFunc;
	if (file->getType() == FOLDER)
	{
		deleteBtnFunc = NULL;
	}
	else
	{
		deleteBtnFunc = [this, file] {
			CollectionSystemManager::get()->deleteCollectionFiles(file);
			ViewController::get()->getGameListView(file->getSystem()).get()->remove(file, true, true);
		};
	}

	mWindow->pushGui(new GuiMetaDataEd(mWindow, &file->metadata, file->metadata.getMDD(), p, Utils::FileSystem::getFileName(file->getPath()), saveBtnFunc, deleteBtnFunc));
}

// ============================================================================
//  deleteSaveState
//
//  Main entry point for the "DELETE THIS SAVE" feature.
//  This is called when the user selects the menu option.
//
//  Flow:
//    1. Show confirmation dialog with "This cannot be undone."
//    2. On YES -> Phase 1: delete watcher files, video (if last ref), gamelist entry
//    3. Check if this was the last save for the ROM
//    4. If last save -> Phase 2: scan for save-RAM files, ask user
//    5. Show "DELETED!" confirmation
//    6. Reload the gamelist view
// ============================================================================
void GuiGamelistOptions::deleteSaveState()
{
	// Get the currently selected save entry
	FileData* file = getGamelist()->getCursor();
	if (!file)
		return;

	// Guard: never delete the system placeholder
	std::string filename = Utils::FileSystem::getFileName(file->getPath());
	if (filename == ".donotdelete.entry")
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, "THIS ENTRY CANNOT BE DELETED.", "OK", nullptr));
		return;
	}

	// The entry path is what ES sees as the "ROM" for this save
	std::string entryPath = file->getPath();
	std::string gameName = file->getName();

	// Build the confirmation message
	std::string confirmMsg = "DELETE SAVE STATE?\n\n\"" + gameName + "\"\n\nTHIS CANNOT BE UNDONE.";

	// Capture everything we need by value — this GuiGamelistOptions will be
	// deleted before the callback runs, so we can't use 'this' safely.
	// Instead, capture the window pointer and system pointer.
	Window* window = mWindow;
	SystemData* system = mSystem;

	auto doDelete = [window, system, entryPath, gameName]()
	{
		// ------------------------------------------------------------------
		//  Derive paths from the entry file location
		// ------------------------------------------------------------------
		std::string savestatesDirStr = Utils::FileSystem::getParent(entryPath);

		std::string gamelistPath = savestatesDirStr + "/gamelist.xml";
		std::string savefilesDir = savestatesDirStr + "/savefiles";

		// The entry's relative path as stored in gamelist.xml (usually "./filename.entry")
		// ES stores paths relative to the system ROM dir, prefixed with "./"
		std::string entryFilename = Utils::FileSystem::getFileName(entryPath);
		std::string gamelistRelPath = "./" + entryFilename;

		// ------------------------------------------------------------------
		//  Read the .metadata file to get the ROM path
		//  (needed for save-RAM detection and "last save" check)
		// ------------------------------------------------------------------
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

		// ------------------------------------------------------------------
		//  Check if this is the last save for this ROM BEFORE we delete
		//  (need to check while our .metadata file still exists)
		// ------------------------------------------------------------------
		bool lastSave = false;
		if (hasMetadata)
		{
			lastSave = SaveStateDeleteHelper::isLastSaveForRom(
				savestatesDirStr, metaInfo.romPath, metadataPath);
		}

		// ------------------------------------------------------------------
		//  Get the video path from gamelist BEFORE we remove the entry
		//  (so we can check references and potentially delete it)
		// ------------------------------------------------------------------
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

		// ------------------------------------------------------------------
		//  PHASE 1: Delete watcher-created files
		// ------------------------------------------------------------------
		SaveStateDeleteHelper::deleteWatcherFiles(entryPath);

		// ------------------------------------------------------------------
		//  Handle video deletion — only delete if no other entries reference it
		// ------------------------------------------------------------------
		if (!videoPath.empty())
		{
			int otherRefs = SaveStateDeleteHelper::countVideoReferences(
				gamelistPath, videoPath, gamelistRelPath);

			if (otherRefs == 0)
			{
				// This was the last reference — safe to delete the video
				// The video path in gamelist.xml is relative (e.g. "./media/videos/game.mp4")
				// Resolve it against the savestates directory
				std::string fullVideoPath;
				if (videoPath.length() > 2 && videoPath.substr(0, 2) == "./")
					fullVideoPath = savestatesDirStr + "/" + videoPath.substr(2);
				else
					fullVideoPath = savestatesDirStr + "/" + videoPath;

				if (Utils::FileSystem::exists(fullVideoPath))
				{
					if (Utils::FileSystem::removeFile(fullVideoPath))
						LOG(LogInfo) << "SaveStateDeleteHelper: Deleted video (last reference): " << fullVideoPath;
					else
						LOG(LogError) << "SaveStateDeleteHelper: Failed to delete video: " << fullVideoPath;
				}
			}
			else
			{
				LOG(LogInfo) << "SaveStateDeleteHelper: Video still referenced by " << otherRefs << " other save(s), keeping: " << videoPath;
			}
		}

		// ------------------------------------------------------------------
		//  Remove the gamelist.xml entry
		// ------------------------------------------------------------------
		SaveStateDeleteHelper::removeGamelistEntry(gamelistPath, gamelistRelPath);

		// ------------------------------------------------------------------
		//  Remove from ES's in-memory data and reload the view
		// ------------------------------------------------------------------
		// Use the same pattern as the metadata editor's delete:
		// remove() handles both the internal FileData tree and the view refresh
		ViewController::get()->getGameListView(system).get()->remove(
			ViewController::get()->getGameListView(system)->getCursor(), true, true);

		// ------------------------------------------------------------------
		//  If we just deleted the last real save, unhide the placeholder
		//  so the user sees "NO SAVED GAMES YET" instead of an empty system
		// ------------------------------------------------------------------
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
					// No real saves left — unhide the placeholder
					pugi::xml_node hiddenNode = placeholderNode.child("hidden");
					if (hiddenNode)
						hiddenNode.text().set("false");
					else
						placeholderNode.append_child("hidden").text().set("false");

					doc.save_file(gamelistPath.c_str());
					LOG(LogInfo) << "SaveStateDeleteHelper: Last real save deleted, unhid placeholder";

					// Also update in-memory FileData so ES sees it immediately
					FileData* rootFolder = system->getRootFolder();
					if (rootFolder)
					{
						std::vector<FileData*> files = rootFolder->getFilesRecursive(GAME);
						for (auto* f : files)
						{
							if (Utils::FileSystem::getFileName(f->getPath()) == ".donotdelete.entry")
							{
								f->metadata.set("hidden", "false");
								break;
							}
						}
					}

					// Rebuild the gamelist view so the placeholder appears
					ViewController::get()->reloadGameListView(system, false);
				}
			}
		}

		// ------------------------------------------------------------------
		//  PHASE 2: If this was the last save for the ROM, check for save-RAM
		// ------------------------------------------------------------------
		if (lastSave && hasMetadata)
		{
			std::string romFilename = SaveStateDeleteHelper::getFilename(metaInfo.romPath);
			std::vector<std::string> saveRamFiles = SaveStateDeleteHelper::findSaveRamFiles(savefilesDir, romFilename);

			if (!saveRamFiles.empty())
			{
				// Build a list of filenames for the dialog
				std::string fileListStr;
				for (const auto& f : saveRamFiles)
				{
					fileListStr += "  " + SaveStateDeleteHelper::getFilename(f) + "\n";
				}

				std::string phase2Msg =
					"SAVE-RAM FILES FOUND\n\n"
					"THIS WAS YOUR LAST SAVE STATE FOR THIS GAME.\n"
					"THE FOLLOWING IN-GAME SAVE FILES WERE FOUND:\n\n" +
					fileListStr +
					"\nDELETE THESE FILES TOO?\n\n"
					"THESE ARE IN-GAME PROGRESS FILES\n"
					"(MEMORY CARDS, BATTERY SAVES, ETC.)";

				// Capture saveRamFiles for the YES callback
				auto deleteRamFiles = [saveRamFiles, window]()
				{
					for (const auto& f : saveRamFiles)
					{
						if (Utils::FileSystem::removeFile(f))
							LOG(LogInfo) << "SaveStateDeleteHelper: Deleted save-RAM file: " << f;
						else
							LOG(LogError) << "SaveStateDeleteHelper: Failed to delete save-RAM file: " << f;
					}

					// Show "DELETED!" confirmation after save-RAM cleanup
					window->pushGui(new GuiMsgBox(window, "DELETED!", "OK", nullptr));
				};

				// Show "DELETED!" even if user says NO to save-RAM
				// (Phase 1 files were already deleted)
				auto skipRamFiles = [window]()
				{
					window->pushGui(new GuiMsgBox(window, "DELETED!", "OK", nullptr));
				};

				window->pushGui(new GuiMsgBox(window, phase2Msg, "YES", deleteRamFiles, "NO", skipRamFiles));
				return; // Phase 2 dialog handles showing "DELETED!"
			}
		}

		// ------------------------------------------------------------------
		//  No Phase 2 needed — show "DELETED!" confirmation
		// ------------------------------------------------------------------
		window->pushGui(new GuiMsgBox(window, "DELETED!", "OK", nullptr));
	};

	// Show the initial confirmation dialog
	// After showing the confirmation, close this options menu
	mWindow->pushGui(new GuiMsgBox(mWindow, confirmMsg, "YES", doDelete, "NO", nullptr));
	delete this;
}

// ============================================================================
//  openPlayOnline
//
//  Shows HOST THIS GAME / FIND A MATCH submenu.
// ============================================================================
void GuiGamelistOptions::openPlayOnline()
{
	FileData* file = getGamelist()->getCursor();
	Window* window = mWindow;
	std::string gameName = file->getName();

	// Close this options menu
	delete this;

	// Show HOST / JOIN choice
	window->pushGui(new GuiMsgBox(window,
		"PLAY ONLINE\n\n" + Utils::String::toUpper(gameName),
		"HOST THIS GAME", [window, file]
		{
			NetplayConfig& cfg = NetplayConfig::get();

			// Prompt for player name if still default
			if (cfg.nickname == "Player" || cfg.nickname.empty())
			{
				window->pushGui(new GuiTextInput(window,
					"ENTER YOUR PLAYER NAME:",
					cfg.nickname.empty() ? "Player" : cfg.nickname,
					[window, file](const std::string& result)
					{
						std::string cleaned = NetplayConfig::sanitizeNickname(result);
						if (cleaned.empty()) cleaned = "Player";
						NetplayConfig::get().nickname = cleaned;
						NetplayConfig::get().save();

						// Now show host confirmation
						NetplayConfig& cfg2 = NetplayConfig::get();
						NetplayGameInfo info = NetplayCore::getGameInfo(file);

						std::string safetyNote;
						if (info.safety == NetplaySafety::STRICT)
							safetyNote = "\n\nNOTE: THIS GAME REQUIRES BOTH PLAYERS\nTO USE THE SAME TYPE OF ARCADE.";

						std::string msg =
							"START HOSTING?\n\n"
							"GAME: " + Utils::String::toUpper(file->getName()) + "\n"
							"PLAYER: " + Utils::String::toUpper(cfg2.nickname) + "\n"
							"MODE: " + Utils::String::toUpper(cfg2.mode == "lan" ? "LAN" : cfg2.onlineMethod) +
							safetyNote;

						window->pushGui(new GuiMsgBox(window, msg,
							"START", [window, file]
							{
								NetplayLauncher::launchAsHost(window, file);
							},
							"CANCEL", nullptr));
					}));
				return;
			}

			NetplayGameInfo info = NetplayCore::getGameInfo(file);

			std::string safetyNote;
			if (info.safety == NetplaySafety::STRICT)
				safetyNote = "\n\nNOTE: THIS GAME REQUIRES BOTH PLAYERS\nTO USE THE SAME TYPE OF ARCADE.";

			std::string msg =
				"START HOSTING?\n\n"
				"GAME: " + Utils::String::toUpper(file->getName()) + "\n"
				"PLAYER: " + Utils::String::toUpper(cfg.nickname) + "\n"
				"MODE: " + Utils::String::toUpper(cfg.mode == "lan" ? "LAN" : cfg.onlineMethod) +
				safetyNote;

			window->pushGui(new GuiMsgBox(window, msg,
				"START", [window, file]
				{
					NetplayLauncher::launchAsHost(window, file);
				},
				"CANCEL", nullptr));
		},
		"FIND A MATCH", [window, file]
		{
			// Search the lobby for sessions matching this specific game
			std::string gameName = file->getName();
			std::string systemName = file->getSystem()->getName();
			window->pushGui(new GuiNetplayLobby(window, gameName, systemName));
		},
		"CANCEL", nullptr));
}

// ============================================================================
//  openSavedGames
//
//  Opens the GuiSavedGames dialog showing all save states for the
//  currently selected ROM.
// ============================================================================
void GuiGamelistOptions::openSavedGames()
{
	// Capture what we need before deleting this menu
	Window* window = mWindow;
	std::string romPath = mSavedGamesRomPath;
	std::string romName = mSavedGamesRomName;

	// Close this options menu so the user doesn't return to a stale
	// "SAVED GAMES (N)" count after deleting saves.
	delete this;

	window->pushGui(new GuiSavedGames(window, romPath, romName));
}

void GuiGamelistOptions::jumpToLetter()
{
	char letter = mJumpToLetterList->getSelected();
	IGameListView* gamelist = getGamelist();

	// this is a really shitty way to get a list of files
	const std::vector<FileData*>& files = gamelist->getCursor()->getParent()->getChildrenListToDisplay();

	long min = 0;
	long max = (long)files.size() - 1;
	long mid = 0;

	while(max >= min)
	{
		mid = ((max - min) / 2) + min;

		// game somehow has no first character to check
		if(files.at(mid)->getName().empty())
			continue;

		char checkLetter = (char)toupper(files.at(mid)->getSortName()[0]);

		if(checkLetter < letter)
			min = mid + 1;
		else if(checkLetter > letter || (mid > 0 && (letter == toupper(files.at(mid - 1)->getSortName()[0]))))
			max = mid - 1;
		else
			break; //exact match found
	}

	gamelist->setCursor(files.at(mid));

	// flag to force default sort order "name, asc", if user changed the sortorder in the options dialog
	mJumpToSelected = true;

	delete this;
}

bool GuiGamelistOptions::input(InputConfig* config, Input input)
{
	if((config->isMappedTo("b", input) || config->isMappedTo("select", input)) && input.value)
	{
		delete this;
		return true;
	}

	return mMenu.input(config, input);
}

HelpStyle GuiGamelistOptions::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	style.applyTheme(mSystem->getTheme(), "system");
	return style;
}

std::vector<HelpPrompt> GuiGamelistOptions::getHelpPrompts()
{
	auto prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "close"));
	return prompts;
}

IGameListView* GuiGamelistOptions::getGamelist()
{
	return ViewController::get()->getGameListView(mSystem).get();
}