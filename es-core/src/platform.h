#pragma once
#ifndef ES_CORE_PLATFORM_H
#define ES_CORE_PLATFORM_H

#include <string>

//why the hell this naming inconsistency exists is well beyond me
#ifdef WIN32
	#define sleep Sleep
#endif

enum QuitMode
{
	QUIT = 0,
	RESTART = 1,
	SHUTDOWN = 2,
	REBOOT = 3
};

// Set by runSystemCommand() when /tmp/es-restart is detected after a game exits.
// Checked by the main loop to trigger a clean ES restart. This flag cannot be
// consumed or flushed like SDL events can during the post-game resume sequence.
extern bool pendingRestart;

// Set by runSystemCommand() when the save_state_flag is "1" after a game exits.
// Checked by the main loop to trigger an in-place reload of the savestates
// gamelist — no ES restart needed, just a view refresh + toast notification.
extern bool pendingSavestateRefresh;

int runSystemCommand(const std::string& cmd_utf8); // run a utf-8 encoded in the shell (requires wstring conversion on Windows)
int quitES(QuitMode mode = QuitMode::QUIT);
void processQuitMode();

#endif // ES_CORE_PLATFORM_H
