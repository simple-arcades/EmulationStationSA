#include "NetplayLauncher.h"
#include "NetplayConfig.h"
#include "FileData.h"
#include "SystemData.h"
#include "Window.h"
#include "Log.h"
#include "guis/GuiMsgBox.h"
#include "AudioManager.h"
#include "VolumeControl.h"
#include "InputManager.h"
#include "Scripting.h"
#include "CollectionSystemManager.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"
#include "utils/TimeUtil.h"
#include "SAStyle.h"

#include "platform.h"

#include <fstream>
#include <sstream>
#include <unistd.h>
#include <cstdlib>

// ============================================================================
//  File paths
// ============================================================================

std::string NetplayLauncher::getSafeguardPath()  { return "/dev/shm/netplay_safeguard.cfg"; }
std::string NetplayLauncher::getLogPath()         { return "/dev/shm/netplay_retroarch.log"; }
std::string NetplayLauncher::getSaveDirPath()     { return "/dev/shm/netplay_saves"; }
std::string NetplayLauncher::getStateDirPath()    { return "/dev/shm/netplay_states"; }

// Debug logging — only writes if /home/pi/netplay_debug.flag exists.
// Create the flag to enable:  touch /home/pi/netplay_debug.flag
// Remove to disable:          rm /home/pi/netplay_debug.flag
static bool isDebugEnabled()
{
	return (access("/home/pi/netplay_debug.flag", F_OK) == 0);
}

static void debugLog(const std::string& text)
{
	if (!isDebugEnabled()) return;
	std::ofstream f("/home/pi/netplay_debug.log", std::ios::app);
	f << text;
}

// Show a PNG on the framebuffer using fbi (runs in background, RetroArch paints over it)
// imageName = filename in SA_BOOT_IMAGES_PATH, e.g. "netplay_host.png"
static void showBootImage(const std::string& imageName)
{
	std::string path = std::string(SA_BOOT_IMAGES_PATH) + imageName;
	if (access(path.c_str(), F_OK) != 0)
	{
		LOG(LogDebug) << "NetplayLauncher: Boot image not found: " << path;
		return;
	}
	// Clear console first, then show image
	// Use -nocomments to suppress font info, redirect tty1 to hide any remaining text
	std::string cmd = "printf '\\033[2J\\033[H\\033[?25l' >/dev/tty1 2>/dev/null; "
	                  "fbi -1 -t 2 -noverbose -nocomments -a \"" + path + "\" "
	                  "</dev/tty1 >/dev/null 2>/dev/null &";
	::system(cmd.c_str());
}

// ============================================================================
//  writeSafeguardAppendConfig
//
//  Ported directly from netplay_arcade.sh write_netplay_appendcfg().
//  Disables all actions that could cause desync or abuse in netplay.
// ============================================================================

void NetplayLauncher::writeSafeguardAppendConfig(const std::string& path,
                                                   const std::string& corePath,
                                                   const std::string& role)
{
	NetplayConfig& cfg = NetplayConfig::get();

	// Ensure temp directories exist (matching shell script: mkdir -p ... 2>/dev/null || true)
	std::string mkdirCmd = "mkdir -p \"" + getSaveDirPath() + "\" \"" + getStateDirPath() + "\" 2>/dev/null";
	::system(mkdirCmd.c_str());

	std::ofstream f(path, std::ios::trunc);
	if (!f.is_open())
	{
		LOG(LogError) << "NetplayLauncher: Could not write safeguard config to " << path;
		return;
	}

	// Core directory (critical for netplay to find the core)
	std::string coreDir = Utils::FileSystem::getParent(corePath);
	f << "libretro_directory = \"" << coreDir << "\"\n";

	// Redirect saves/states to RAM-backed temp dirs
	f << "savefile_directory = \"" << getSaveDirPath() << "\"\n";
	f << "savestate_directory = \"" << getStateDirPath() << "\"\n";

	// Disable save states (auto + hotkeys + notifications)
	f << "savestate_auto_save = \"false\"\n";
	f << "savestate_auto_load = \"false\"\n";
	f << "notification_show_save_state = \"false\"\n";
	f << "input_save_state = \"nul\"\n";
	f << "input_load_state = \"nul\"\n";
	f << "input_save_state_btn = \"nul\"\n";
	f << "input_load_state_btn = \"nul\"\n";
	f << "input_save_state_axis = \"nul\"\n";
	f << "input_load_state_axis = \"nul\"\n";
	f << "input_state_slot_increase = \"nul\"\n";
	f << "input_state_slot_decrease = \"nul\"\n";

	// Disable pause / reset / FF / slowmo / rewind / disc / menu
	f << "netplay_allow_pause = \"false\"\n";
	f << "input_pause_toggle = \"nul\"\n";
	f << "input_pause_toggle_btn = \"nul\"\n";
	f << "input_pause_toggle_axis = \"nul\"\n";
	f << "input_reset = \"nul\"\n";
	f << "input_reset_btn = \"nul\"\n";
	f << "input_reset_axis = \"nul\"\n";
	f << "input_toggle_fast_forward = \"nul\"\n";
	f << "input_hold_fast_forward = \"nul\"\n";
	f << "input_slowmotion = \"nul\"\n";
	f << "input_rewind = \"nul\"\n";
	f << "rewind_enable = \"false\"\n";
	f << "input_disk_eject_toggle = \"nul\"\n";
	f << "input_disk_next = \"nul\"\n";
	f << "input_disk_prev = \"nul\"\n";
	f << "input_menu_toggle = \"nul\"\n";
	f << "input_menu_toggle_btn = \"nul\"\n";
	f << "input_menu_toggle_axis = \"nul\"\n";

	// Passwords (apply to both host and client)
	if (!cfg.password.empty())
		f << "netplay_password = \"" << cfg.password << "\"\n";
	if (!cfg.spectatePassword.empty())
		f << "netplay_spectate_password = \"" << cfg.spectatePassword << "\"\n";

	// Role-specific settings
	if (role == "client")
	{
		// Client: don't inherit weird global settings
		f << "netplay_use_mitm_server = \"false\"\n";
		f << "netplay_nat_traversal = \"false\"\n";
	}
	else
	{
		// Host: configure based on user settings
		std::string publicAnnounce = "false";
		std::string useRelay = "false";
		std::string natTraversal = "false";

		if (cfg.mode == "online")
		{
			if (cfg.publicAnnounce == "auto" || cfg.publicAnnounce == "true")
				publicAnnounce = "true";

			if (cfg.onlineMethod == "relay")
				useRelay = "true";

			natTraversal = cfg.natTraversal;
		}
		// LAN mode: all forced off (already "false")

		f << "netplay_public_announce = \"" << publicAnnounce << "\"\n";
		f << "netplay_use_mitm_server = \"" << useRelay << "\"\n";
		f << "netplay_nat_traversal = \"" << natTraversal << "\"\n";

		f << "netplay_allow_slaves = \"" << cfg.allowSlaves << "\"\n";
		f << "netplay_max_connections = \"" << cfg.maxConnections << "\"\n";
		f << "netplay_max_ping = \"" << cfg.maxPing << "\"\n";
	}

	f.close();

	// Verify the file has libretro_directory
	std::ifstream check(path);
	std::string content((std::istreambuf_iterator<char>(check)),
	                     std::istreambuf_iterator<char>());
	if (content.find("libretro_directory") == std::string::npos)
		LOG(LogError) << "NetplayLauncher: Safeguard config missing libretro_directory!";
}

// ============================================================================
//  buildCommand
// ============================================================================

std::string NetplayLauncher::buildCommand(const NetplayGameInfo& info,
                                            const std::string& role,
                                            const std::string& hostIp,
                                            const std::string& hostPort)
{
	NetplayConfig& cfg = NetplayConfig::get();
	std::string port = hostPort.empty() ? cfg.port : hostPort;

	// Match the exact launch pattern from the working netplay_arcade.sh:
	//   "$RETROARCH" --verbose --log-file "$LOG" \
	//       -L "$core" --config "$config" \
	//       "$rom" \
	//       --appendconfig "$append" \
	//       --host \
	//       --port "$port" \
	//       --nick "$nick" \
	//       >/dev/null 2>&1

	std::string cmd = "/opt/simplearcades/retroarch/bin/retroarch";
	cmd += " --verbose";
	cmd += " --log-file \"" + getLogPath() + "\"";
	cmd += " -L \"" + info.corePath + "\"";
	cmd += " --config \"" + info.configPath + "\"";
	cmd += " \"" + info.romPath + "\"";
	cmd += " --appendconfig \"" + getSafeguardPath() + "\"";

	if (role == "host")
	{
		cmd += " --host";
		cmd += " --port " + cfg.port;
		cmd += " --nick \"" + cfg.nickname + "\"";
	}
	else
	{
		cmd += " --connect " + hostIp;
		cmd += " --port " + port;
		cmd += " --nick \"" + cfg.nickname + "\"";
	}

	// Redirect stdout/stderr to /dev/null — same as the shell script.
	// RetroArch writes its diagnostics to --log-file, so console output
	// is unnecessary and can interfere with ES's framebuffer handling.
	cmd += " >/dev/null 2>&1";

	return cmd;
}

// ============================================================================
//  executeCommand
//
//  Mirrors FileData::launchGame() — deinit ES, run command, reinit ES.
// ============================================================================

int NetplayLauncher::executeCommand(Window* window, FileData* game,
                                      const std::string& command,
                                      const std::string& role)
{
	LOG(LogInfo) << "NetplayLauncher: Executing: " << command;

	AudioManager::getInstance()->deinit();
	VolumeControl::getInstance()->deinit();
	InputManager::getInstance()->deinit();
	window->deinit();

	// Show boot image on framebuffer (fbi runs in background, RA paints over it)
	if (role == "host")
		showBootImage("netplay_host.png");
	else
		showBootImage("netplay_join.png");

	// Clear console text and hide cursor (prevents font loading messages showing)
	::system("clear >/dev/tty1 2>/dev/null; "
	         "printf '\\033[?25l' >/dev/tty1 2>/dev/null; "
	         "printf '\\033[2J\\033[H' >/dev/tty1 2>/dev/null");

	SimpleArcadesMusicManager::getInstance().onGameLaunched();

	if (game)
	{
		const std::string rom = Utils::FileSystem::getEscapedPath(game->getPath());
		const std::string basename = Utils::FileSystem::getStem(game->getPath());
		const std::string name = game->getName();
		Scripting::fireEvent("game-start", rom, basename, name);
	}

	int exitCode = runSystemCommand(command);

	// Restore cursor
	::system("printf '\\033[?25h' >/dev/tty1 2>/dev/null");

	SimpleArcadesMusicManager::getInstance().onGameReturned();

	if (game)
		Scripting::fireEvent("game-end");

	window->init();
	InputManager::getInstance()->init();
	VolumeControl::getInstance()->init();
	window->normalizeNextUpdate();

	// Update play count and last played (same as launchGame)
	if (game)
	{
		FileData* gameToUpdate = game->getSourceFileData();
		int timesPlayed = gameToUpdate->metadata.getInt("playcount") + 1;
		gameToUpdate->metadata.set("playcount",
			std::to_string(static_cast<long long>(timesPlayed)));
		gameToUpdate->metadata.set("lastplayed",
			Utils::Time::DateTime(Utils::Time::now()));
		CollectionSystemManager::get()->refreshCollectionSystems(gameToUpdate);
		gameToUpdate->getSystem()->onMetaDataSavePoint();
	}

	return exitCode;
}

// ============================================================================
//  handlePostLaunch — parse RA log and show user-friendly error messages
//
//  Ported from netplay_arcade.sh maybe_explain_netplay_failure().
// ============================================================================

void NetplayLauncher::handlePostLaunch(Window* window,
                                         int exitCode,
                                         const std::string& role,
                                         NetplaySafety safety,
                                         const std::string& hostIp,
                                         const std::string& hostPort)
{
	std::string logPath = getLogPath();
	bool hasLogFailure = false;

	// Read log for netplay error keywords
	std::string logContent;
	if (Utils::FileSystem::exists(logPath))
	{
		std::ifstream f(logPath);
		logContent = std::string((std::istreambuf_iterator<char>(f)),
		                          std::istreambuf_iterator<char>());

		// Check for netplay-specific failure patterns (not general log lines)
		std::vector<std::string> errorPatterns = {
			"Failed to connect", "Connection refused",
			"Core does not support", "cross-platform",
			"Port Mapping Failed", "UPnP",
			"wrong password", "Incorrect password",
			"authentication failed", "unauthorized",
			"timed out", "unreachable"
		};
		for (const auto& pat : errorPatterns)
		{
			if (logContent.find(pat) != std::string::npos)
			{
				hasLogFailure = true;
				break;
			}
		}
	}

	if (exitCode == 0 && !hasLogFailure)
	{
		// Clean exit, no errors — nothing to show
		return;
	}

	// --- STRICT core mismatch ---
	if (safety == NetplaySafety::STRICT && role == "client")
	{
		if (logContent.find("cross-platform") != std::string::npos ||
		    logContent.find("crossplay") != std::string::npos)
		{
			window->pushGui(new GuiMsgBox(window,
				"CONNECTION FAILED\n\n"
				"THIS GAME REQUIRES BOTH PLAYERS TO USE\n"
				"THE SAME HARDWARE. THE HOST MAY BE ON A PC\n"
				"OR A DIFFERENT DEVICE.\n\n"
				"TRY A DIFFERENT GAME.",
				"OK", nullptr));
			return;
		}
	}

	// --- Core doesn't support netplay ---
	if (logContent.find("Core does not support") != std::string::npos)
	{
		window->pushGui(new GuiMsgBox(window,
			"NOT SUPPORTED\n\n"
			"THIS GAME'S EMULATOR DOESN'T SUPPORT\n"
			"ONLINE PLAY. TRY A DIFFERENT GAME.\n\n"
			"NES, SNES, AND GENESIS GAMES\n"
			"USUALLY WORK BEST.",
			"OK", nullptr));
		return;
	}

	// --- Password required ---
	if (logContent.find("wrong password") != std::string::npos ||
	    logContent.find("password incorrect") != std::string::npos ||
	    logContent.find("unauthorized") != std::string::npos ||
	    logContent.find("authentication failed") != std::string::npos ||
	    logContent.find("Incorrect password") != std::string::npos)
	{
		window->pushGui(new GuiMsgBox(window,
			"PASSWORD REQUIRED\n\n"
			"THIS SESSION NEEDS A PASSWORD.\n"
			"SET IT IN NETPLAY SETTINGS >\n"
			"ADVANCED OPTIONS.",
			"OK", nullptr));
		return;
	}

	// --- UPnP / port mapping ---
	if (logContent.find("Port Mapping Failed") != std::string::npos ||
	    logContent.find("UPnP") != std::string::npos)
	{
		if (role == "host")
		{
			window->pushGui(new GuiMsgBox(window,
				"PORT MAPPING FAILED\n\n"
				"YOUR ROUTER COULDN'T OPEN THE PORT.\n\n"
				"SWITCH TO RELAY MODE IN NETPLAY SETTINGS\n"
				"FOR THE EASIEST FIX.",
				"OK", nullptr));
		}
		else
		{
			window->pushGui(new GuiMsgBox(window,
				"CONNECTION BLOCKED\n\n"
				"THE HOST'S NETWORK MAY BE BLOCKING\n"
				"CONNECTIONS. TRY A DIFFERENT SESSION\n"
				"OR ASK THE HOST TO USE RELAY MODE.",
				"OK", nullptr));
		}
		return;
	}

	// --- Host unreachable ---
	if (logContent.find("Failed to connect to host") != std::string::npos)
	{
		window->pushGui(new GuiMsgBox(window,
			"COULDN'T CONNECT\n\n"
			"THIS HOST ISN'T ACCEPTING CONNECTIONS.\n"
			"THEY MAY HAVE LEFT, OR THEIR NETWORK\n"
			"DOESN'T ALLOW OUTSIDE PLAYERS TO JOIN.\n\n"
			"NOT EVERY SESSION IN THE LIST WILL BE\n"
			"REACHABLE. TRY A DIFFERENT ONE!",
			"OK", nullptr));
		return;
	}

	// --- Generic connection failure ---
	if (logContent.find("Failed to connect") != std::string::npos ||
	    logContent.find("Connection refused") != std::string::npos ||
	    logContent.find("timed out") != std::string::npos ||
	    logContent.find("unreachable") != std::string::npos)
	{
		if (role == "client")
		{
			window->pushGui(new GuiMsgBox(window,
				"COULDN'T CONNECT\n\n"
				"THE HOST'S NETWORK ISN'T ALLOWING\n"
				"OUTSIDE PLAYERS TO JOIN.\n\n"
				"TRY A DIFFERENT SESSION.",
				"OK", nullptr));
		}
		else
		{
			window->pushGui(new GuiMsgBox(window,
				"HOSTING PROBLEM\n\n"
				"THERE WAS A NETWORK ERROR WHILE\n"
				"SETTING UP YOUR SESSION.\n\n"
				"CHECK YOUR WI-FI AND TRY AGAIN.",
				"OK", nullptr));
		}
		return;
	}

	// --- Non-zero exit with no netplay errors = generic crash ---
	if (exitCode != 0 && !hasLogFailure)
	{
		window->pushGui(new GuiMsgBox(window,
			"GAME CLOSED UNEXPECTEDLY\n\n"
			"TRY AGAIN. IF IT KEEPS HAPPENING,\n"
			"TRY A DIFFERENT GAME OR RESTART\n"
			"BOTH ARCADES.",
			"OK", nullptr));
		return;
	}

	// --- Fallback ---
	if (role == "client")
	{
		window->pushGui(new GuiMsgBox(window,
			"COULD NOT JOIN\n\n"
			"TRY A DIFFERENT SESSION OR\n"
			"CHECK NETPLAY SETTINGS.",
			"OK", nullptr));
	}
	else
	{
		window->pushGui(new GuiMsgBox(window,
			"HOSTING FAILED\n\n"
			"TRY AGAIN OR SWITCH TO RELAY MODE\n"
			"IN NETPLAY SETTINGS.",
			"OK", nullptr));
	}
}

// ============================================================================
//  LAN Broadcaster — Python UDP broadcast for discovery
//  Ported from netplay_arcade.sh start_lan_broadcaster()
// ============================================================================

static const std::string LAN_BROADCASTER_SCRIPT = R"PY(
import sys, json, time, socket, signal, subprocess, re

adv_port = int(sys.argv[1])
interval = float(sys.argv[2])
nick = sys.argv[3]
system = sys.argv[4]
game = sys.argv[5]
rom_file = sys.argv[6]
core_file = sys.argv[7]
netplay_port = int(sys.argv[8])

running = True
def _stop(*args):
    global running
    running = False
signal.signal(signal.SIGTERM, _stop)
signal.signal(signal.SIGINT, _stop)

bcast_addrs = set(["255.255.255.255"])
try:
    out = subprocess.check_output(["ip", "-4", "addr"])
    out = out.decode("utf-8", "ignore")
    for line in out.splitlines():
        line = line.strip()
        m = re.search(r"\binet\s+\d{1,3}(?:\.\d{1,3}){3}/\d+\s+brd\s+(\d{1,3}(?:\.\d{1,3}){3})\b", line)
        if m:
            bcast_addrs.add(m.group(1))
except Exception:
    pass

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

payload = {
    "sa": "netplay",
    "v": 1,
    "nick": nick,
    "system": system,
    "game": game,
    "rom": rom_file,
    "core": core_file,
    "port": netplay_port,
}

while running:
    payload["ts"] = int(time.time())
    data = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
    for addr in list(bcast_addrs):
        try:
            sock.sendto(data, (addr, adv_port))
        except Exception:
            pass
    time.sleep(interval)

try:
    sock.close()
except Exception:
    pass
)PY";

static const std::string LAN_BROADCASTER_PIDFILE = "/dev/shm/netplay_lan_broadcaster.pid";
static const std::string LAN_BROADCASTER_SCRIPTFILE = "/dev/shm/netplay_lan_broadcaster.py";
static const int LAN_DISCOVERY_PORT = 55439;

void NetplayLauncher::startLanBroadcaster(const NetplayGameInfo& info,
                                            const std::string& gameName)
{
	NetplayConfig& cfg = NetplayConfig::get();

	// Write the Python script
	{
		std::ofstream f(LAN_BROADCASTER_SCRIPTFILE, std::ios::trunc);
		f << LAN_BROADCASTER_SCRIPT;
	}

	// Extract just the ROM and core filenames
	std::string romFile = Utils::FileSystem::getFileName(info.romPath);
	std::string coreFile = Utils::FileSystem::getFileName(info.corePath);

	// Launch in background, save PID
	std::string cmd = "python3 \"" + LAN_BROADCASTER_SCRIPTFILE + "\""
		" " + std::to_string(LAN_DISCOVERY_PORT) +
		" 1"  // 1 second interval
		" \"" + cfg.nickname + "\""
		" \"" + info.systemName + "\""
		" \"" + gameName + "\""
		" \"" + romFile + "\""
		" \"" + coreFile + "\""
		" " + cfg.port +
		" >/dev/null 2>&1 & echo $!";

	FILE* fp = popen(cmd.c_str(), "r");
	if (fp)
	{
		char buf[64];
		if (fgets(buf, sizeof(buf), fp))
		{
			std::string pid(buf);
			// Trim whitespace
			while (!pid.empty() && (pid.back() == '\n' || pid.back() == '\r' || pid.back() == ' '))
				pid.pop_back();

			std::ofstream pidFile(LAN_BROADCASTER_PIDFILE, std::ios::trunc);
			pidFile << pid;

			LOG(LogInfo) << "NetplayLauncher: LAN broadcaster started, PID=" << pid;
		}
		pclose(fp);
	}
}

void NetplayLauncher::stopLanBroadcaster()
{
	if (Utils::FileSystem::exists(LAN_BROADCASTER_PIDFILE))
	{
		std::ifstream f(LAN_BROADCASTER_PIDFILE);
		std::string pid;
		std::getline(f, pid);
		f.close();

		if (!pid.empty())
		{
			std::string cmd = "kill " + pid + " 2>/dev/null; wait " + pid + " 2>/dev/null";
			::system(cmd.c_str());
			LOG(LogInfo) << "NetplayLauncher: LAN broadcaster stopped, PID=" << pid;
		}

		::unlink(LAN_BROADCASTER_PIDFILE.c_str());
	}
	::unlink(LAN_BROADCASTER_SCRIPTFILE.c_str());
}

// ============================================================================
//  cleanup
// ============================================================================

void NetplayLauncher::cleanup()
{
	// Stop broadcaster if somehow still running
	stopLanBroadcaster();

	// Remove temp files
	::unlink(getSafeguardPath().c_str());
	::unlink(getLogPath().c_str());

	// Remove temp save/state dirs and their contents
	std::string cmd = "rm -rf \"" + getSaveDirPath() + "\" \"" + getStateDirPath() + "\" 2>/dev/null";
	::system(cmd.c_str());

	LOG(LogInfo) << "NetplayLauncher: Cleanup complete";
}

// ============================================================================
//  launchAsHost
// ============================================================================

void NetplayLauncher::launchAsHost(Window* window, FileData* game)
{
	NetplayGameInfo info = NetplayCore::getGameInfo(game);
	if (info.safety == NetplaySafety::NONE)
	{
		window->pushGui(new GuiMsgBox(window,
			"THIS GAME DOESN'T SUPPORT ONLINE PLAY.",
			"OK", nullptr));
		return;
	}

	// Ensure nickname is set
	NetplayConfig& cfg = NetplayConfig::get();
	if (cfg.nickname.empty() || cfg.nickname == "Player")
	{
		// For Phase 2, just use "Player" — Phase 3 will prompt via OSK
		if (cfg.nickname.empty())
		{
			cfg.nickname = "Player";
			cfg.save();
		}
	}

	LOG(LogInfo) << "NetplayLauncher: Hosting " << game->getName()
	             << " [" << info.systemName << "] core=" << info.corePath;

	// Write safeguard config
	writeSafeguardAppendConfig(getSafeguardPath(), info.corePath, "host");

	// Build command
	std::string command = buildCommand(info, "host");

	// Start LAN broadcaster if in LAN mode
	bool lanBroadcasting = false;
	if (cfg.mode == "lan")
	{
		startLanBroadcaster(info, game->getName());
		lanBroadcasting = true;
	}

	// Debug: log command (only if /home/pi/netplay_debug.flag exists)
	debugLog("=== HOST " + game->getName() + " ===\n"
	         "COMMAND: " + command + "\n"
	         "CORE: " + info.corePath + "\n"
	         "CONFIG: " + info.configPath + "\n"
	         "ROM: " + info.romPath + "\n");

	int exitCode = executeCommand(window, game, command, "host");

	// Debug: log exit code and RA log
	if (isDebugEnabled())
	{
		std::string dbgText = "EXIT CODE: " + std::to_string(exitCode) + "\n";
		std::ifstream ralog(getLogPath());
		if (ralog.is_open())
		{
			std::string content((std::istreambuf_iterator<char>(ralog)),
			                     std::istreambuf_iterator<char>());
			dbgText += "RA LOG (" + std::to_string(content.size()) + " bytes):\n" + content + "\n";
		}
		dbgText += "=== END ===\n\n";
		debugLog(dbgText);
	}

	// Post-launch error handling
	handlePostLaunch(window, exitCode, "host", info.safety);

	// Stop LAN broadcaster if it was running
	if (lanBroadcasting)
		stopLanBroadcaster();

	// Cleanup temp files
	cleanup();
}

// ============================================================================
//  launchAsClient
// ============================================================================

void NetplayLauncher::launchAsClient(Window* window, FileData* game,
                                       const std::string& hostIp,
                                       const std::string& hostPort)
{
	NetplayGameInfo info = NetplayCore::getGameInfo(game);
	if (info.safety == NetplaySafety::NONE)
	{
		window->pushGui(new GuiMsgBox(window,
			"THIS GAME DOESN'T SUPPORT ONLINE PLAY.",
			"OK", nullptr));
		return;
	}

	LOG(LogInfo) << "NetplayLauncher: Joining " << game->getName()
	             << " at " << hostIp << ":" << hostPort;

	// Write safeguard config
	writeSafeguardAppendConfig(getSafeguardPath(), info.corePath, "client");

	// Build and execute command
	std::string command = buildCommand(info, "client", hostIp, hostPort);
	int exitCode = executeCommand(window, game, command, "client");

	// Post-launch error handling
	handlePostLaunch(window, exitCode, "client", info.safety, hostIp, hostPort);

	// Cleanup
	cleanup();
}

// ============================================================================
//  launchAsClientDirect — for lobby/LAN joins with explicit info
// ============================================================================

void NetplayLauncher::launchAsClientDirect(Window* window,
                                             const NetplayGameInfo& info,
                                             const std::string& hostIp,
                                             const std::string& hostPort)
{
	if (info.safety == NetplaySafety::NONE)
	{
		window->pushGui(new GuiMsgBox(window,
			"THIS GAME DOESN'T SUPPORT ONLINE PLAY.",
			"OK", nullptr));
		return;
	}

	LOG(LogInfo) << "NetplayLauncher: Joining direct at " << hostIp << ":" << hostPort
	             << " core=" << info.corePath;

	writeSafeguardAppendConfig(getSafeguardPath(), info.corePath, "client");

	std::string command = buildCommand(info, "client", hostIp, hostPort);

	// Debug: log command (only if /home/pi/netplay_debug.flag exists)
	debugLog("=== CLIENT JOIN " + hostIp + ":" + hostPort + " ===\n"
	         "COMMAND: " + command + "\n"
	         "CORE: " + info.corePath + "\n"
	         "CONFIG: " + info.configPath + "\n"
	         "ROM: " + info.romPath + "\n"
	         "SYSTEM: " + info.systemName + "\n");

	// No FileData for direct launches — pass nullptr
	int exitCode = executeCommand(window, nullptr, command, "client");

	// Debug: log exit code and RA log
	if (isDebugEnabled())
	{
		std::string dbgText = "EXIT CODE: " + std::to_string(exitCode) + "\n";
		std::ifstream ralog(getLogPath());
		if (ralog.is_open())
		{
			std::string content((std::istreambuf_iterator<char>(ralog)),
			                     std::istreambuf_iterator<char>());
			dbgText += "RA LOG (" + std::to_string(content.size()) + " bytes):\n" + content + "\n";
		}
		dbgText += "=== END ===\n\n";
		debugLog(dbgText);
	}

	handlePostLaunch(window, exitCode, "client", info.safety, hostIp, hostPort);

	cleanup();
}
