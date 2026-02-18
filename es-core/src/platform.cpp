#include "platform.h"

#include <SDL_events.h>
#ifdef WIN32
#include <codecvt>
#else
#include <unistd.h>
#endif
#include <fcntl.h>

#include "Log.h"

// For reading the save state flag file
#include <fstream>
#include <string>

int runShutdownCommand()
{
#ifdef WIN32 // windows
	return system("shutdown -s -t 0");
#else // osx / linux
	return system("sudo shutdown -h now");
#endif
}

int runRestartCommand()
{
#ifdef WIN32 // windows
	return system("shutdown -r -t 0");
#else // osx / linux
	return system("sudo shutdown -r now");
#endif
}

int runSystemCommand(const std::string& cmd_utf8)
{
#ifdef WIN32
	// on Windows we use _wsystem to support non-ASCII paths
	// which requires converting from utf8 to a wstring
	typedef std::codecvt_utf8<wchar_t> convert_type;
	std::wstring_convert<convert_type, wchar_t> converter;
	std::wstring wchar_str = converter.from_bytes(cmd_utf8);
	return _wsystem(wchar_str.c_str());
#else
	int ret = system(cmd_utf8.c_str());

	// When a game exits, check if a save state was created during gameplay.
	// The watcher script sets this flag to "1" when it detects a new save.
	// We check it HERE — at the exact moment system() returns — before
	// ES's resume/reinit sequence runs. If a save was detected, we set
	// pendingSavestateRefresh so the main loop can reload the savestates
	// gamelist in-place (no full ES restart needed).
	const std::string flagPath = "/home/pi/simplearcades/flags/save_state_flag.flag";
	if (access(flagPath.c_str(), F_OK) == 0)
	{
		std::ifstream flagFile(flagPath);
		std::string flagValue;
		if (flagFile.is_open())
		{
			std::getline(flagFile, flagValue);
			flagFile.close();
		}

		// Trim whitespace
		while (!flagValue.empty() && (flagValue.back() == ' ' || flagValue.back() == '\n' || flagValue.back() == '\r'))
			flagValue.pop_back();

		if (flagValue == "1")
		{
			LOG(LogInfo) << "SA_SAVESTATE: save_state_flag is '1' after game exit, setting pendingSavestateRefresh";
			pendingSavestateRefresh = true;

			// Reset the flag to "0" immediately so it doesn't fire again
			std::ofstream resetFile(flagPath, std::ios::trunc);
			if (resetFile.is_open())
			{
				resetFile << "0";
				resetFile.close();
			}
		}
	}

	return ret;
#endif
}

QuitMode quitMode = QuitMode::QUIT;
bool pendingRestart = false;
bool pendingSavestateRefresh = false;

int quitES(QuitMode mode)
{
	quitMode = mode;

	SDL_Event *quit = new SDL_Event();
	quit->type = SDL_QUIT;
	SDL_PushEvent(quit);
	return 0;
}

void touch(const std::string& filename)
{
#ifdef WIN32
	FILE* fp = fopen(filename.c_str(), "ab+");
	if (fp != NULL)
		fclose(fp);
#else
	int fd = open(filename.c_str(), O_CREAT|O_WRONLY, 0644);
	if (fd >= 0)
		close(fd);
#endif
}

void processQuitMode()
{
	switch (quitMode)
	{
	case QuitMode::RESTART:
		LOG(LogInfo) << "Restarting EmulationStation";
		touch("/tmp/es-restart");
		break;
	case QuitMode::REBOOT:
		LOG(LogInfo) << "Rebooting system";
		touch("/tmp/es-sysrestart");
		// Shell wrapper plays reboot video then calls sudo reboot
		break;
	case QuitMode::SHUTDOWN:
		LOG(LogInfo) << "Shutting system down";
		touch("/tmp/es-shutdown");
		// Shell wrapper plays shutdown video then calls sudo poweroff
		break;
	default:
		// No-op to prevent compiler warnings
		// If we reach here, it is not a RESTART, REBOOT,
		// or SHUTDOWN. Basically a normal exit.
		break;
	}
}
