#include "platform.h"

#include <SDL_events.h>
#ifdef WIN32
#include <codecvt>
#else
#include <unistd.h>
#endif
#include <fcntl.h>

#include "Log.h"

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

	// When a game exits, onend.sh may have created /tmp/es-restart
	// to signal that ES should restart (e.g. after a save state).
	// Set a global flag HERE — at the exact moment system() returns.
	// We cannot use SDL_PushEvent(SDL_QUIT) because ES flushes the
	// event queue during its post-game resume/reinit sequence.
	// The main loop checks pendingRestart directly — no events to
	// consume, no file I/O, just a memory read.
	if (access("/tmp/es-restart", F_OK) == 0)
	{
		LOG(LogInfo) << "SA_RESTART_TRIGGER: /tmp/es-restart detected after system command, setting pendingRestart";
		pendingRestart = true;
	}

	return ret;
#endif
}

QuitMode quitMode = QuitMode::QUIT;
bool pendingRestart = false;

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
