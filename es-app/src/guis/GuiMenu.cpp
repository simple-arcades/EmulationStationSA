#include "guis/GuiMenu.h"

#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "guis/GuiCollectionSystemsOptions.h"
#include "guis/GuiDetectDevice.h"
#include "guis/GuiGeneralScreensaverOptions.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiScraperStart.h"
#include "guis/GuiSettings.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "EmulationStation.h"
#include "Scripting.h"
#include "SystemData.h"
#include "VolumeControl.h"
#include "AudioManager.h"
#include <SDL_events.h>
#include <algorithm>
#include <set>
#include "platform.h"
#include "FileSorts.h"
#include "views/gamelist/IGameListView.h"
#include "guis/GuiInfoPopup.h"
#include "SAStyle.h"
#include "guis/GuiImageViewer.h"
#include "InputManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "pugixml.hpp"
#include "renderers/Renderer.h"
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cctype>

namespace
{
	struct ControllerProfile
	{
		std::string name;
		std::string guid;
	};
	
	static std::string normalizeSpaces(const std::string& in)
	{
		std::string out;
		out.reserve(in.size());
		bool inSpace = false;

		for (unsigned char ch : in)
		{
			if (std::isspace(ch))
			{
				if (!out.empty() && !inSpace)
					out.push_back(' ');
				inSpace = true;
			}
			else
			{
				out.push_back((char)ch);
				inSpace = false;
			}
		}

		if (!out.empty() && out.back() == ' ')
			out.pop_back();

		return out;
	}

	static std::string ellipsize(const std::string& s, size_t maxLen)
	{
		if (s.size() <= maxLen) return s;
		if (maxLen <= 3) return s.substr(0, maxLen);
		return s.substr(0, maxLen - 3) + "...";
	}

	static bool isBlacklistedDeviceName(const std::string& name)
	{
		// Block ALL DragonRise (built-ins + any external DragonRise you don't want users adding)
		return (name.find("DragonRise") != std::string::npos);
	}

	static std::vector<ControllerProfile> getDeletableControllerProfiles()
	{
		std::vector<ControllerProfile> out;

		const std::string cfgPath = InputManager::getConfigPath();
		if (!Utils::FileSystem::exists(cfgPath))
			return out;

		pugi::xml_document doc;
		pugi::xml_parse_result res = doc.load_file(cfgPath.c_str());
		if (!res)
			return out;

		pugi::xml_node root = doc.child("inputList");
		if (!root)
			return out;

		std::set<std::string> seenGuids;

		for (pugi::xml_node n = root.child("inputConfig"); n; n = n.next_sibling("inputConfig"))
		{
			const std::string type = n.attribute("type").as_string();
			const std::string devName = n.attribute("deviceName").as_string();
			const std::string guid = n.attribute("deviceGUID").as_string();

			// Only allow deleting joystick profiles (never keyboard)
			if (type != "joystick")
				continue;

			// Never show DragonRise in delete list
			if (isBlacklistedDeviceName(devName))
				continue;

			if (guid.empty())
				continue;

			// De-dupe by GUID
			if (!seenGuids.insert(guid).second)
				continue;

			ControllerProfile p;
			p.name = ellipsize(normalizeSpaces(devName), 32);
			p.guid = guid;
			out.push_back(p);
		}

		return out;
	}
	
	static std::string normalizeDeviceName(const std::string& s)
	{
		std::string out;
		bool inSpace = false;

		for (size_t i = 0; i < s.size(); i++)
		{
			unsigned char c = (unsigned char)s[i];
			if (std::isspace(c))
			{
				if (!out.empty() && !inSpace)
				{
					out.push_back(' ');
					inSpace = true;
				}
			}
			else
			{
				out.push_back((char)c);
				inSpace = false;
			}
		}

		// trim trailing space
		if (!out.empty() && out.back() == ' ')
			out.pop_back();

		return out;
	}

	static bool endsWith(const std::string& s, const std::string& suffix)
	{
		if (s.size() < suffix.size())
			return false;
		return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
	}

	static bool retroArchAutoconfigMatchesDevice(const std::string& filePath, const std::string& wantedNorm)
	{
		std::ifstream in(filePath.c_str());
		if (!in)
			return false;

		std::stringstream buf;
		buf << in.rdbuf();
		const std::string content = buf.str();

		std::istringstream ss(content);
		std::string line;
		while (std::getline(ss, line))
		{
			const std::string low = Utils::String::toLower(line);
			if (low.find("input_device") == std::string::npos)
				continue;

			// Expect something like: input_device = "Device Name"
			const size_t q1 = line.find('"');
			if (q1 == std::string::npos) continue;
			const size_t q2 = line.find('"', q1 + 1);
			if (q2 == std::string::npos) continue;

			const std::string val = line.substr(q1 + 1, q2 - q1 - 1);
			if (normalizeDeviceName(val) == wantedNorm)
				return true;
		}

		return false;
	}

	static void deleteRetroArchAutoconfigRecursive(const std::string& dirPath, const std::string& wantedNorm)
	{
		DIR* d = opendir(dirPath.c_str());
		if (!d)
			return;

		struct dirent* ent = nullptr;
		while ((ent = readdir(d)) != nullptr)
		{
			const std::string name = ent->d_name;
			if (name == "." || name == "..")
				continue;

			const std::string path = dirPath + "/" + name;

			struct stat st;
			if (stat(path.c_str(), &st) != 0)
				continue;

			if (S_ISDIR(st.st_mode))
			{
				deleteRetroArchAutoconfigRecursive(path, wantedNorm);
			}
			else if (S_ISREG(st.st_mode))
			{
				if (!endsWith(name, ".cfg"))
					continue;

				if (retroArchAutoconfigMatchesDevice(path, wantedNorm))
				{
					unlink(path.c_str()); // hard delete
				}
			}
		}

		closedir(d);
	}

	static void deleteRetroArchAutoconfigForDeviceName(const std::string& deviceName)
	{
		if (deviceName.empty())
			return;

		const std::string root = "/opt/retropie/configs/all/retroarch/autoconfig";
		if (!Utils::FileSystem::exists(root))
			return;

		const std::string wantedNorm = normalizeDeviceName(deviceName);
		deleteRetroArchAutoconfigRecursive(root, wantedNorm);
	}


	static bool deleteControllerProfileByGuid(const std::string& guid)
	{
		const std::string cfgPath = InputManager::getConfigPath();
		if (!Utils::FileSystem::exists(cfgPath))
			return false;

		pugi::xml_document doc;
		pugi::xml_parse_result res = doc.load_file(cfgPath.c_str());
		if (!res)
			return false;

		pugi::xml_node root = doc.child("inputList");
		if (!root)
			return false;

		bool removedAny = false;

		std::string removedDeviceName;

		for (pugi::xml_node n = root.child("inputConfig"); n;)
		{
			pugi::xml_node next = n.next_sibling("inputConfig");

			const std::string type = n.attribute("type").as_string();
			const std::string nodeGuid = n.attribute("deviceGUID").as_string();
			const std::string devName = n.attribute("deviceName").as_string();

			// Only delete joystick profiles, never keyboard
			if (type == "joystick" && nodeGuid == guid)
			{
				// Extra safety: never delete DragonRise even if GUID matched somehow
				if (!isBlacklistedDeviceName(devName))
				{
					removedDeviceName = devName;
					root.remove_child(n);
					removedAny = true;
				}
			}

			n = next;
		}
		
		
		if (!removedAny)
			return false;

		const bool saved = doc.save_file(cfgPath.c_str());
		if (saved && !removedDeviceName.empty())
		{
			// Also remove RetroArch autoconfigs created for this controller
			deleteRetroArchAutoconfigForDeviceName(removedDeviceName);
		}
		return saved;
	}
}

void GuiMenu::openDeleteControllerProfile()
{
	auto profiles = getDeletableControllerProfiles();

	if (profiles.empty())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow,
			"NO EXTERNAL CONTROLLER PROFILES FOUND.\n\n"
			"CONNECT AN EXTERNAL CONTROLLER,\n"
			"THEN CONFIGURE IT FIRST.",
			"OK", nullptr));
		return;
	}

	auto s = new GuiSettings(mWindow, "DELETE CONTROLLER PROFILE");
	
	auto profileList = std::make_shared<OptionListComponent<std::string>>(mWindow, "CONTROLLER", false);
	bool first = true;
	for (auto& p : profiles)
	{
		profileList->add(p.name, p.guid, first);
		first = false;
	}
	s->addWithLabel("CONTROLLER", profileList);

	ComponentListRow row;
	row.makeAcceptInputHandler([this, profileList, profiles]
	{
		const std::string guid = profileList->getSelected();

		std::string pickedName = "";
		for (auto& p : profiles)
		{
			if (p.guid == guid) { pickedName = p.name; break; }
		}

		mWindow->pushGui(new GuiMsgBox(mWindow,
			"DELETE CONTROLLER PROFILE?\n\n" + pickedName + "\n\n"
			"THIS CANNOT BE UNDONE.",
			"YES",
			[this, guid]
			{
				if (deleteControllerProfileByGuid(guid))
				{
					auto restart_es_fx = []() {
						Scripting::fireEvent("quit");
						quitES(QuitMode::RESTART);
					};
					
					mWindow->pushGui(new GuiMsgBox(mWindow,
						"PROFILE DELETED.\nRESTART REQUIRED.\n\nPRESS OK TO RESTART.",
						"OK", restart_es_fx));
				}
				else
				{
					mWindow->pushGui(new GuiMsgBox(mWindow,
						"DELETE FAILED.\n\nNOTHING CHANGED.",
						"OK", nullptr));
				}
			},
			"NO", nullptr));
	});

	row.addElement(std::make_shared<TextComponent>(mWindow,
		"DELETE SELECTED PROFILE", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	row.addElement(makeArrow(mWindow), false);
	s->addRow(row);

	mWindow->pushGui(s);
}






	// --- Music v2 Pass 2: helper to clean track display names ---
	static std::string musicCleanName(const std::string& raw)
	{
		std::string s = raw;

		// Strip .mp3/.MP3 extension.
		{
			const size_t dot = s.rfind('.');
			if (dot != std::string::npos)
			{
				std::string ext = s.substr(dot);
				for (auto& c : ext) c = tolower(c);
				if (ext == ".mp3")
					s = s.substr(0, dot);
			}
		}

		// Replace underscores with spaces.
		for (auto& c : s)
			if (c == '_') c = ' ';

		// Collapse multiple spaces.
		std::string out;
		bool lastSp = false;
		for (char c : s)
		{
			if (c == ' ') { if (!lastSp) out += c; lastSp = true; }
			else { out += c; lastSp = false; }
		}

		// Trim leading/trailing spaces.
		while (!out.empty() && out.front() == ' ') out.erase(out.begin());
		while (!out.empty() && out.back() == ' ') out.pop_back();

		// Title case.
		bool capNext = true;
		for (size_t i = 0; i < out.size(); ++i)
		{
			if (out[i] == ' ') capNext = true;
			else if (capNext) { out[i] = toupper((unsigned char)out[i]); capNext = false; }
		}

		return out;
	}

	// Extract just the filename from a relative path.
	static std::string musicBaseName(const std::string& relPath)
	{
		const size_t slash = relPath.rfind('/');
		return (slash != std::string::npos) ? relPath.substr(slash + 1) : relPath;
	}

	// Extract the folder name from a relative path.
	static std::string musicFolderName(const std::string& relPath)
	{
		const size_t slash = relPath.find('/');
		return (slash != std::string::npos) ? relPath.substr(0, slash) : "";
	}

	// --- Music v2 Pass 2: Shuffle All Settings submenu ---
	void openShuffleAllSettings(Window* window)
	{
		// Show loading feedback immediately (filesystem scan can take 10+ seconds on RPi).
		window->renderLoadingScreen("Loading music...");

		auto s = new GuiSettings(window, "SHUFFLE ALL SETTINGS");

		auto tracks = SimpleArcadesMusicManager::getInstance().getShuffleAllowlist();

		if (tracks.empty())
		{
			s->addWithLabel("NO TRACKS FOUND", std::make_shared<TextComponent>(window,
				"Add MP3 files to soundtracks folder", saFont(FONT_SIZE_SMALL), SA_TEXT_COLOR));
			window->pushGui(s);
			return;
		}

		// Build a switch for each track, grouped by soundtrack folder.
		struct TrackSwitch {
			std::string relPath;
			std::shared_ptr<SwitchComponent> sw;
		};
		auto trackSwitches = std::make_shared<std::vector<TrackSwitch>>();

		std::string lastFolder;
		for (const auto& entry : tracks)
		{
			const std::string& relPath = entry.first;
			const bool enabled = entry.second;

			// Show folder separator when folder changes.
			const std::string folder = musicFolderName(relPath);
			if (folder != lastFolder && !folder.empty())
			{
				lastFolder = folder;
				ComponentListRow headerRow;
				headerRow.addElement(std::make_shared<TextComponent>(window,
					musicCleanName(folder), saFont(FONT_SIZE_MEDIUM), SA_SECTION_HEADER_COLOR), true);
				s->addRow(headerRow);
			}

			// Track toggle row.
			const std::string trackName = musicCleanName(musicBaseName(relPath));
			auto sw = std::make_shared<SwitchComponent>(window);
			sw->setState(enabled);

			s->addWithLabel("  " + trackName, sw);
			trackSwitches->push_back({relPath, sw});
		}

		// Save: write changes back to allowlist.
		s->addSaveFunc([trackSwitches]
		{
			for (const auto& ts : *trackSwitches)
			{
				SimpleArcadesMusicManager::getInstance().setTrackEnabled(ts.relPath, ts.sw->getState());
			}
			SimpleArcadesMusicManager::getInstance().saveShuffleAllowlist();
		});

		window->pushGui(s);
	}

	void openSimpleArcadesMusicSettings(Window* window)
	{
		SimpleArcadesMusicManager::getInstance().init();

		auto s = new GuiSettings(window, "MUSIC SETTINGS");

		// --- 1. Background Music on/off ---
		const auto musicEnabled = std::make_shared<SwitchComponent>(window);
		musicEnabled->setState(SimpleArcadesMusicManager::getInstance().isEnabled());
		s->addWithLabel("BACKGROUND MUSIC", musicEnabled);

		// --- 2. Music Volume slider ---
		auto musicVolume = std::make_shared<SliderComponent>(window, 0.f, 100.f, 1.f, "%");
		musicVolume->setValue((float)SimpleArcadesMusicManager::getInstance().getVolumePercent());
		s->addWithLabel("MUSIC VOLUME", musicVolume);

		// --- 3. Playlist Mode ---
		auto folders = SimpleArcadesMusicManager::getInstance().getAvailableFolders();

		auto mode = std::make_shared<OptionListComponent<std::string>>(window, "PLAYLIST MODE", false);
		mode->add("Shuffle All", "shuffle_all", SimpleArcadesMusicManager::getInstance().getMode() != "folder");
		mode->add("Single Soundtrack", "folder", SimpleArcadesMusicManager::getInstance().getMode() == "folder");
		s->addWithLabel("PLAYLIST MODE", mode);

		// --- 4. Soundtrack folder selector ---
		auto folderOpt = std::make_shared<OptionListComponent<std::string>>(window, "SOUNDTRACK", false);
		if (folders.empty())
		{
			folderOpt->add("No folders found", "", true);
		}
		else
		{
			const std::string curFolder = SimpleArcadesMusicManager::getInstance().getFolder();
			const bool hasCur = (std::find(folders.begin(), folders.end(), curFolder) != folders.end());
			bool selected = false;

			for (const auto& f : folders)
			{
				const bool isSel = hasCur ? (f == curFolder) : (!selected);
				folderOpt->add(f, f, isSel);
				if (isSel)
					selected = true;
			}
		}
		s->addWithLabel("SOUNDTRACK", folderOpt);

		// --- 5. Show Track Popup toggle ---
		const auto showPopup = std::make_shared<SwitchComponent>(window);
		showPopup->setState(SimpleArcadesMusicManager::getInstance().getShowTrackPopup());
		s->addWithLabel("SHOW TRACK POPUP", showPopup);

		// --- 6. Play During Screensaver toggle ---
		const auto playDuringSS = std::make_shared<SwitchComponent>(window);
		playDuringSS->setState(SimpleArcadesMusicManager::getInstance().getPlayDuringScreensaver());
		s->addWithLabel("PLAY DURING SCREENSAVER", playDuringSS);

		// --- 7. Shuffle All Settings submenu ---
		ComponentListRow shuffleRow;
		shuffleRow.makeAcceptInputHandler([window]
		{
			openShuffleAllSettings(window);
		});
		shuffleRow.addElement(std::make_shared<TextComponent>(window,
			"SHUFFLE ALL SETTINGS", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		s->addRow(shuffleRow);

		// --- 8. Rescan Music Now action ---
		ComponentListRow rescanRow;
		rescanRow.makeAcceptInputHandler([window]
		{
			SimpleArcadesMusicManager::getInstance().rescanMusic();
			window->setInfoPopup(new GuiInfoPopup(window, "Music rescanned!", 3000));
		});
		rescanRow.addElement(std::make_shared<TextComponent>(window,
			"RESCAN MUSIC NOW", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		s->addRow(rescanRow);

		// --- 9. Add Your Own Music (QR code viewer, hidden if no QR image) ---
		const std::string qrPath = Utils::FileSystem::getHomePath() + "/simplearcades/media/qrcodes/qr_music_help.png";
		if (Utils::FileSystem::exists(qrPath))
		{
			ComponentListRow addMusicRow;
			addMusicRow.makeAcceptInputHandler([window, qrPath]
			{
				window->pushGui(new GuiImageViewer(window, qrPath, "SCAN TO ADD YOUR OWN MUSIC"));
			});
			addMusicRow.addElement(std::make_shared<TextComponent>(window,
				"ADD YOUR OWN MUSIC", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
			s->addRow(addMusicRow);
		}

		// --- Save all settings on menu close ---
		s->addSaveFunc([musicEnabled, musicVolume, mode, folderOpt, playDuringSS, showPopup]
		{
			SimpleArcadesMusicManager::getInstance().init();

		SimpleArcadesMusicManager::getInstance().setVolumePercent((int)musicVolume->getValue());
			SimpleArcadesMusicManager::getInstance().setMode(mode->getSelected());
			SimpleArcadesMusicManager::getInstance().setFolder(folderOpt->getSelected());
			SimpleArcadesMusicManager::getInstance().setPlayDuringScreensaver(playDuringSS->getState());
			SimpleArcadesMusicManager::getInstance().setShowTrackPopup(showPopup->getState());
			SimpleArcadesMusicManager::getInstance().setEnabled(musicEnabled->getState());

			SimpleArcadesMusicManager::getInstance().saveConfig();
		});

		window->pushGui(s);
	}

// ============================================================================
//  Game Launch Video Settings
// ============================================================================

namespace GameLaunchVideoConfig
{
	struct Settings
	{
		bool enabled;
		int mode;    // 0 = RANDOM TIPS, 1 = STANDARD LOADING, 2 = CONTROL MAPPINGS
		bool mute;

		Settings() : enabled(true), mode(0), mute(false) {}
	};

	static Settings load()
	{
		Settings cfg;
		const std::string path = SA_LAUNCH_VIDEO_CONFIG;
		if (!Utils::FileSystem::exists(path))
			return cfg;

		std::ifstream file(path);
		if (!file.good())
			return cfg;

		std::string line;
		while (std::getline(file, line))
		{
			// Trim whitespace
			while (!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
				line.pop_back();

			auto eq = line.find('=');
			if (eq == std::string::npos)
				continue;

			std::string key = line.substr(0, eq);
			std::string val = line.substr(eq + 1);

			if (key == "enabled")
				cfg.enabled = (val == "1");
			else if (key == "mode")
				cfg.mode = atoi(val.c_str());
			else if (key == "mute")
				cfg.mute = (val == "1");
		}

		return cfg;
	}

	static void save(const Settings& cfg)
	{
		const std::string path = SA_LAUNCH_VIDEO_CONFIG;

		// Ensure parent directory tree exists (recursive)
		size_t lastSlash = path.rfind('/');
		if (lastSlash != std::string::npos)
		{
			std::string dir = path.substr(0, lastSlash);
			if (!Utils::FileSystem::exists(dir))
			{
				std::string cmd = "mkdir -p \"" + dir + "\"";
				system(cmd.c_str());
			}
		}

		std::ofstream file(path);
		if (!file.good())
		{
			LOG(LogError) << "Failed to write game launch video config: " << path;
			return;
		}

		file << "enabled=" << (cfg.enabled ? "1" : "0") << "\n";
		file << "mode=" << cfg.mode << "\n";
		file << "mute=" << (cfg.mute ? "1" : "0") << "\n";
	}
}

void openGameLaunchVideoSettings(Window* window)
{
	auto cfg = GameLaunchVideoConfig::load();

	auto s = new GuiSettings(window, "GAME LAUNCH VIDEO SETTINGS");

	// --- 1. Launch Videos ON/OFF ---
	auto launchEnabled = std::make_shared<SwitchComponent>(window);
	launchEnabled->setState(cfg.enabled);
	s->addWithLabel("LAUNCH VIDEOS", launchEnabled);

	// --- 2. Video Mode selector ---
	auto videoMode = std::make_shared<OptionListComponent<std::string>>(window, "VIDEO MODE", false);
	videoMode->add("RANDOM TIPS", "0", cfg.mode == 0);
	videoMode->add("STANDARD LOADING", "1", cfg.mode == 1);
	videoMode->add("CONTROL MAPPINGS", "2", cfg.mode == 2);
	s->addWithLabel("VIDEO MODE", videoMode);

	// --- 3. Mute Launch Sound ---
	auto muteLaunch = std::make_shared<SwitchComponent>(window);
	muteLaunch->setState(cfg.mute);
	s->addWithLabel("MUTE LAUNCH SOUND", muteLaunch);

	// --- Save ---
	s->addSaveFunc([launchEnabled, videoMode, muteLaunch]
	{
		GameLaunchVideoConfig::Settings newCfg;
		newCfg.enabled = launchEnabled->getState();
		newCfg.mode = atoi(videoMode->getSelected().c_str());
		newCfg.mute = muteLaunch->getState();
		GameLaunchVideoConfig::save(newCfg);
	});

	window->pushGui(s);
}

GuiMenu::GuiMenu(Window* window) : GuiComponent(window), mMenu(window, "SIMPLE ARCADES MAIN MENU"), mVersion(window)
{
	bool isFullUI = UIModeController::getInstance()->isUIModeFull();

	bool isKioskUI = UIModeController::getInstance()->isUIModeKiosk();
	if (isFullUI) {
		addEntry("SCRAPER", SA_TEXT_COLOR, true, [this] { openScraperSettings(); });
		addEntry("SOUND SETTINGS", SA_TEXT_COLOR, true, [this] { openSoundSettings(); });
		addEntry("MUSIC SETTINGS", SA_TEXT_COLOR, true, [this] { openSimpleArcadesMusicSettings(mWindow); });
		addEntry("GAME LAUNCH VIDEO SETTINGS", SA_TEXT_COLOR, true, [this] { openGameLaunchVideoSettings(mWindow); });
		addEntry("UI SETTINGS", SA_TEXT_COLOR, true, [this] { openUISettings(); });
		addEntry("GAME COLLECTION SETTINGS", SA_TEXT_COLOR, true, [this] { openCollectionSystemSettings(); });
		addEntry("OTHER SETTINGS", SA_TEXT_COLOR, true, [this] { openOtherSettings(); });
	} else {
		addEntry("SOUND SETTINGS", SA_TEXT_COLOR, true, [this] { openSoundSettings(); });
		addEntry("MUSIC SETTINGS", SA_TEXT_COLOR, true, [this] { openSimpleArcadesMusicSettings(mWindow); });
		addEntry("GAME LAUNCH VIDEO SETTINGS", SA_TEXT_COLOR, true, [this] { openGameLaunchVideoSettings(mWindow); });
	}
	
	if (isFullUI || isKioskUI) {
		addEntry("CONFIGURE INPUT", SA_TEXT_COLOR, true, [this] { openConfigInput(); });
		if (isKioskUI) {
			addEntry("SCREENSAVER SETTINGS", SA_TEXT_COLOR, true, [this] { openScreensaverOptions(); });
		}
	}

addEntry("QUIT", SA_TEXT_COLOR, true, [this] {openQuitMenu(); });

	addChild(&mMenu);
	addVersionInfo();
	setSize(mMenu.getSize());
	setPosition((Renderer::getScreenWidth() - mSize.x()) / 2, Renderer::getScreenHeight() * 0.15f);
}

void GuiMenu::openScraperSettings()
{
	auto s = new GuiSettings(mWindow, "SCRAPER");

	// scrape from
	auto scraper_list = std::make_shared< OptionListComponent< std::string > >(mWindow, "SCRAPE FROM", false);
	std::vector<std::string> scrapers = getScraperList();

	// Select either the first entry of the one read from the settings, just in case the scraper from settings has vanished.
	for(auto it = scrapers.cbegin(); it != scrapers.cend(); it++)
		scraper_list->add(*it, *it, *it == Settings::getInstance()->getString("Scraper"));

	s->addWithLabel("SCRAPE FROM", scraper_list);
	s->addSaveFunc([scraper_list] { Settings::getInstance()->setString("Scraper", scraper_list->getSelected()); });

	// scrape ratings
	auto scrape_ratings = std::make_shared<SwitchComponent>(mWindow);
	scrape_ratings->setState(Settings::getInstance()->getBool("ScrapeRatings"));
	s->addWithLabel("SCRAPE RATINGS", scrape_ratings);
	s->addSaveFunc([scrape_ratings] { Settings::getInstance()->setBool("ScrapeRatings", scrape_ratings->getState()); });

	// scrape now
	ComponentListRow row;
	auto openScrapeNow = [this] { mWindow->pushGui(new GuiScraperStart(mWindow)); };
	std::function<void()> openAndSave = openScrapeNow;
	openAndSave = [s, openAndSave] { s->save(); openAndSave(); };
	row.makeAcceptInputHandler(openAndSave);

	auto scrape_now = std::make_shared<TextComponent>(mWindow, "SCRAPE NOW", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR);
	auto bracket = makeArrow(mWindow);
	row.addElement(scrape_now, true);
	row.addElement(bracket, false);
	s->addRow(row);

	mWindow->pushGui(s);
}

void GuiMenu::openSoundSettings()
{
	auto s = new GuiSettings(mWindow, "SOUND SETTINGS");

	// volume
	auto volume = std::make_shared<SliderComponent>(mWindow, 0.f, 100.f, 1.f, "%");
	volume->setValue((float)VolumeControl::getInstance()->getVolume());
	s->addWithLabel("SYSTEM VOLUME", volume);
	s->addSaveFunc([volume] { VolumeControl::getInstance()->setVolume((int)Math::round(volume->getValue())); });

	if (UIModeController::getInstance()->isUIModeFull())
	{
#if defined(__linux__)
		// audio card
		auto audio_card = std::make_shared< OptionListComponent<std::string> >(mWindow, "AUDIO CARD", false);
		std::vector<std::string> audio_cards;
		audio_cards.push_back("default");
		audio_cards.push_back("sysdefault");
		audio_cards.push_back("dmix");
		audio_cards.push_back("hw");
		audio_cards.push_back("plughw");
		audio_cards.push_back("null");
		if (Settings::getInstance()->getString("AudioCard") != "") {
			if(std::find(audio_cards.begin(), audio_cards.end(), Settings::getInstance()->getString("AudioCard")) == audio_cards.end()) {
				audio_cards.push_back(Settings::getInstance()->getString("AudioCard"));
			}
		}
		for(auto ac = audio_cards.cbegin(); ac != audio_cards.cend(); ac++)
			audio_card->add(*ac, *ac, Settings::getInstance()->getString("AudioCard") == *ac);
		s->addWithLabel("AUDIO CARD", audio_card);
		s->addSaveFunc([audio_card] {
			Settings::getInstance()->setString("AudioCard", audio_card->getSelected());
			VolumeControl::getInstance()->deinit();
			VolumeControl::getInstance()->init();
		});

		// volume control device
		auto vol_dev = std::make_shared< OptionListComponent<std::string> >(mWindow, "AUDIO DEVICE", false);
		std::vector<std::string> transitions;
		transitions.push_back("PCM");
		transitions.push_back("HDMI");
		transitions.push_back("Headphone");
		transitions.push_back("Speaker");
		transitions.push_back("Master");
		transitions.push_back("Digital");
		transitions.push_back("Analogue");
		if (Settings::getInstance()->getString("AudioDevice") != "") {
			if(std::find(transitions.begin(), transitions.end(), Settings::getInstance()->getString("AudioDevice")) == transitions.end()) {
				transitions.push_back(Settings::getInstance()->getString("AudioDevice"));
			}
		}
		for(auto it = transitions.cbegin(); it != transitions.cend(); it++)
			vol_dev->add(*it, *it, Settings::getInstance()->getString("AudioDevice") == *it);
		s->addWithLabel("AUDIO DEVICE", vol_dev);
		s->addSaveFunc([vol_dev] {
			Settings::getInstance()->setString("AudioDevice", vol_dev->getSelected());
			VolumeControl::getInstance()->deinit();
			VolumeControl::getInstance()->init();
		});
#endif

		// disable sounds
		auto sounds_enabled = std::make_shared<SwitchComponent>(mWindow);
		sounds_enabled->setState(Settings::getInstance()->getBool("EnableSounds"));
		s->addWithLabel("ENABLE NAVIGATION SOUNDS", sounds_enabled);
		s->addSaveFunc([sounds_enabled] {
			if (sounds_enabled->getState()
				&& !Settings::getInstance()->getBool("EnableSounds")
				&& PowerSaver::getMode() == PowerSaver::INSTANT)
			{
				Settings::getInstance()->setString("PowerSaverMode", "default");
				PowerSaver::init();
			}
			Settings::getInstance()->setBool("EnableSounds", sounds_enabled->getState());
		});

		auto video_audio = std::make_shared<SwitchComponent>(mWindow);
		video_audio->setState(Settings::getInstance()->getBool("VideoAudio"));
		s->addWithLabel("ENABLE VIDEO AUDIO", video_audio);
		s->addSaveFunc([video_audio] { Settings::getInstance()->setBool("VideoAudio", video_audio->getState()); });

#ifdef _OMX_
		// OMX player Audio Device
		auto omx_audio_dev = std::make_shared< OptionListComponent<std::string> >(mWindow, "OMX PLAYER AUDIO DEVICE", false);
		std::vector<std::string> omx_cards;
		// RPi Specific  Audio Cards
		omx_cards.push_back("local");
		omx_cards.push_back("hdmi");
		omx_cards.push_back("both");
		omx_cards.push_back("alsa");
		omx_cards.push_back("alsa:hw:0,0");
		omx_cards.push_back("alsa:hw:1,0");
		if (Settings::getInstance()->getString("OMXAudioDev") != "") {
			if (std::find(omx_cards.begin(), omx_cards.end(), Settings::getInstance()->getString("OMXAudioDev")) == omx_cards.end()) {
				omx_cards.push_back(Settings::getInstance()->getString("OMXAudioDev"));
			}
		}
		for (auto it = omx_cards.cbegin(); it != omx_cards.cend(); it++)
			omx_audio_dev->add(*it, *it, Settings::getInstance()->getString("OMXAudioDev") == *it);
		s->addWithLabel("OMX PLAYER AUDIO DEVICE", omx_audio_dev);
		s->addSaveFunc([omx_audio_dev] {
			if (Settings::getInstance()->getString("OMXAudioDev") != omx_audio_dev->getSelected())
				Settings::getInstance()->setString("OMXAudioDev", omx_audio_dev->getSelected());
		});
#endif
	}

	mWindow->pushGui(s);

}

void GuiMenu::openUISettings()
{
	auto s = new GuiSettings(mWindow, "UI SETTINGS");

	//UI mode
	auto UImodeSelection = std::make_shared< OptionListComponent<std::string> >(mWindow, "UI MODE", false);
	std::vector<std::string> UImodes = UIModeController::getInstance()->getUIModes();
	for (auto it = UImodes.cbegin(); it != UImodes.cend(); it++)
		UImodeSelection->add(*it, *it, Settings::getInstance()->getString("UIMode") == *it);
	s->addWithLabel("UI MODE", UImodeSelection);
	Window* window = mWindow;
	s->addSaveFunc([ UImodeSelection, window]
	{
		std::string selectedMode = UImodeSelection->getSelected();
		if (selectedMode != "Full")
		{
			std::string msg = "You are changing the UI to a restricted mode:\n" + selectedMode + "\n";
			msg += "This will hide most menu-options to prevent changes to the system.\n";
			msg += "To unlock and return to the full UI, enter this code: \n";
			msg += "\"" + UIModeController::getInstance()->getFormattedPassKeyStr() + "\"\n\n";
			msg += "Do you want to proceed?";
			window->pushGui(new GuiMsgBox(window, msg,
				"YES", [selectedMode] {
					LOG(LogDebug) << "Setting UI mode to " << selectedMode;
					Settings::getInstance()->setString("UIMode", selectedMode);
					Settings::getInstance()->saveFile();
			}, "NO",nullptr));
		}
	});

	// screensaver
	ComponentListRow screensaver_row;
	screensaver_row.elements.clear();
	screensaver_row.addElement(std::make_shared<TextComponent>(mWindow, "SCREENSAVER SETTINGS", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	screensaver_row.addElement(makeArrow(mWindow), false);
	screensaver_row.makeAcceptInputHandler(std::bind(&GuiMenu::openScreensaverOptions, this));
	s->addRow(screensaver_row);

	// quick system select (left/right in game list view)
	auto quick_sys_select = std::make_shared<SwitchComponent>(mWindow);
	quick_sys_select->setState(Settings::getInstance()->getBool("QuickSystemSelect"));
	s->addWithLabel("QUICK SYSTEM SELECT", quick_sys_select);
	s->addSaveFunc([quick_sys_select] { Settings::getInstance()->setBool("QuickSystemSelect", quick_sys_select->getState()); });

	// carousel transition option
	auto move_carousel = std::make_shared<SwitchComponent>(mWindow);
	move_carousel->setState(Settings::getInstance()->getBool("MoveCarousel"));
	s->addWithLabel("CAROUSEL TRANSITIONS", move_carousel);
	s->addSaveFunc([move_carousel] {
		if (move_carousel->getState()
			&& !Settings::getInstance()->getBool("MoveCarousel")
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setBool("MoveCarousel", move_carousel->getState());
	});

	// transition style
	auto transition_style = std::make_shared< OptionListComponent<std::string> >(mWindow, "TRANSITION STYLE", false);
	std::vector<std::string> transitions;
	transitions.push_back("fade");
	transitions.push_back("slide");
	transitions.push_back("instant");
	for(auto it = transitions.cbegin(); it != transitions.cend(); it++)
		transition_style->add(*it, *it, Settings::getInstance()->getString("TransitionStyle") == *it);
	s->addWithLabel("TRANSITION STYLE", transition_style);
	s->addSaveFunc([transition_style] {
		if (Settings::getInstance()->getString("TransitionStyle") == "instant"
			&& transition_style->getSelected() != "instant"
			&& PowerSaver::getMode() == PowerSaver::INSTANT)
		{
			Settings::getInstance()->setString("PowerSaverMode", "default");
			PowerSaver::init();
		}
		Settings::getInstance()->setString("TransitionStyle", transition_style->getSelected());
	});

	// theme set
	auto themeSets = ThemeData::getThemeSets();

	if(!themeSets.empty())
	{
		std::map<std::string, ThemeSet>::const_iterator selectedSet = themeSets.find(Settings::getInstance()->getString("ThemeSet"));
		if(selectedSet == themeSets.cend())
			selectedSet = themeSets.cbegin();

		auto theme_set = std::make_shared< OptionListComponent<std::string> >(mWindow, "THEME SET", false);
		for(auto it = themeSets.cbegin(); it != themeSets.cend(); it++)
			theme_set->add(it->first, it->first, it == selectedSet);
		s->addWithLabel("THEME SET", theme_set);

		Window* window = mWindow;
		s->addSaveFunc([window, theme_set]
		{
			bool needReload = false;
			std::string oldTheme = Settings::getInstance()->getString("ThemeSet");
			if(oldTheme != theme_set->getSelected())
				needReload = true;

			Settings::getInstance()->setString("ThemeSet", theme_set->getSelected());

			if(needReload)
			{
				Scripting::fireEvent("theme-changed", theme_set->getSelected(), oldTheme);
				CollectionSystemManager::get()->updateSystemsList();
				ViewController::get()->reloadAll(true); // TODO - replace this with some sort of signal-based implementation
			}
		});
	}

	// GameList view style
	auto gamelist_style = std::make_shared< OptionListComponent<std::string> >(mWindow, "GAMELIST VIEW STYLE", false);
	std::vector<std::string> styles;
	styles.push_back("automatic");
	styles.push_back("basic");
	styles.push_back("detailed");
	styles.push_back("video");
	styles.push_back("grid");

	for (auto it = styles.cbegin(); it != styles.cend(); it++)
		gamelist_style->add(*it, *it, Settings::getInstance()->getString("GamelistViewStyle") == *it);
	s->addWithLabel("GAMELIST VIEW STYLE", gamelist_style);
	s->addSaveFunc([gamelist_style] {
		bool needReload = false;
		if (Settings::getInstance()->getString("GamelistViewStyle") != gamelist_style->getSelected())
			needReload = true;
		Settings::getInstance()->setString("GamelistViewStyle", gamelist_style->getSelected());
		if (needReload)
			ViewController::get()->reloadAll();
	});

	// Optionally ignore leading articles when sorting game titles
	auto ignore_articles = std::make_shared<SwitchComponent>(mWindow);
	ignore_articles->setState(Settings::getInstance()->getBool("IgnoreLeadingArticles"));
	s->addWithLabel("IGNORE ARTICLES (NAME SORT ONLY)", ignore_articles);
	s->addSaveFunc([ignore_articles, window] {
		bool articles_are_ignored = Settings::getInstance()->getBool("IgnoreLeadingArticles");
		Settings::getInstance()->setBool("IgnoreLeadingArticles", ignore_articles->getState());
		if (ignore_articles->getState() != articles_are_ignored)
		{
			//For each system...
			for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
			{
				//Apply sort recursively
				FileData* root = (*it)->getRootFolder();
				root->sort(getSortTypeFromString(root->getSortName()));

				//Notify that the root folder was sorted
				ViewController::get()->getGameListView((*it))->onFileChanged(root, FILE_SORTED);
			}

			//Display popup to inform user
			GuiInfoPopup* popup = new GuiInfoPopup(window, "Files sorted", 4000);
			window->setInfoPopup(popup);
		}
	});

	// lb/rb uses full screen size paging instead of -10/+10 steps
	auto use_fullscreen_paging = std::make_shared<SwitchComponent>(mWindow);
	use_fullscreen_paging->setState(Settings::getInstance()->getBool("UseFullscreenPaging"));
	s->addWithLabel("USE FULL SCREEN PAGING FOR LB/RB", use_fullscreen_paging);
	s->addSaveFunc([use_fullscreen_paging] {
		Settings::getInstance()->setBool("UseFullscreenPaging", use_fullscreen_paging->getState());
	});

	// Optionally start in selected system
	auto systemfocus_list = std::make_shared< OptionListComponent<std::string> >(mWindow, "START ON SYSTEM", false);
	systemfocus_list->add("NONE", "", Settings::getInstance()->getString("StartupSystem") == "");
	for (auto it = SystemData::sSystemVector.cbegin(); it != SystemData::sSystemVector.cend(); it++)
	{
		if ("retropie" != (*it)->getName())
		{
			systemfocus_list->add((*it)->getName(), (*it)->getName(), Settings::getInstance()->getString("StartupSystem") == (*it)->getName());
		}
	}
	s->addWithLabel("START ON SYSTEM", systemfocus_list);
	s->addSaveFunc([systemfocus_list] {
		Settings::getInstance()->setString("StartupSystem", systemfocus_list->getSelected());
	});

	// show help
	auto show_help = std::make_shared<SwitchComponent>(mWindow);
	show_help->setState(Settings::getInstance()->getBool("ShowHelpPrompts"));
	s->addWithLabel("ON-SCREEN HELP", show_help);
	s->addSaveFunc([show_help] { Settings::getInstance()->setBool("ShowHelpPrompts", show_help->getState()); });

	// enable filters (ForceDisableFilters)
	auto enable_filter = std::make_shared<SwitchComponent>(mWindow);
	enable_filter->setState(!Settings::getInstance()->getBool("ForceDisableFilters"));
	s->addWithLabel("ENABLE FILTERS", enable_filter);
	s->addSaveFunc([enable_filter] {
		bool filter_is_enabled = !Settings::getInstance()->getBool("ForceDisableFilters");
		Settings::getInstance()->setBool("ForceDisableFilters", !enable_filter->getState());
		if (enable_filter->getState() != filter_is_enabled) ViewController::get()->ReloadAndGoToStart();
	});

	// hide start menu in Kid Mode
	auto disable_start = std::make_shared<SwitchComponent>(mWindow);
	disable_start->setState(Settings::getInstance()->getBool("DisableKidStartMenu"));
	s->addWithLabel("DISABLE START MENU IN KID MODE", disable_start);
	s->addSaveFunc([disable_start] { Settings::getInstance()->setBool("DisableKidStartMenu", disable_start->getState()); });

	mWindow->pushGui(s);

}

void GuiMenu::openOtherSettings()
{
	auto s = new GuiSettings(mWindow, "OTHER SETTINGS");

	// maximum vram
	auto max_vram = std::make_shared<SliderComponent>(mWindow, 0.f, 1000.f, 10.f, "Mb");
	max_vram->setValue((float)(Settings::getInstance()->getInt("MaxVRAM")));
	s->addWithLabel("VRAM LIMIT", max_vram);
	s->addSaveFunc([max_vram] { Settings::getInstance()->setInt("MaxVRAM", (int)Math::round(max_vram->getValue())); });

	// power saver
	auto power_saver = std::make_shared< OptionListComponent<std::string> >(mWindow, "POWER SAVER MODES", false);
	std::vector<std::string> modes;
	modes.push_back("disabled");
	modes.push_back("default");
	modes.push_back("enhanced");
	modes.push_back("instant");
	for (auto it = modes.cbegin(); it != modes.cend(); it++)
		power_saver->add(*it, *it, Settings::getInstance()->getString("PowerSaverMode") == *it);
	s->addWithLabel("POWER SAVER MODES", power_saver);
	s->addSaveFunc([this, power_saver] {
		if (Settings::getInstance()->getString("PowerSaverMode") != "instant" && power_saver->getSelected() == "instant") {
			Settings::getInstance()->setString("TransitionStyle", "instant");
			Settings::getInstance()->setBool("MoveCarousel", false);
			Settings::getInstance()->setBool("EnableSounds", false);
		}
		Settings::getInstance()->setString("PowerSaverMode", power_saver->getSelected());
		PowerSaver::init();
	});

	// gamelists
	auto gamelistsSaveMode = std::make_shared< OptionListComponent<std::string> >(mWindow, "SAVE METADATA", false);
	std::vector<std::string> saveModes;
	saveModes.push_back("on exit");
	saveModes.push_back("always");
	saveModes.push_back("never");

	for(auto it = saveModes.cbegin(); it != saveModes.cend(); it++)
		gamelistsSaveMode->add(*it, *it, Settings::getInstance()->getString("SaveGamelistsMode") == *it);
	s->addWithLabel("SAVE METADATA", gamelistsSaveMode);
	s->addSaveFunc([gamelistsSaveMode] {
		Settings::getInstance()->setString("SaveGamelistsMode", gamelistsSaveMode->getSelected());
	});

	auto parse_gamelists = std::make_shared<SwitchComponent>(mWindow);
	parse_gamelists->setState(Settings::getInstance()->getBool("ParseGamelistOnly"));
	s->addWithLabel("PARSE GAMESLISTS ONLY", parse_gamelists);
	s->addSaveFunc([parse_gamelists] { Settings::getInstance()->setBool("ParseGamelistOnly", parse_gamelists->getState()); });

	auto local_art = std::make_shared<SwitchComponent>(mWindow);
	local_art->setState(Settings::getInstance()->getBool("LocalArt"));
	s->addWithLabel("SEARCH FOR LOCAL ART", local_art);
	s->addSaveFunc([local_art] { Settings::getInstance()->setBool("LocalArt", local_art->getState()); });

	// hidden files
	auto hidden_files = std::make_shared<SwitchComponent>(mWindow);
	hidden_files->setState(Settings::getInstance()->getBool("ShowHiddenFiles"));
	s->addWithLabel("SHOW HIDDEN FILES", hidden_files);
	s->addSaveFunc([hidden_files] { Settings::getInstance()->setBool("ShowHiddenFiles", hidden_files->getState()); });

#ifdef _OMX_
	// Video Player - VideoOmxPlayer
	auto omx_player = std::make_shared<SwitchComponent>(mWindow);
	omx_player->setState(Settings::getInstance()->getBool("VideoOmxPlayer"));
	s->addWithLabel("USE OMX PLAYER (HW ACCELERATED)", omx_player);
	s->addSaveFunc([omx_player]
	{
		// need to reload all views to re-create the right video components
		bool needReload = false;
		if(Settings::getInstance()->getBool("VideoOmxPlayer") != omx_player->getState())
			needReload = true;

		Settings::getInstance()->setBool("VideoOmxPlayer", omx_player->getState());

		if(needReload)
			ViewController::get()->reloadAll();
	});

#endif

	// hidden files
	auto background_indexing = std::make_shared<SwitchComponent>(mWindow);
	background_indexing->setState(Settings::getInstance()->getBool("BackgroundIndexing"));
	s->addWithLabel("INDEX FILES DURING SCREENSAVER", background_indexing);
	s->addSaveFunc([background_indexing] { Settings::getInstance()->setBool("BackgroundIndexing", background_indexing->getState()); });

	// framerate
	auto framerate = std::make_shared<SwitchComponent>(mWindow);
	framerate->setState(Settings::getInstance()->getBool("DrawFramerate"));
	s->addWithLabel("SHOW FRAMERATE", framerate);
	s->addSaveFunc([framerate] { Settings::getInstance()->setBool("DrawFramerate", framerate->getState()); });


	mWindow->pushGui(s);

}

void GuiMenu::openConfigInput()
{
    auto s = new GuiSettings(mWindow, "CONTROLLERS");
    Window* window = mWindow;

    // Helper: launch the existing external-controller detect/config flow
    auto launchDetect = [window]() {
        window->pushGui(new GuiDetectDevice(window, false, nullptr));
    };

    // 1) Configure /remap controller
    {
        ComponentListRow row;
        row.makeAcceptInputHandler([window, launchDetect] {
            window->pushGui(new GuiMsgBox(
                window,
                "ADD OR REMAP AN EXTERNAL CONTROLLER?",
                "YES", launchDetect,
                "NO", nullptr
            ));
        });
        row.addElement(std::make_shared<TextComponent>(
            window, "ADD / REMAP CONTROLLER", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR
        ), true);
        s->addRow(row);
    }

	// 2) Delete controller profile
	{
		ComponentListRow row;
		row.makeAcceptInputHandler([this] {
			openDeleteControllerProfile();
		});
		row.addElement(std::make_shared<TextComponent>(
			mWindow, "DELETE CONTROLLER PROFILE", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR
		), true);
		s->addRow(row);
	}

    mWindow->pushGui(s);
}

void GuiMenu::openQuitMenu()
{
	auto s = new GuiSettings(mWindow, "QUIT");

	Window* window = mWindow;

	// command line switch
	bool confirm_quit = Settings::getInstance()->getBool("ConfirmQuit");

	ComponentListRow row;
	if (UIModeController::getInstance()->isUIModeFull())
	{
		auto static restart_es_fx = []() {
			Scripting::fireEvent("quit");
			if (quitES(QuitMode::RESTART)) {
				LOG(LogWarning) << "Restart terminated with non-zero result!";
			}
		};

		if (confirm_quit) {
			row.makeAcceptInputHandler([window] {
				window->pushGui(new GuiMsgBox(window, "REALLY RESTART?", "YES", restart_es_fx, "NO", nullptr));
			});
		} else {
			row.makeAcceptInputHandler(restart_es_fx);
		}
		row.addElement(std::make_shared<TextComponent>(window, "RESTART EMULATIONSTATION", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
		s->addRow(row);

		if(Settings::getInstance()->getBool("ShowExit"))
		{
			auto static quit_es_fx = [] {
				Scripting::fireEvent("quit");
				quitES();
			};

			row.elements.clear();
			if (confirm_quit) {
				row.makeAcceptInputHandler([window] {
					window->pushGui(new GuiMsgBox(window, "REALLY QUIT?", "YES", quit_es_fx, "NO", nullptr));
				});
			} else {
				row.makeAcceptInputHandler(quit_es_fx);
			}
			row.addElement(std::make_shared<TextComponent>(window, "QUIT EMULATIONSTATION", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
			s->addRow(row);
		}
	}

	auto static reboot_sys_fx = [] {
		Scripting::fireEvent("quit", "reboot");
		Scripting::fireEvent("reboot");
		if (quitES(QuitMode::REBOOT)) {
			LOG(LogWarning) << "Restart terminated with non-zero result!";
		}
	};

	row.elements.clear();
	if (confirm_quit) {
		row.makeAcceptInputHandler([window] {
			window->pushGui(new GuiMsgBox(window, "REALLY RESTART?", "YES", {reboot_sys_fx}, "NO", nullptr));
		});
	} else {
		row.makeAcceptInputHandler(reboot_sys_fx);
	}
	row.addElement(std::make_shared<TextComponent>(window, "RESTART SYSTEM", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	s->addRow(row);

	auto static shutdown_sys_fx = [] {
		Scripting::fireEvent("quit", "shutdown");
		Scripting::fireEvent("shutdown");
		if (quitES(QuitMode::SHUTDOWN)) {
			LOG(LogWarning) << "Shutdown terminated with non-zero result!";
		}
	};

	row.elements.clear();
	if (confirm_quit) {
		row.makeAcceptInputHandler([window] {
			window->pushGui(new GuiMsgBox(window, "REALLY SHUTDOWN?", "YES", shutdown_sys_fx, "NO", nullptr));
		});
	} else {
		row.makeAcceptInputHandler(shutdown_sys_fx);
	}
	row.addElement(std::make_shared<TextComponent>(window, "SHUTDOWN SYSTEM", saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);
	s->addRow(row);
	mWindow->pushGui(s);
}

void GuiMenu::addVersionInfo()
{
	std::string  buildDate = (Settings::getInstance()->getBool("Debug") ? std::string( "   (" + Utils::String::toUpper(PROGRAM_BUILT_STRING) + ")") : (""));

	mVersion.setFont(saFont(FONT_SIZE_SMALL));
	mVersion.setColor(SA_VERSION_COLOR);
	// old versoin string mVersion.setText("EMULATIONSTATION V" + Utils::String::toUpper(PROGRAM_VERSION_STRING) + buildDate);
	mVersion.setText("EMULATIONSTATION V12.0.1.SIMPLE.ARCADES");
	mVersion.setHorizontalAlignment(ALIGN_CENTER);
	addChild(&mVersion);
}

void GuiMenu::openScreensaverOptions() {
	mWindow->pushGui(new GuiGeneralScreensaverOptions(mWindow, "SCREENSAVER SETTINGS"));
}

void GuiMenu::openCollectionSystemSettings() {
	mWindow->pushGui(new GuiCollectionSystemsOptions(mWindow));
}

void GuiMenu::onSizeChanged()
{
	mVersion.setSize(mSize.x(), 0);
	mVersion.setPosition(0, mSize.y() - mVersion.getSize().y());
}

void GuiMenu::addEntry(const char* name, unsigned int color, bool add_arrow, const std::function<void()>& func)
{
	std::shared_ptr<Font> font = saFont(FONT_SIZE_MEDIUM);

	// populate the list
	ComponentListRow row;
	row.addElement(std::make_shared<TextComponent>(mWindow, name, font, color), true);

	if(add_arrow)
	{
		std::shared_ptr<ImageComponent> bracket = makeArrow(mWindow);
		row.addElement(bracket, false);
	}

	row.makeAcceptInputHandler(func);

	mMenu.addRow(row);
}

bool GuiMenu::input(InputConfig* config, Input input)
{
	if(GuiComponent::input(config, input))
		return true;

	if((config->isMappedTo("b", input) || config->isMappedTo("start", input)) && input.value != 0)
	{
		delete this;
		return true;
	}

	return false;
}

HelpStyle GuiMenu::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	style.applyTheme(ViewController::get()->getState().getSystem()->getTheme(), "system");
	return style;
}

std::vector<HelpPrompt> GuiMenu::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	prompts.push_back(HelpPrompt("up/down", "choose"));
	prompts.push_back(HelpPrompt("a", "select"));
	prompts.push_back(HelpPrompt("start", "close"));
	return prompts;
}
