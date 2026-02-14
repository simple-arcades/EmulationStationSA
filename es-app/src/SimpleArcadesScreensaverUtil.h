#pragma once
#include <string>
#include <unordered_map>
#include <vector>

class SimpleArcadesScreensaverUtil
{
public:
	// Returns absolute paths of enabled videos. Also syncs the config with the filesystem.
	static std::vector<std::string> syncAndGetEnabledVideos();
	
	// Convenience helpers (also used by the gallery UI later)
	static std::string getRootDir();      // /home/pi/simplearcades/media/videos/screensavers (fallback: $HOME/simplearcades/...)
	static std::string getConfigPath();   // /home/pi/simplearcades/config/screensavers/allowlist.cfg (fallback: $HOME/simplearcades/config/screensavers/allowlist.cfg)
	
	// Returns all relative videos + their enabled state (and syncs config with filesystem).
	static bool syncSelection(std::vector<std::string>& allRel,
		std::unordered_map<std::string, bool>& enabledByRel);

	// Writes allowlist.cfg using the standard format:
	// enabled:  rel/path.mp4
	// disabled: # rel/path.mp4
	static bool writeSelection(const std::vector<std::string>& allRel,
		const std::unordered_map<std::string, bool>& enabledByRel);

};
