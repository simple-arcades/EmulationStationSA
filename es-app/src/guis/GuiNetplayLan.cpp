#include "guis/GuiNetplayLan.h"
#include "guis/GuiMsgBox.h"
#include "guis/GuiTextInput.h"
#include "components/TextComponent.h"
#include "NetplayConfig.h"
#include "NetplayCore.h"
#include "NetplayLauncher.h"
#include "SystemData.h"
#include "FileData.h"
#include "Window.h"
#include "Log.h"
#include "SAStyle.h"
#include "renderers/Renderer.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

// ============================================================================
//  LAN Discovery constants — kept as functions to avoid static init issues
// ============================================================================

static int getLanDiscoveryPort() { return 55439; }
static int getLanListenSec() { return 4; }
static std::string getLanOutputPath() { return "/dev/shm/netplay_lan_parsed.tsv"; }

// The Python listener script is written to disk at call time,
// not stored as a static global (avoids ARM static init crashes)
static void writeLanListenerScript(const std::string& path)
{
	std::ofstream f(path, std::ios::trunc);
	f << "import sys, json, time, socket\n"
	     "\n"
	     "port = int(sys.argv[1])\n"
	     "timeout = float(sys.argv[2])\n"
	     "outpath = sys.argv[3]\n"
	     "\n"
	     "sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)\n"
	     "sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)\n"
	     "try:\n"
	     "    sock.bind((\"\", port))\n"
	     "except Exception:\n"
	     "    sys.exit(1)\n"
	     "\n"
	     "sock.settimeout(0.5)\n"
	     "\n"
	     "sessions = {}\n"
	     "end = time.time() + timeout\n"
	     "\n"
	     "while time.time() < end:\n"
	     "    try:\n"
	     "        data, addr = sock.recvfrom(4096)\n"
	     "    except socket.timeout:\n"
	     "        continue\n"
	     "    except Exception:\n"
	     "        break\n"
	     "\n"
	     "    ip = addr[0]\n"
	     "    try:\n"
	     "        msg = json.loads(data.decode(\"utf-8\", errors=\"ignore\"))\n"
	     "    except Exception:\n"
	     "        continue\n"
	     "\n"
	     "    if not isinstance(msg, dict):\n"
	     "        continue\n"
	     "    if msg.get(\"sa\") != \"netplay\":\n"
	     "        continue\n"
	     "\n"
	     "    nick = str(msg.get(\"nick\", \"Host\")).strip()[:32]\n"
	     "    system = str(msg.get(\"system\", \"\")).strip()[:32]\n"
	     "    game = str(msg.get(\"game\", \"\")).strip()[:120]\n"
	     "    rom = str(msg.get(\"rom\", \"\")).strip()[:200]\n"
	     "    core = str(msg.get(\"core\", \"\")).strip()[:200]\n"
	     "    nport = msg.get(\"port\", None)\n"
	     "    try:\n"
	     "        nport = int(nport)\n"
	     "    except Exception:\n"
	     "        continue\n"
	     "\n"
	     "    key = (ip, nport, rom, system)\n"
	     "    sessions[key] = {\n"
	     "        \"ip\": ip, \"port\": nport, \"nick\": nick, \"system\": system,\n"
	     "        \"game\": game, \"rom\": rom, \"core\": core, \"last\": time.time()\n"
	     "    }\n"
	     "\n"
	     "sock.close()\n"
	     "\n"
	     "out = open(outpath, \"w\")\n"
	     "for key, s in sorted(sessions.items(), key=lambda kv: kv[1][\"last\"], reverse=True):\n"
	     "    out.write(\"%s\\t%s\\t%s\\t%s\\t%s\\t%s\\t%s\\n\" % (\n"
	     "        s[\"ip\"], s[\"port\"], s[\"nick\"], s[\"system\"], s[\"game\"], s[\"rom\"], s[\"core\"]\n"
	     "    ))\n"
	     "out.close()\n";
}

// ============================================================================
//  Constructor
// ============================================================================

GuiNetplayLan::GuiNetplayLan(Window* window)
	: GuiComponent(window),
	  mMenu(window, "LAN GAMES")
{
	addChild(&mMenu);
	discoverAndBuild();
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2,
	                   (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  discoverAndBuild
// ============================================================================

void GuiNetplayLan::discoverAndBuild()
{
	// Write the listener script
	std::string scriptPath = "/dev/shm/netplay_lan_listen.py";
	writeLanListenerScript(scriptPath);

	std::string outputPath = getLanOutputPath();
	int listenSec = getLanListenSec();

	// Run it (blocks for listenSec seconds)
	std::string cmd = "python3 \"" + scriptPath + "\" "
	                  + std::to_string(getLanDiscoveryPort()) + " "
	                  + std::to_string(listenSec) + " \""
	                  + outputPath + "\" 2>/dev/null";
	
	mWindow->renderLoadingScreen("SEARCHING FOR LAN GAMES...");
	LOG(LogInfo) << "NetplayLan: Listening for " << listenSec << " seconds...";
	int rc = ::system(cmd.c_str());

	::unlink(scriptPath.c_str());

	std::string rawTsv;

	if (rc == 0 && Utils::FileSystem::exists(outputPath))
	{
		std::ifstream f(outputPath);
		rawTsv = std::string((std::istreambuf_iterator<char>(f)),
		                      std::istreambuf_iterator<char>());
		::unlink(outputPath.c_str());
	}

	if (rawTsv.empty())
	{
		// Show "no games found" as a menu row — don't pushGui from constructor
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			"NO GAMES FOUND ON YOUR NETWORK.",
			saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row);

		ComponentListRow row2;
		row2.addElement(std::make_shared<TextComponent>(mWindow,
			"MAKE SURE THE OTHER ARCADE IS HOSTING",
			saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row2);

		ComponentListRow row3;
		row3.addElement(std::make_shared<TextComponent>(mWindow,
			"IN LAN MODE ON THE SAME NETWORK.",
			saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row3);
		return;
	}

	buildSessionList(rawTsv);
}

// ============================================================================
//  buildSessionList
// ============================================================================

void GuiNetplayLan::buildSessionList(const std::string& rawTsv)
{
	std::istringstream stream(rawTsv);
	std::string line;

	while (std::getline(stream, line))
	{
		if (line.empty()) continue;

		// Parse TSV: ip \t port \t nick \t system \t game \t rom \t core
		std::vector<std::string> fields;
		std::istringstream lstream(line);
		std::string field;
		while (std::getline(lstream, field, '\t'))
			fields.push_back(field);

		if (fields.size() < 7) continue;

		LanSession session;
		session.ip = fields[0];
		session.port = fields[1];
		session.hostName = fields[2];
		session.systemName = fields[3];
		session.gameName = fields[4];
		session.romFile = fields[5];
		session.coreFile = fields[6];
		session.hasLocalMatch = false;
		session.safety = NetplaySafety::NONE;

		findLocalMatch(session);
		mSessions.push_back(session);
	}

	if (mSessions.empty())
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			"NO COMPATIBLE GAMES FOUND.",
			saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row);
		return;
	}

	// Legend as non-selectable subtitle
	mMenu.setSubtitle("[?] NOT FOUND LOCALLY", SA_SUBTITLE_COLOR);

	for (size_t i = 0; i < mSessions.size(); i++)
	{
		const LanSession& s = mSessions[i];

		std::string prefix = s.hasLocalMatch ? "  " : "? ";
		std::string label = prefix + s.gameName;

		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			label, saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);

		row.addElement(std::make_shared<TextComponent>(mWindow,
			s.hostName, saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR), false);

		size_t idx = i;
		row.makeAcceptInputHandler([this, idx] { joinSession(mSessions[idx]); });
		mMenu.addRow(row);
	}

	// Reposition
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2,
	                   (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  findLocalMatch
// ============================================================================

bool GuiNetplayLan::findLocalMatch(LanSession& session)
{
	// LAN sessions include the system name and ROM filename from the host.
	// We can match precisely: find the same system, then the same ROM filename.

	const std::vector<SystemData*>& systems = SystemData::sSystemVector;

	for (auto* sys : systems)
	{
		// Try matching the system name (including LG variants)
		// The host sends their ES system name (e.g. "snes", "snesLG")
		// We should also check the base system if names differ
		std::string sysName = sys->getName();
		if (sysName == "retropie" || sysName == "savestates")
			continue;

		// Match if same system name, or one is an LG variant of the other
		bool sysMatch = (sysName == session.systemName);
		if (!sysMatch)
		{
			// Strip "LG" suffix for comparison
			std::string baseSys = session.systemName;
			std::string baseLocal = sysName;
			if (baseSys.size() > 2 && baseSys.substr(baseSys.size() - 2) == "LG")
				baseSys = baseSys.substr(0, baseSys.size() - 2);
			if (baseLocal.size() > 2 && baseLocal.substr(baseLocal.size() - 2) == "LG")
				baseLocal = baseLocal.substr(0, baseLocal.size() - 2);
			sysMatch = (baseSys == baseLocal);
		}
		if (!sysMatch) continue;

		FileData* root = sys->getRootFolder();
		if (!root) continue;

		std::vector<FileData*> games = root->getFilesRecursive(GAME);

		for (auto* game : games)
		{
			std::string localFilename = Utils::FileSystem::getFileName(game->getPath());
			std::string remoteFilename = session.romFile;

			// Extract just the filename if it's a full path
			size_t lastSlash = remoteFilename.rfind('/');
			if (lastSlash != std::string::npos)
				remoteFilename = remoteFilename.substr(lastSlash + 1);

			if (Utils::String::toLower(localFilename) ==
			    Utils::String::toLower(remoteFilename))
			{
				NetplayGameInfo info = NetplayCore::getGameInfo(game);
				if (info.safety != NetplaySafety::NONE)
				{
					session.hasLocalMatch = true;
					session.localCorePath = info.corePath;
					session.localConfigPath = info.configPath;
					session.localRomPath = info.romPath;
					session.localSystemName = info.systemName;
					session.safety = info.safety;
					return true;
				}
			}
		}
	}

	return false;
}

// ============================================================================
//  joinSession
// ============================================================================

void GuiNetplayLan::joinSession(const LanSession& session)
{
	Window* window = mWindow;

	if (!session.hasLocalMatch)
	{
		window->pushGui(new GuiMsgBox(window,
			"GAME NOT FOUND\n\n"
			"YOU DON'T HAVE THIS GAME INSTALLED.\n"
			"ASK THE HOST TO PICK A GAME\n"
			"YOU BOTH HAVE.",
			"OK", nullptr));
		return;
	}

	NetplayConfig& cfg = NetplayConfig::get();

	// Prompt for player name if still default
	if (cfg.nickname == "Player" || cfg.nickname.empty())
	{
		window->pushGui(new GuiTextInput(window,
			"ENTER YOUR PLAYER NAME:",
			cfg.nickname.empty() ? "Player" : cfg.nickname,
			[window, session](const std::string& result)
			{
				std::string cleaned = NetplayConfig::sanitizeNickname(result);
				if (cleaned.empty()) cleaned = "Player";
				NetplayConfig::get().nickname = cleaned;
				NetplayConfig::get().save();

				NetplayConfig& cfg2 = NetplayConfig::get();

				std::string msg =
					"JOIN LAN GAME?\n\n"
					"GAME: " + Utils::String::toUpper(session.gameName) + "\n"
					"HOST: " + Utils::String::toUpper(session.hostName) + "\n"
					"PLAYER: " + Utils::String::toUpper(cfg2.nickname);

				window->pushGui(new GuiMsgBox(window, msg,
					"JOIN", [window, session]
					{
						NetplayGameInfo info;
						info.corePath = session.localCorePath;
						info.configPath = session.localConfigPath;
						info.romPath = session.localRomPath;
						info.systemName = session.localSystemName;
						info.safety = session.safety;

						NetplayLauncher::launchAsClientDirect(window, info,
							session.ip, session.port);
					},
					"CANCEL", nullptr));
			}));
		return;
	}

	std::string msg =
		"JOIN LAN GAME?\n\n"
		"GAME: " + Utils::String::toUpper(session.gameName) + "\n"
		"HOST: " + Utils::String::toUpper(session.hostName) + "\n"
		"PLAYER: " + Utils::String::toUpper(cfg.nickname);

	window->pushGui(new GuiMsgBox(window, msg,
		"JOIN", [window, session]
		{
			NetplayGameInfo info;
			info.corePath = session.localCorePath;
			info.configPath = session.localConfigPath;
			info.romPath = session.localRomPath;
			info.systemName = session.localSystemName;
			info.safety = session.safety;

			NetplayLauncher::launchAsClientDirect(window, info,
				session.ip, session.port);
		},
		"CANCEL", nullptr));
}

// ============================================================================
//  Input / Help
// ============================================================================

bool GuiNetplayLan::input(InputConfig* config, Input input)
{
	if (input.value != 0)
	{
		if (config->isMappedTo("b", input))
		{
			delete this;
			return true;
		}
	}
	return GuiComponent::input(config, input);
}

std::vector<HelpPrompt> GuiNetplayLan::getHelpPrompts()
{
	return mMenu.getHelpPrompts();
}

HelpStyle GuiNetplayLan::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	return style;
}
