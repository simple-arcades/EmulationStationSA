#include "guis/GuiNetplayLobby.h"
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
#include <algorithm>
#include <map>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

// ============================================================================
//  Lobby URL and Python parser
//
//  We shell out to curl + python3 for the lobby fetch/parse, exactly
//  matching the proven approach from netplay_arcade.sh.
//  This avoids adding JSON and HTTP library dependencies to ES.
// ============================================================================

static const std::string LOBBY_URL = "http://lobby.libretro.com/list";
static const std::string PARSED_OUTPUT = "/dev/shm/netplay_lobby_parsed.tsv";

// Python script that fetches and parses the lobby JSON.
// Outputs TSV: game \t host \t ip \t port \t core \t safety \t filename \t crc \t conntype
static const std::string FETCH_SCRIPT = R"PY(
import sys, json, re, subprocess

SAFE_CORES = [
    "snes9x", "fceumm", "nestopia", "genesis_plus_gx", "picodrive",
    "beetle_pce_fast", "mednafen_pce_fast", "gambatte", "stella",
    "beetle_ngp", "mednafen_ngp", "beetle_wswan", "mednafen_wswan",
    "beetle_vb", "mednafen_vb", "beetle_supergrafx", "mednafen_supergrafx"
]

try:
    raw = subprocess.check_output(
        ["curl", "-4", "-fsS", "--connect-timeout", "5", "--max-time", "15",
         "-H", "User-Agent: SimpleArcades-Netplay/3.0", sys.argv[1]],
        stderr=subprocess.DEVNULL
    ).decode("utf-8", errors="ignore")

    data = json.loads(raw)
    if not isinstance(data, list):
        sys.exit(1)

    out = open(sys.argv[2], "w")

    for item in data[:200]:
        fields = item.get("fields", {})
        if not fields:
            continue

        game = str(fields.get("game_name", "")).strip()
        if not game:
            continue

        user = str(fields.get("username", "Unknown")).strip()

        ip = ""
        for key in ["ip", "host_ip", "address"]:
            val = str(fields.get(key, "")).strip()
            if val:
                match = re.search(r'(\d{1,3}(?:\.\d{1,3}){3})', val)
                if match:
                    ip = match.group(1)
                    break
        if not ip:
            continue

        port = str(fields.get("port", "55435")).strip()
        core_name = str(fields.get("core_name", "")).strip()

        filename = str(fields.get("filename", "")).strip()
        if not filename:
            # Try content_name (lobby sometimes has this instead)
            content = str(fields.get("content_name", "")).strip()
            if content:
                filename = content
            elif "." in game and len(game) < 80:
                filename = game

        # Extract CRC — lobby provides it as game_crc or crc
        crc = str(fields.get("game_crc", "")).strip()
        if not crc:
            crc = str(fields.get("crc", "")).strip()
        # Normalize: uppercase, strip any 0x prefix
        if crc:
            crc = crc.upper().replace("0X", "")
            # Ensure 8 chars
            if len(crc) < 8:
                crc = crc.zfill(8)

        # Detect connection type: RELAY vs DIRECT
        # Relay sessions have mitm_server/mitm_session fields, or
        # the host_method field indicates relay usage
        conntype = "DIRECT"
        mitm = str(fields.get("mitm_server", "")).strip()
        mitm_session = str(fields.get("mitm_session", "")).strip()
        host_method = str(fields.get("host_method", "")).strip()
        if mitm or mitm_session:
            conntype = "RELAY"
        elif "mitm" in host_method.lower() or "relay" in host_method.lower():
            conntype = "RELAY"

        safety = "STRICT"
        norm_core = core_name.lower().replace(" ", "_").replace("-", "_")
        for safe in SAFE_CORES:
            if safe in norm_core:
                safety = "OPEN"
                break

        out.write(f"{game}\t{user}\t{ip}\t{port}\t{core_name}\t{safety}\t{filename}\t{crc}\t{conntype}\n")

    out.close()

except Exception as e:
    sys.exit(1)
)PY";

// ============================================================================
//  Constructor
// ============================================================================

GuiNetplayLobby::GuiNetplayLobby(Window* window,
                                   const std::string& filterGame,
                                   const std::string& filterSystem)
	: GuiComponent(window),
	  mMenu(window, filterGame.empty() ? "ONLINE GAMES" : "FIND A MATCH"),
	  mFilterGame(filterGame),
	  mFilterSystem(filterSystem),
	  mCRCLoaded(false)
{
	addChild(&mMenu);
	fetchAndBuild();
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2,
	                   (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  fetchAndBuild — runs curl+python, then builds the menu
// ============================================================================

void GuiNetplayLobby::fetchAndBuild()
{
	// Write the Python script to a temp file
	std::string scriptPath = "/dev/shm/netplay_lobby_fetch.py";
	{
		std::ofstream f(scriptPath, std::ios::trunc);
		f << FETCH_SCRIPT;
	}

	// Run it
	std::string cmd = "python3 \"" + scriptPath + "\" \"" + LOBBY_URL + "\" \""
	                  + PARSED_OUTPUT + "\" 2>/dev/null";
	int rc = ::system(cmd.c_str());

	// Clean up script
	::unlink(scriptPath.c_str());

	if (rc != 0 || !Utils::FileSystem::exists(PARSED_OUTPUT))
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			"COULD NOT LOAD ONLINE GAMES.",
			saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row);

		ComponentListRow row2;
		row2.addElement(std::make_shared<TextComponent>(mWindow,
			"CHECK YOUR INTERNET CONNECTION.",
			saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row2);
		return;
	}

	// Read parsed TSV
	std::ifstream f(PARSED_OUTPUT);
	std::string rawTsv((std::istreambuf_iterator<char>(f)),
	                     std::istreambuf_iterator<char>());
	::unlink(PARSED_OUTPUT.c_str());

	if (rawTsv.empty())
	{
		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			"NO ONLINE GAMES AVAILABLE RIGHT NOW.",
			saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row);
		return;
	}

	buildSessionList(rawTsv);
}

// ============================================================================
//  buildSessionList — parse TSV lines into LobbySession structs, build menu
// ============================================================================

void GuiNetplayLobby::buildSessionList(const std::string& rawTsv)
{
	std::istringstream stream(rawTsv);
	std::string line;

	while (std::getline(stream, line))
	{
		if (line.empty()) continue;

		// Parse TSV: game \t host \t ip \t port \t core \t safety \t filename \t crc \t conntype
		std::vector<std::string> fields;
		std::istringstream lstream(line);
		std::string field;
		while (std::getline(lstream, field, '\t'))
			fields.push_back(field);

		if (fields.size() < 6) continue;

		LobbySession session;
		session.gameName = fields[0];
		session.hostName = fields[1];
		session.ip = fields[2];
		session.port = fields[3];
		session.coreName = fields[4];
		session.safety = (fields[5] == "OPEN") ? NetplaySafety::OPEN : NetplaySafety::STRICT;
		session.remoteFilename = (fields.size() > 6) ? fields[6] : "";
		session.remoteCRC = (fields.size() > 7) ? fields[7] : "";
		session.connType = (fields.size() > 8) ? fields[8] : "DIRECT";
		session.hasLocalMatch = false;

		// Apply filter if FIND A MATCH mode
		if (!mFilterGame.empty())
		{
			// Case-insensitive partial match on game name
			std::string filterLower = Utils::String::toLower(mFilterGame);
			std::string gameLower = Utils::String::toLower(session.gameName);
			if (gameLower.find(filterLower) == std::string::npos &&
			    filterLower.find(gameLower) == std::string::npos)
				continue;
		}

		// Try to find a local match
		findLocalMatch(session);

		// Only show games we can actually join (have locally with compatible core)
		if (!session.hasLocalMatch)
			continue;

		mSessions.push_back(session);
	}

	if (mSessions.empty())
	{
		std::string msg = mFilterGame.empty()
			? "NO COMPATIBLE GAMES FOUND."
			: "NO MATCHES FOUND FOR THIS GAME.";

		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			msg, saFont(FONT_SIZE_MEDIUM), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row);

		ComponentListRow row2;
		row2.addElement(std::make_shared<TextComponent>(mWindow,
			mFilterGame.empty()
				? "NO ONE IS HOSTING A GAME YOU HAVE."
				: "TRY HOSTING INSTEAD.",
			saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR), true);
		mMenu.addRow(row2);
		return;
	}

	// Legend as non-selectable subtitle under the title bar
	mMenu.setSubtitle("[+] CROSS-PLAY SAFE   [!] SAME HARDWARE ONLY", SA_SUBTITLE_COLOR);

	// Sort: [+] OPEN first, then [!] STRICT
	std::sort(mSessions.begin(), mSessions.end(),
		[](const LobbySession& a, const LobbySession& b)
		{
			int scoreA = (a.safety == NetplaySafety::OPEN) ? 0 : 1;
			int scoreB = (b.safety == NetplaySafety::OPEN) ? 0 : 1;
			return scoreA < scoreB;
		});

	// Build menu rows
	for (size_t i = 0; i < mSessions.size(); i++)
	{
		const LobbySession& s = mSessions[i];

		std::string prefix = (s.safety == NetplaySafety::OPEN) ? "[+] " : "[!] ";

		std::string label = prefix + s.gameName;

		ComponentListRow row;
		row.addElement(std::make_shared<TextComponent>(mWindow,
			label, saFont(FONT_SIZE_MEDIUM), SA_TEXT_COLOR), true);

		// Right side: host name
		row.addElement(std::make_shared<TextComponent>(mWindow,
			s.hostName, saFont(FONT_SIZE_SMALL), SA_SUBTITLE_COLOR), false);

		size_t idx = i;
		row.makeAcceptInputHandler([this, idx] { joinSession(mSessions[idx]); });
		mMenu.addRow(row);
	}

	// Reposition after adding rows
	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2,
	                   (mSize.y() - mMenu.getSize().y()) / 2);
}

// ============================================================================
//  loadCRCDatabase — reads .netplay_crc files from all system ROM dirs
// ============================================================================

void GuiNetplayLobby::loadCRCDatabase()
{
	if (mCRCLoaded) return;
	mCRCLoaded = true;

	const std::vector<SystemData*>& systems = SystemData::sSystemVector;

	for (auto* sys : systems)
	{
		if (sys->getName() == "settings" || sys->getName() == "savestates")
			continue;

		std::string crcPath = sys->getRootFolder()->getPath() + "/.netplay_crc";
		std::ifstream file(crcPath);
		if (!file.is_open()) continue;

		// Get netplay info for this system via a dummy check
		// We need corePath and configPath — use getGameInfo on first game
		FileData* root = sys->getRootFolder();
		if (!root) continue;

		std::vector<FileData*> games = root->getFilesRecursive(GAME);
		if (games.empty()) continue;

		// Get core info from the first game (all games in a system share the same core)
		NetplayGameInfo sampleInfo = NetplayCore::getGameInfo(games[0]);
		if (sampleInfo.safety == NetplaySafety::NONE) continue;

		std::string corePath = sampleInfo.corePath;
		std::string configPath = sampleInfo.configPath;

		std::string line;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#') continue;

			size_t tab = line.find('\t');
			if (tab == std::string::npos) continue;

			std::string filename = line.substr(0, tab);
			std::string crc = line.substr(tab + 1);

			// Normalize CRC to uppercase
			std::transform(crc.begin(), crc.end(), crc.begin(), ::toupper);

			if (!crc.empty() && crc != "00000000")
			{
				CRCEntry entry;
				entry.corePath = corePath;
				entry.configPath = configPath;
				entry.romPath = sys->getRootFolder()->getPath() + "/" + filename;
				entry.systemName = sys->getName();
				mCRCDatabase[crc] = entry;
			}
		}
	}

	LOG(LogInfo) << "NetplayLobby: Loaded " << mCRCDatabase.size() << " CRC entries";
}

// ============================================================================
//  findLocalMatch — search local gamelists for a matching game
//  Priority: 1) CRC match  2) Exact filename  3) Stem match  4) Name match
// ============================================================================

bool GuiNetplayLobby::findLocalMatch(LobbySession& session)
{
	// Try CRC match first (most reliable, works cross-platform)
	if (!session.remoteCRC.empty() && session.remoteCRC != "00000000")
	{
		loadCRCDatabase();

		std::string crc = session.remoteCRC;
		std::transform(crc.begin(), crc.end(), crc.begin(), ::toupper);

		auto it = mCRCDatabase.find(crc);
		if (it != mCRCDatabase.end())
		{
			session.hasLocalMatch = true;
			session.localCorePath = it->second.corePath;
			session.localConfigPath = it->second.configPath;
			session.localRomPath = it->second.romPath;
			session.localSystemName = it->second.systemName;
			return true;
		}
	}

	// Fall back to filename/name matching
	const std::vector<SystemData*>& systems = SystemData::sSystemVector;

	for (auto* sys : systems)
	{
		if (sys->getName() == "settings" || sys->getName() == "savestates")
			continue;

		// Get the game list root
		FileData* root = sys->getRootFolder();
		if (!root) continue;

		std::vector<FileData*> games = root->getFilesRecursive(GAME);

		for (auto* game : games)
		{
			// Skip single-player games (players metadata must be 2+)
			// If no player data exists, allow it (can't filter)
			const std::string& playersStr = game->metadata.get("players");
			if (!playersStr.empty())
			{
				int maxPlayers = 1;
				size_t dash = playersStr.find('-');
				if (dash != std::string::npos)
					maxPlayers = atoi(playersStr.substr(dash + 1).c_str());
				else
					maxPlayers = atoi(playersStr.c_str());

				if (maxPlayers < 2)
					continue;  // Single-player only — skip
			}

			// Try matching by filename first (most reliable)
			if (!session.remoteFilename.empty())
			{
				std::string localFilename = Utils::FileSystem::getFileName(game->getPath());
				std::string remoteFilename = session.remoteFilename;

				// Compare with extensions
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
						return true;
					}
				}

				// Compare without extensions (lobby often strips them)
				std::string localStem = Utils::FileSystem::getStem(game->getPath());
				std::string remoteStem = remoteFilename;
				// Strip extension from remote if it has one
				size_t dotPos = remoteStem.rfind('.');
				if (dotPos != std::string::npos)
					remoteStem = remoteStem.substr(0, dotPos);

				if (Utils::String::toLower(localStem) ==
				    Utils::String::toLower(remoteStem))
				{
					NetplayGameInfo info = NetplayCore::getGameInfo(game);
					if (info.safety != NetplaySafety::NONE)
					{
						session.hasLocalMatch = true;
						session.localCorePath = info.corePath;
						session.localConfigPath = info.configPath;
						session.localRomPath = info.romPath;
						session.localSystemName = info.systemName;
						return true;
					}
				}
			}

			// Exact name match (fallback if filename didn't match)
			std::string localName = Utils::String::toLower(game->getName());
			std::string remoteName = Utils::String::toLower(session.gameName);
			if (localName == remoteName)
			{
				NetplayGameInfo info = NetplayCore::getGameInfo(game);
				if (info.safety != NetplaySafety::NONE)
				{
					session.hasLocalMatch = true;
					session.localCorePath = info.corePath;
					session.localConfigPath = info.configPath;
					session.localRomPath = info.romPath;
					session.localSystemName = info.systemName;
					return true;
				}
			}
		}
	}

	return false;
}

// ============================================================================
//  joinSession — confirm and launch as client
// ============================================================================

void GuiNetplayLobby::joinSession(const LobbySession& session)
{
	Window* window = mWindow;
	NetplayConfig& cfg = NetplayConfig::get();

	// Build notes that appear in the confirmation dialog
	auto buildNotes = [](const LobbySession& s) -> std::string
	{
		std::string notes;
		if (s.safety == NetplaySafety::STRICT)
			notes += "\n\nNOTE: THIS GAME REQUIRES BOTH PLAYERS\nTO USE THE SAME TYPE OF HARDWARE.";
		if (s.connType == "RELAY")
			notes += "\n\nTHIS HOST'S CONNECTION MAY NOT\n"
			         "ALLOW YOU TO JOIN. YOU CAN TRY,\n"
			         "BUT IF IT DOESN'T CONNECT, LOOK\n"
			         "FOR A DIFFERENT SESSION.";
		return notes;
	};

	// Prompt for player name if still default
	if (cfg.nickname == "Player" || cfg.nickname.empty())
	{
		window->pushGui(new GuiTextInput(window,
			"ENTER YOUR PLAYER NAME:",
			cfg.nickname.empty() ? "Player" : cfg.nickname,
			[window, session, buildNotes](const std::string& result)
			{
				std::string cleaned = NetplayConfig::sanitizeNickname(result);
				if (cleaned.empty()) cleaned = "Player";
				NetplayConfig::get().nickname = cleaned;
				NetplayConfig::get().save();

				NetplayConfig& cfg2 = NetplayConfig::get();

				std::string msg =
					"JOIN THIS GAME?\n\n"
					"GAME: " + Utils::String::toUpper(session.gameName) + "\n"
					"HOST: " + Utils::String::toUpper(session.hostName) + "\n"
					"PLAYER: " + Utils::String::toUpper(cfg2.nickname) +
					buildNotes(session);

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
		"JOIN THIS GAME?\n\n"
		"GAME: " + Utils::String::toUpper(session.gameName) + "\n"
		"HOST: " + Utils::String::toUpper(session.hostName) + "\n"
		"PLAYER: " + Utils::String::toUpper(cfg.nickname) +
		buildNotes(session);

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

bool GuiNetplayLobby::input(InputConfig* config, Input input)
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

std::vector<HelpPrompt> GuiNetplayLobby::getHelpPrompts()
{
	return mMenu.getHelpPrompts();
}

HelpStyle GuiNetplayLobby::getHelpStyle()
{
	HelpStyle style = HelpStyle();
	return style;
}
