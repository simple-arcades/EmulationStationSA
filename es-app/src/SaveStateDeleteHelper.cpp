// ============================================================================
//  SaveStateDeleteHelper.cpp
//
//  Implementation of save state deletion logic.
//  See SaveStateDeleteHelper.h for the full design overview.
//
//  Uses ES's built-in Utils::FileSystem for all file operations —
//  no Boost filesystem dependency needed.
// ============================================================================
#include "SaveStateDeleteHelper.h"
#include "Log.h"
#include "utils/FileSystemUtil.h"

#include <fstream>
#include <sstream>
#include <algorithm>

// pugixml is used by ES for all XML work — it's already linked
#include <pugixml.hpp>

// ============================================================================
//  parseMetadataFile
//
//  The .metadata file is a simple text file with lines like:
//    CORE=mgba_libretro.so
//    CONFIG=/opt/retropie/configs/all/retroarch.cfg
//    ROM=/home/pi/RetroPie/roms/snes/Super Mario World (USA).sfc
//    SYSTEM=snes
//
//  We only strictly require ROM= but parse all fields for completeness.
// ============================================================================
bool SaveStateDeleteHelper::parseMetadataFile(const std::string& metadataPath, MetadataInfo& outInfo)
{
	std::ifstream file(metadataPath);
	if (!file.is_open())
	{
		LOG(LogWarning) << "SaveStateDeleteHelper: Could not open metadata file: " << metadataPath;
		return false;
	}

	bool foundRom = false;
	std::string line;
	while (std::getline(file, line))
	{
		// Skip empty lines
		if (line.empty())
			continue;

		// Find the first '=' to split key=value
		size_t eqPos = line.find('=');
		if (eqPos == std::string::npos)
			continue;

		std::string key = line.substr(0, eqPos);
		std::string value = line.substr(eqPos + 1);

		// Strip surrounding quotes if present (watcher writes ROM="..." with quotes)
		if (value.length() >= 2 && value.front() == '"' && value.back() == '"')
			value = value.substr(1, value.length() - 2);

		if (key == "CORE")
			outInfo.core = value;
		else if (key == "CONFIG")
			outInfo.config = value;
		else if (key == "ROM")
		{
			outInfo.romPath = value;
			foundRom = true;
		}
		else if (key == "SYSTEM")
			outInfo.system = value;
	}

	if (!foundRom)
		LOG(LogWarning) << "SaveStateDeleteHelper: No ROM= field found in: " << metadataPath;

	return foundRom;
}

// ============================================================================
//  deleteWatcherFiles
//
//  Given the .entry file path, derive and delete:
//    - {name}.state{N}         (raw save state — entry path minus ".entry")
//    - {name}.state{N}.entry   (the entry file itself)
//    - {name}.state{N}.metadata
//    - media/images/{name}.state{N}.png  (screenshot)
//
//  We don't delete the video here — that's handled separately after checking
//  whether other gamelist entries still reference it.
// ============================================================================
bool SaveStateDeleteHelper::deleteWatcherFiles(const std::string& entryPath)
{
	bool success = true;

	// The .entry file path looks like:
	//   /home/pi/RetroPie/roms/savestates/GameName.state1.entry
	// The raw state file is the same without ".entry":
	//   /home/pi/RetroPie/roms/savestates/GameName.state1
	// The metadata file adds ".metadata":
	//   /home/pi/RetroPie/roms/savestates/GameName.state1.metadata

	// Derive the base path (without .entry extension)
	std::string basePath;
	const std::string entrySuffix = ".entry";
	if (entryPath.length() > entrySuffix.length() &&
	    entryPath.compare(entryPath.length() - entrySuffix.length(), entrySuffix.length(), entrySuffix) == 0)
	{
		basePath = entryPath.substr(0, entryPath.length() - entrySuffix.length());
	}
	else
	{
		LOG(LogWarning) << "SaveStateDeleteHelper: Entry path doesn't end with .entry: " << entryPath;
		basePath = entryPath; // try anyway
	}

	// Build all file paths to delete
	std::string statePath    = basePath;                     // .state{N}
	std::string metadataPath = basePath + ".metadata";       // .state{N}.metadata

	// Screenshot: in media/images/ directory, named {basename}.png
	// The savestates dir is the parent of the entry file
	std::string savestatesDir = Utils::FileSystem::getParent(entryPath);
	std::string entryFilename = Utils::FileSystem::getFileName(entryPath);

	// Screenshot filename: replace ".entry" with ".png"
	// e.g. "GameName.state1.entry" -> "GameName.state1.png"
	std::string screenshotFilename = entryFilename;
	if (screenshotFilename.length() > entrySuffix.length())
		screenshotFilename = screenshotFilename.substr(0, screenshotFilename.length() - entrySuffix.length()) + ".png";

	std::string screenshotPath = savestatesDir + "/media/images/" + screenshotFilename;

	// Delete each file, logging but not failing if individual files are missing
	// (user might have manually deleted some already)
	auto tryDelete = [&success](const std::string& path, const std::string& description) {
		if (Utils::FileSystem::exists(path))
		{
			if (Utils::FileSystem::removeFile(path))
			{
				LOG(LogInfo) << "SaveStateDeleteHelper: Deleted " << description << ": " << path;
			}
			else
			{
				LOG(LogError) << "SaveStateDeleteHelper: Failed to delete " << description << ": " << path;
				success = false;
			}
		}
		else
		{
			LOG(LogDebug) << "SaveStateDeleteHelper: " << description << " not found (already gone?): " << path;
		}
	};

	tryDelete(entryPath, "entry file");
	tryDelete(statePath, "raw state file");
	tryDelete(metadataPath, "metadata file");
	tryDelete(screenshotPath, "screenshot");

	return success;
}

// ============================================================================
//  countVideoReferences
//
//  Walk the gamelist.xml and count how many <game> entries have a <video>
//  child whose text matches the given videoPath. Exclude the game entry
//  we're about to delete (matched by <path>).
//
//  This tells us whether it's safe to delete the video file:
//    count == 0  ->  safe to delete (this was the last reference)
//    count > 0   ->  other saves still use this video, leave it alone
// ============================================================================
int SaveStateDeleteHelper::countVideoReferences(const std::string& gamelistPath,
                                                const std::string& videoPath,
                                                const std::string& excludeGamePath)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(gamelistPath.c_str());
	if (!result)
	{
		LOG(LogWarning) << "SaveStateDeleteHelper: Could not parse gamelist: " << gamelistPath;
		return -1; // error — caller should NOT delete the video
	}

	int count = 0;
	for (pugi::xml_node game = doc.child("gameList").child("game"); game; game = game.next_sibling("game"))
	{
		std::string gamePath = game.child("path").text().as_string();
		std::string gameVideo = game.child("video").text().as_string();

		// Skip the entry we're about to delete
		if (gamePath == excludeGamePath)
			continue;

		// Check if this entry references the same video
		if (gameVideo == videoPath)
			count++;
	}

	return count;
}

// ============================================================================
//  removeGamelistEntry
//
//  Find the <game> node in gamelist.xml whose <path> matches gamePath,
//  remove it from the XML tree, and save the file back to disk.
// ============================================================================
bool SaveStateDeleteHelper::removeGamelistEntry(const std::string& gamelistPath,
                                                const std::string& gamePath)
{
	pugi::xml_document doc;
	pugi::xml_parse_result result = doc.load_file(gamelistPath.c_str());
	if (!result)
	{
		LOG(LogError) << "SaveStateDeleteHelper: Could not parse gamelist for removal: " << gamelistPath;
		return false;
	}

	pugi::xml_node gameList = doc.child("gameList");
	bool found = false;

	for (pugi::xml_node game = gameList.child("game"); game; game = game.next_sibling("game"))
	{
		if (std::string(game.child("path").text().as_string()) == gamePath)
		{
			gameList.remove_child(game);
			found = true;
			LOG(LogInfo) << "SaveStateDeleteHelper: Removed gamelist entry for: " << gamePath;
			break;
		}
	}

	if (!found)
	{
		LOG(LogWarning) << "SaveStateDeleteHelper: No gamelist entry found for path: " << gamePath;
		return false;
	}

	// Save the modified gamelist back to disk
	if (!doc.save_file(gamelistPath.c_str()))
	{
		LOG(LogError) << "SaveStateDeleteHelper: Failed to save gamelist after removal: " << gamelistPath;
		return false;
	}

	return true;
}

// ============================================================================
//  findSaveRamFiles
//
//  Glob the savefiles/ directory for anything matching the ROM's base name.
//  This catches all core-specific formats without enumerating extensions:
//    .srm, .mcd, .mcd0, .mcd1, .nvmem2, subdirectories, etc.
//
//  romFilename example: "Super Mario World (USA).sfc"
//  We strip the extension -> "Super Mario World (USA)"
//  Then match anything in savefiles/ that starts with that base name
//  followed by '.' or end-of-string.
// ============================================================================
std::vector<std::string> SaveStateDeleteHelper::findSaveRamFiles(const std::string& savefilesDir,
                                                                 const std::string& romFilename)
{
	std::vector<std::string> found;

	if (!Utils::FileSystem::exists(savefilesDir))
	{
		LOG(LogDebug) << "SaveStateDeleteHelper: savefiles dir not found: " << savefilesDir;
		return found;
	}

	// Strip extension from ROM filename to get the base name cores use for save files
	std::string baseName = stripExtension(romFilename);
	if (baseName.empty())
		return found;

	LOG(LogDebug) << "SaveStateDeleteHelper: Scanning savefiles for base name: " << baseName;

	// getDirContent returns a list of full paths for everything in the directory
	// Pass true for recursive so we also catch files inside subdirectories
	Utils::FileSystem::stringList dirContents = Utils::FileSystem::getDirContent(savefilesDir, true);

	for (const auto& itemPath : dirContents)
	{
		std::string itemName = Utils::FileSystem::getFileName(itemPath);

		// Check if this item starts with the base name
		if (itemName.length() >= baseName.length() &&
		    itemName.compare(0, baseName.length(), baseName) == 0)
		{
			// Make sure the match isn't just a prefix of a longer name —
			// the character after the base name should be '.' or end-of-string
			if (itemName.length() == baseName.length() ||
			    itemName[baseName.length()] == '.')
			{
				found.push_back(itemPath);
			}
		}
	}

	LOG(LogInfo) << "SaveStateDeleteHelper: Found " << found.size() << " save-RAM file(s) for: " << baseName;
	return found;
}

// ============================================================================
//  isLastSaveForRom
//
//  Scan all .metadata files in the savestates directory and check if any
//  (other than excludeMetadataPath) reference the same ROM= path.
//
//  Returns true if this is the LAST save — no other metadata files exist
//  for the same ROM.
// ============================================================================
bool SaveStateDeleteHelper::isLastSaveForRom(const std::string& savestatesDir,
                                             const std::string& romPath,
                                             const std::string& excludeMetadataPath)
{
	if (!Utils::FileSystem::exists(savestatesDir))
		return true;

	// Get all files in the savestates directory (non-recursive, just top level)
	Utils::FileSystem::stringList dirContents = Utils::FileSystem::getDirContent(savestatesDir, false);

	for (const auto& itemPath : dirContents)
	{
		std::string filename = Utils::FileSystem::getFileName(itemPath);

		// Only check .metadata files
		const std::string metaSuffix = ".metadata";
		if (filename.length() < metaSuffix.length())
			continue;
		if (filename.compare(filename.length() - metaSuffix.length(), metaSuffix.length(), metaSuffix) != 0)
			continue;

		// Skip the one we're about to delete
		if (itemPath == excludeMetadataPath)
			continue;

		// Parse this metadata file and check if it references the same ROM
		MetadataInfo info;
		if (parseMetadataFile(itemPath, info))
		{
			if (info.romPath == romPath)
			{
				// Found another save for the same ROM — not the last one
				return false;
			}
		}
	}

	// No other metadata files reference this ROM — this is the last save
	return true;
}

// ============================================================================
//  findSavesForRom
//
//  Scans the savestates directory for all .metadata files that reference the
//  given ROM path. For each match, reads the corresponding gamelist entry
//  to get display name, screenshot, description, etc.
//  Returns a vector of SaveEntryInfo sorted by slot number.
// ============================================================================
std::vector<SaveStateDeleteHelper::SaveEntryInfo> SaveStateDeleteHelper::findSavesForRom(const std::string& romPath)
{
	std::vector<SaveEntryInfo> results;
	const std::string savestatesDir = "/home/pi/RetroPie/roms/savestates";
	const std::string gamelistPath = savestatesDir + "/gamelist.xml";

	if (!Utils::FileSystem::exists(savestatesDir))
		return results;

	// Phase 1: Scan .metadata files to find matching saves
	Utils::FileSystem::stringList dirContents = Utils::FileSystem::getDirContent(savestatesDir, false);

	struct MetaMatch {
		std::string entryPath;
		std::string system;
		int slotNumber;
	};
	std::vector<MetaMatch> matches;

	for (const auto& itemPath : dirContents)
	{
		std::string filename = Utils::FileSystem::getFileName(itemPath);

		// Only check .metadata files
		const std::string metaSuffix = ".metadata";
		if (filename.length() < metaSuffix.length())
			continue;
		if (filename.compare(filename.length() - metaSuffix.length(), metaSuffix.length(), metaSuffix) != 0)
			continue;

		MetadataInfo info;
		if (parseMetadataFile(itemPath, info) && info.romPath == romPath)
		{
			// Derive entry path: replace .metadata with .entry
			std::string entryPath = itemPath.substr(0, itemPath.length() - metaSuffix.length()) + ".entry";

			// Extract slot number from filename: "Game Name.state1.metadata" -> 1
			int slot = 0;
			std::string baseName = itemPath.substr(0, itemPath.length() - metaSuffix.length());
			size_t statePos = baseName.rfind(".state");
			if (statePos != std::string::npos)
			{
				std::string slotStr = baseName.substr(statePos + 6); // after ".state"
				try { slot = std::stoi(slotStr); } catch (...) { slot = 0; }
			}

			MetaMatch m;
			m.entryPath = entryPath;
			m.system = info.system;
			m.slotNumber = slot;
			matches.push_back(m);
		}
	}

	if (matches.empty())
		return results;

	// Phase 2: Read gamelist.xml once and enrich all matches
	pugi::xml_document doc;
	bool haveGamelist = doc.load_file(gamelistPath.c_str()).status == pugi::status_ok;

	for (const auto& match : matches)
	{
		SaveEntryInfo entry;
		entry.entryPath = match.entryPath;
		entry.romPath = romPath;
		entry.system = match.system;
		entry.slotNumber = match.slotNumber;

		// Default display name
		std::string entryFilename = Utils::FileSystem::getFileName(match.entryPath);
		entry.displayName = "Save Slot " + std::to_string(match.slotNumber);

		// Try to get richer info from gamelist
		if (haveGamelist)
		{
			std::string relPath = "./" + entryFilename;
			for (pugi::xml_node game = doc.child("gameList").child("game"); game; game = game.next_sibling("game"))
			{
				if (std::string(game.child("path").text().as_string()) == relPath)
				{
					std::string name = game.child("name").text().as_string();
					if (!name.empty()) entry.displayName = name;

					entry.description = game.child("desc").text().as_string();

					std::string imgRel = game.child("image").text().as_string();
					if (!imgRel.empty())
					{
						if (imgRel.length() > 2 && imgRel.substr(0, 2) == "./")
							entry.imagePath = savestatesDir + "/" + imgRel.substr(2);
						else
							entry.imagePath = savestatesDir + "/" + imgRel;
					}

					std::string vidRel = game.child("video").text().as_string();
					if (!vidRel.empty())
					{
						if (vidRel.length() > 2 && vidRel.substr(0, 2) == "./")
							entry.videoPath = savestatesDir + "/" + vidRel.substr(2);
						else
							entry.videoPath = savestatesDir + "/" + vidRel;
					}
					break;
				}
			}
		}

		results.push_back(entry);
	}

	// Sort by slot number
	std::sort(results.begin(), results.end(),
		[](const SaveEntryInfo& a, const SaveEntryInfo& b) { return a.slotNumber < b.slotNumber; });

	return results;
}

// ============================================================================
//  Utility functions
// ============================================================================

std::string SaveStateDeleteHelper::getFilename(const std::string& path)
{
	return Utils::FileSystem::getFileName(path);
}

std::string SaveStateDeleteHelper::stripExtension(const std::string& filename)
{
	return Utils::FileSystem::getStem(filename);
}
