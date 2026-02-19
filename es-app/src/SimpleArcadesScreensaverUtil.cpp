#include "SimpleArcadesScreensaverUtil.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <set>
#include <cstdlib>
#include <sstream>

static bool isLikelyVideoFile(const std::string& path)
{
	std::string ext = Utils::FileSystem::getExtension(path);
	ext = Utils::String::toLower(ext);
	// Keep this conservative; you can add more later if needed.
	return (ext == ".mp4" || ext == ".m4v" || ext == ".mkv" || ext == ".avi" || ext == ".mov" || ext == ".webm");
}

static std::string joinPath(const std::string& a, const std::string& b)
{
	if (a.empty()) return b;
	if (a.back() == '/') return a + b;
	return a + "/" + b;
}

static bool isSafeRelative(const std::string& rel)
{
	if (rel.empty()) return false;
	if (rel[0] == '/') return false;
	if (rel.find("..") != std::string::npos) return false;
	return true;
}

std::string SimpleArcadesScreensaverUtil::getRootDir()
{
	// Preferred location (your chosen layout)
	const std::string preferred = "/home/pi/simplearcades/media/videos/screensavers";
	if (Utils::FileSystem::exists(preferred))
		return preferred;

	// Fallback: $HOME/simplearcades/media/videos/screensavers
	const char* home = std::getenv("HOME");
	if (home && *home)
	{
		std::string fallback = std::string(home) + "/simplearcades/media/videos/screensavers";
		if (Utils::FileSystem::exists(fallback))
			return fallback;
	}

	// If neither exists, still return preferred so callers have a deterministic target.
	return preferred;
}

std::string SimpleArcadesScreensaverUtil::getConfigPath()
{
	// WRITE PATH (always .cfg going forward)
	// We never auto-migrate on menu open; we only WRITE when the Gallery saves.
	const std::string preferredDir = "/home/pi/simplearcades/config/screensavers";
	const std::string filename     = "allowlist.cfg";

	const std::string preferredFile = preferredDir + "/" + filename;

	// Best-effort: ensure preferred directory exists (for typical Pi installs)
	if (!Utils::FileSystem::exists(preferredDir))
		Utils::FileSystem::createDirectory(preferredDir);

	if (Utils::FileSystem::exists(preferredDir))
		return preferredFile;

	// Fallback: $HOME/simplearcades/config/screensavers (for non-pi builds / other users)
	const char* home = std::getenv("HOME");
	if (home != nullptr && *home)
	{
		const std::string fallbackDir  = std::string(home) + "/simplearcades/config/screensavers";
		const std::string fallbackFile = fallbackDir + "/" + filename;

		// Best-effort: ensure fallback directory exists
		if (!Utils::FileSystem::exists(fallbackDir))
			Utils::FileSystem::createDirectory(fallbackDir);

		if (Utils::FileSystem::exists(fallbackDir))
			return fallbackFile;
	}

	// Worst case: return preferredFile; callers may fail gracefully.
	return preferredFile;
}


static std::string getConfigReadPath()
{
	// READ PATH (prefer .cfg; fall back to legacy .config if .cfg doesn't exist yet)
	const std::string preferredDir = "/home/pi/simplearcades/config/screensavers";
	const char* home = std::getenv("HOME");
	const std::string fallbackDir = (home != nullptr && std::string(home).size() > 0)
		? (std::string(home) + "/simplearcades/config/screensavers")
		: std::string();

	const std::string cfgName    = "allowlist.cfg";
	const std::string legacyName = "allowlist.config";

	auto pickExisting = [&](const std::string& dir) -> std::string {
		if (dir.empty())
			return std::string();

		const std::string cfgFile    = dir + "/" + cfgName;
		const std::string legacyFile = dir + "/" + legacyName;

		if (Utils::FileSystem::exists(cfgFile))
			return cfgFile;

		if (Utils::FileSystem::exists(legacyFile))
			return legacyFile;

		return std::string();
	};

	// Prefer preferredDir if present
	std::string p = pickExisting(preferredDir);
	if (!p.empty())
		return p;

	// Then fallbackDir if present
	std::string f = pickExisting(fallbackDir);
	if (!f.empty())
		return f;

	// If nothing exists yet, just read from where we will write.
	return SimpleArcadesScreensaverUtil::getConfigPath();
}


static bool hasVideoExtension(const std::string& rel)
{
	std::string ext = Utils::FileSystem::getExtension(rel);
	ext = Utils::String::toLower(ext);

	// Keep this list aligned with scanAllVideosRelative()
	return (ext == ".mp4" || ext == ".m4v" || ext == ".mkv" || ext == ".avi" ||
		ext == ".mov" || ext == ".mpg" || ext == ".mpeg" || ext == ".webm");
}

static std::string normalizeRel(std::string rel)
{
	rel = Utils::String::trim(rel);
	for (auto& c : rel)
		if (c == '\\') c = '/';

	// Strip leading "./" and leading "/"
	while (rel.rfind("./", 0) == 0)
		rel = rel.substr(2);
	while (!rel.empty() && rel[0] == '/')
		rel.erase(0, 1);

	// Collapse accidental double slashes
	while (rel.find("//") != std::string::npos)
	{
		size_t pos = 0;
		while ((pos = rel.find("//", pos)) != std::string::npos)
			rel.replace(pos, 2, "/");
	}

	return rel;
}


static std::unordered_map<std::string, bool> loadSelectionFile(const std::string& path)
{
	std::unordered_map<std::string, bool> out;

	std::ifstream in(path);
	if (!in.good())
		return out;

	std::string line;
	while (std::getline(in, line))
	{
		line = Utils::String::trim(line);
		if (line.empty())
			continue;

		bool enabled = true;
		if (!line.empty() && line[0] == '#')
		{
			enabled = false;
			line.erase(0, 1);
			line = Utils::String::trim(line);
			if (line.empty())
				continue;
		}

		line = normalizeRel(line);

		// Ignore non-entry comments (e.g. header lines) and unsafe paths
		if (!isSafeRelative(line))
			continue;
		if (!hasVideoExtension(line))
			continue;

		out[line] = enabled;
	}

	return out;
}

static std::vector<std::string> scanAllVideosRelative(const std::string& root)
{
	std::vector<std::string> rels;

	if (!Utils::FileSystem::exists(root))
		return rels;

	// recurse = true
	auto files = Utils::FileSystem::getDirContent(root, true);
	for (auto it = files.cbegin(); it != files.cend(); ++it)
	{
		const std::string& abs = *it;
		if (!Utils::FileSystem::isRegularFile(abs))
			continue;
		if (!isLikelyVideoFile(abs))
			continue;

		// Convert abs -> relative to root
		if (abs.size() <= root.size())
			continue;
		if (abs.compare(0, root.size(), root) != 0)
			continue;

		std::string rel = abs.substr(root.size());
		if (!rel.empty() && rel[0] == '/')
			rel = rel.substr(1);

		if (!isSafeRelative(rel))
			continue;

		rels.push_back(rel);
	}

	std::sort(rels.begin(), rels.end());
	rels.erase(std::unique(rels.begin(), rels.end()), rels.end());
	return rels;
}

static bool writeSelectionFile(const std::string& cfgPath,
                               const std::vector<std::string>& allRels,
                               const std::unordered_map<std::string, bool>& sel)
{
	// Write to temp then replace (avoid partial writes)
	const std::string tmp = cfgPath + ".tmp";

	// Best-effort: ensure target directory exists (avoid failing if folder was missing)
	{
		const std::size_t p = cfgPath.find_last_of("/\\");
		if (p != std::string::npos)
		{
			const std::string dir = cfgPath.substr(0, p);
			if (!dir.empty() && !Utils::FileSystem::exists(dir))
				Utils::FileSystem::createDirectory(dir);
		}
	}

	std::ofstream f(tmp, std::ios::out | std::ios::trunc);
	if (!f.good())
		return false;

	f << "# Simple Arcades Screensaver Allowlist\n";
	f << "# Lines: <relative/path> enabled, or '# <relative/path>' disabled\n";

	for (const auto& rel : allRels)
	{
		auto it = sel.find(rel);
		bool enabled = (it != sel.end()) ? it->second : true;

		if (enabled)
			f << rel << "\n";
		else
			f << "# " << rel << "\n";
	}

	f.close();
	if (!f.good())
		return false;

	// Replace
	Utils::FileSystem::removeFile(cfgPath);
	return (std::rename(tmp.c_str(), cfgPath.c_str()) == 0);
}

bool SimpleArcadesScreensaverUtil::syncSelection(std::vector<std::string>& allRel,
	std::unordered_map<std::string, bool>& enabledByRel)
{
	const std::string root     = getRootDir();
	const std::string cfgRead  = getConfigReadPath();
	const std::string cfgWrite = getConfigPath();

	LOG(LogDebug) << "[SA] Screensaver allowlist read:  " << cfgRead;
	LOG(LogDebug) << "[SA] Screensaver allowlist write: " << cfgWrite;
	LOG(LogDebug) << "[SA] Screensaver root: " << root;

	allRel = scanAllVideosRelative(root);
	enabledByRel = loadSelectionFile(cfgRead);

	// Fail-safe:
	// If the allowlist FILE EXISTS but we parsed ZERO VALID entries, do NOT default-enable everything.
	// Treat as all disabled to avoid an unintended re-enable.
	if (Utils::FileSystem::exists(cfgRead) && enabledByRel.empty())
	{
		std::ifstream sizeCheck(cfgRead, std::ios::binary | std::ios::ate);
		const std::streamoff sz = sizeCheck.good() ? static_cast<std::streamoff>(sizeCheck.tellg()) : 0;

		if (sz > 0)
		{
			LOG(LogError) << "[SA] Allowlist exists but parsed 0 valid entries. "
				<< "Fail-safe: treating ALL as disabled. Path: " << cfgRead;

			enabledByRel.clear();
			enabledByRel.reserve(allRel.size());
			for (const auto& rel : allRel)
				enabledByRel[rel] = false;

			return true;
		}
	}

	std::set<std::string> allSet(allRel.begin(), allRel.end());

	// Backward-compat: if allowlist contains bare filenames, map to generic_screensavers/<name>
	for (auto it = enabledByRel.begin(); it != enabledByRel.end(); )
	{
		if (it->first.find('/') == std::string::npos)
		{
			std::string alt = std::string("generic_screensavers/") + it->first;
			if (allSet.find(alt) != allSet.end())
			{
				bool state = it->second;
				it = enabledByRel.erase(it);
				enabledByRel[alt] = state;
				continue;
			}
		}
		++it;
	}

	// Drop stale entries
	for (auto it = enabledByRel.begin(); it != enabledByRel.end(); )
	{
		if (allSet.find(it->first) == allSet.end())
			it = enabledByRel.erase(it);
		else
			++it;
	}

	// Add new files as enabled by default
	for (const auto& rel : allRel)
	{
		if (enabledByRel.find(rel) == enabledByRel.end())
			enabledByRel[rel] = true;
	}

	// IMPORTANT:
	// syncSelection() must NOT write to disk.
	// Only explicit user save/exit from the Gallery should write allowlist.cfg.
	// Summary
	int enabledCount = 0;
	for (const auto& kv : enabledByRel)
		if (kv.second) enabledCount++;
	LOG(LogDebug) << "[SA] Allowlist entries after sync: " << enabledByRel.size() << " (enabled: " << enabledCount << ", discovered: " << allRel.size() << ")";
	return true;
}

bool SimpleArcadesScreensaverUtil::writeSelection(const std::vector<std::string>& allRel,
	const std::unordered_map<std::string, bool>& enabledByRel)
{
	// Ensure stable ordering
	std::vector<std::string> sorted = allRel;
	std::sort(sorted.begin(), sorted.end());
	sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

	return writeSelectionFile(getConfigPath(), sorted, enabledByRel);
}

std::vector<std::string> SimpleArcadesScreensaverUtil::syncAndGetEnabledVideos()
{
	const std::string root = getRootDir();

	std::vector<std::string> allRels;
	std::unordered_map<std::string, bool> sel;

	if (!syncSelection(allRels, sel))
		return {};

	// Build enabled abs list
	std::vector<std::string> enabledAbs;
	for (const auto& rel : allRels)
	{
		auto it = sel.find(rel);
		if (it != sel.end() && it->second)
			enabledAbs.push_back(joinPath(root, rel));
	}
	return enabledAbs;
}