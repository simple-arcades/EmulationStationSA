// ============================================================================
//  SaveStateDeleteHelper.h
//
//  Handles deleting save states from the "savestates" system.
//  Two-phase approach:
//    Phase 1: Delete watcher-created files (.state, .entry, .metadata, image, 
//             gamelist entry, and video if no other entries reference it)
//    Phase 2: If that was the last save for a ROM, scan for save-RAM files
//             (.srm, .mcd, etc.) and ask user whether to delete those too
//
//  Called from GuiGamelistOptions when browsing the savestates system.
// ============================================================================
#pragma once
#ifndef ES_APP_SAVE_STATE_DELETE_HELPER_H
#define ES_APP_SAVE_STATE_DELETE_HELPER_H

#include <string>
#include <vector>
#include <map>

class Window;
class FileData;
class SystemData;

class SaveStateDeleteHelper
{
public:
	// -----------------------------------------------------------------------
	//  Metadata parsing
	// -----------------------------------------------------------------------

	// Represents the fields we care about from a .metadata file
	struct MetadataInfo
	{
		std::string core;     // CORE= value
		std::string config;   // CONFIG= value
		std::string romPath;  // ROM= value (full path to original ROM)
		std::string system;   // SYSTEM= value
	};

	// Parse a .metadata file (simple KEY=VALUE format, one per line)
	// Returns true if ROM= field was found (the only required field)
	static bool parseMetadataFile(const std::string& metadataPath, MetadataInfo& outInfo);

	// -----------------------------------------------------------------------
	//  Phase 1: Delete watcher-created files
	// -----------------------------------------------------------------------

	// Delete the .state, .entry, .metadata, and screenshot for a save entry.
	// entryPath is the full path to the .entry file (what ES sees as the "ROM").
	// Returns true if core files were deleted successfully.
	static bool deleteWatcherFiles(const std::string& entryPath);

	// -----------------------------------------------------------------------
	//  Video reference checking
	// -----------------------------------------------------------------------

	// Count how many <game> entries in gamelist.xml reference the given video path.
	// gamelistPath: full path to the savestates gamelist.xml
	// videoPath:    the <video> value to search for
	// excludeGamePath: exclude this game's <path> from the count (the one being deleted)
	static int countVideoReferences(const std::string& gamelistPath,
	                                const std::string& videoPath,
	                                const std::string& excludeGamePath);

	// -----------------------------------------------------------------------
	//  Gamelist XML manipulation
	// -----------------------------------------------------------------------

	// Remove a <game> entry from gamelist.xml whose <path> matches gamePath.
	// Returns true if the entry was found and removed.
	static bool removeGamelistEntry(const std::string& gamelistPath,
	                                const std::string& gamePath);

	// -----------------------------------------------------------------------
	//  Phase 2: Save-RAM file discovery
	// -----------------------------------------------------------------------

	// Given a ROM filename (e.g. "Super Mario World (USA).sfc"), find all
	// matching save-RAM files in the savefiles/ directory.
	// Matches by stripping the ROM extension and globbing:
	//   savefiles/{basename}.*
	//   savefiles/{basename}/*  (subdirectories)
	// Returns a list of full paths to found files.
	static std::vector<std::string> findSaveRamFiles(const std::string& savefilesDir,
	                                                 const std::string& romFilename);

	// -----------------------------------------------------------------------
	//  "Last save" detection
	// -----------------------------------------------------------------------

	// Check whether any other .metadata files in the savestates directory
	// reference the same ROM= path. Returns true if this is the LAST save
	// for that ROM (no other metadata files reference it).
	static bool isLastSaveForRom(const std::string& savestatesDir,
	                             const std::string& romPath,
	                             const std::string& excludeMetadataPath);

	// -----------------------------------------------------------------------
	//  Save entry discovery (for "SAVED GAMES" context menu)
	// -----------------------------------------------------------------------

	// Info about a single save state entry, used by the saved games dialog
	struct SaveEntryInfo
	{
		std::string entryPath;     // Full path to .entry file
		std::string romPath;       // ROM= from .metadata
		std::string system;        // SYSTEM= from .metadata
		std::string displayName;   // Display name from gamelist (e.g. "Super Mario World [US] (Save Slot 1)")
		std::string imagePath;     // Screenshot path from gamelist
		std::string videoPath;     // Video path from gamelist
		std::string description;   // Description from gamelist (contains timestamp)
		int slotNumber;            // Extracted slot number
	};

	// Scan the savestates directory for all saves matching a given ROM path.
	// Reads .metadata files to match by ROM=, then enriches with gamelist data.
	// Returns entries sorted by slot number.
	static std::vector<SaveEntryInfo> findSavesForRom(const std::string& romPath);

	// -----------------------------------------------------------------------
	//  Utility
	// -----------------------------------------------------------------------

	// Extract just the filename from a full path (e.g. "/path/to/game.sfc" -> "game.sfc")
	static std::string getFilename(const std::string& path);

	// Strip extension from a filename (e.g. "game.sfc" -> "game")
	static std::string stripExtension(const std::string& filename);
};

#endif // ES_APP_SAVE_STATE_DELETE_HELPER_H
