#pragma once
#ifndef ES_CORE_SA_STYLE_H
#define ES_CORE_SA_STYLE_H

// ============================================================================
//  Simple Arcades -- Central Style Sheet
//
//  Edit ONLY this file to change the look of every menu in EmulationStation.
//  All colors are in RRGGBBAA hex format (AA = alpha/transparency).
//  After changing values, recompile and the new style applies everywhere.
// ============================================================================

// --- CUSTOM FONT ---
// Set to "" (empty string) to use the built-in EmulationStation font.
#define SA_FONT_PATH  "/home/pi/simplearcades/media/fonts/TinyUnicode.ttf"

// --- MENU TITLE ---
// The large heading at the top of each menu (e.g. "MUSIC SETTINGS").
#define SA_TITLE_COLOR        0x00D8FFFF

// --- MENU TEXT ---
// Primary text color for menu labels, option values, action rows, etc.
#define SA_TEXT_COLOR          0xD7D7D7FF

// --- SUBTITLE / DIM TEXT ---
// Used for secondary information: "HOLD ANY BUTTON TO SKIP", device info, etc.
#define SA_SUBTITLE_COLOR     0x9F9F9FFF

// --- GAME NAME / BRIGHT TEXT ---
// Used in gamelist views for the selected game name.
#define SA_GAMENAME_COLOR     0xD7D7D7FF

// --- SCRAPER SUBTITLE ---
// Used in scraper multi-progress screen.
#define SA_SCRAPER_SUBTITLE_COLOR 0x00D8FFFF

// --- BUTTON TEXT ---
// Focused (highlighted) and unfocused button text colors.
#define SA_BUTTON_TEXT_FOCUSED   0x1C1C1CFF
#define SA_BUTTON_TEXT_UNFOCUSED 0x1C1C1CFF

// --- INPUT CONFIG ---
// Colors used in the controller configuration screens.
#define SA_INPUT_ICON_COLOR     0xD7D7D7FF
#define SA_INPUT_MAPPED_COLOR   0x00D8FFFF

// --- SELECTOR BAR (highlighted row) ---
// SA_SELECTOR_COLOR controls the brightness of the highlight.
// SA_SELECTOR_EDGE_COLOR is the thin 2px border on left/right edges.
#define SA_SELECTOR_COLOR     0x1C1C1CFF
#define SA_SELECTOR_EDGE_COLOR 0x00D8FFFF

// --- ROW SEPARATORS ---
// Thin horizontal lines between menu rows.
#define SA_SEPARATOR_COLOR    0x000000FF

// --- SLIDER ---
// The horizontal line and value text for slider controls (e.g. volume).
#define SA_SLIDER_LINE_COLOR  0xD7D7D7FF
#define SA_SLIDER_TEXT_COLOR  0xD7D7D7FF

// --- HELP BAR ---
// The icon and text colors for the help prompts bar at the bottom of screen.
#define SA_HELP_ICON_COLOR    0x00D8FFFF
#define SA_HELP_TEXT_COLOR    0x00D8FFFF

// --- INFO POPUP ---
// The small toast popup (e.g. "Music rescanned!").
#define SA_POPUP_TEXT_COLOR   0xD7D7D7FF

// --- MUSIC POPUP (Now Playing) ---
#define SA_MUSIC_LABEL_COLOR  0x00D8FFFF
#define SA_MUSIC_TEXT_COLOR   0xD7D7D7FF
#define SA_MUSIC_BG_COLOR     0x000000FF

// --- SECTION HEADER TEXT ---
// Bright text used for section/folder headers in submenus.
#define SA_SECTION_HEADER_COLOR 0x00D8FFFF

// --- VERSION TEXT ---
// The version string at the bottom of the main menu.
#define SA_VERSION_COLOR      0x222222FF

// --- LOADING SCREEN ---
// Shown during long operations (e.g. music rescan, shuffle settings load).
#define SA_LOADING_BG_COLOR   0x1C1C1C
#define SA_LOADING_BAR_COLOR  0x00D8FF
#define SA_LOADING_TEXT_COLOR 0xD7D7D7FF

// --- RESTART REASON / BOOT IMAGES ---
// When ES restarts for a specific reason (game save, music change, etc.),
// the reason keyword is written to this file before exit. On next boot,
// ES reads it, shows the corresponding image and text, then deletes it.
#define SA_RESTART_REASON_PATH "/home/pi/.restart_reason"
#define SA_BOOT_IMAGES_PATH    "/home/pi/simplearcades/media/images/boot_images/"
#define SA_BOOT_DEFAULT_IMAGE  "boot_splash.png"

// --- GAME LAUNCH VIDEO CONFIG ---
// Config file storing launch video preferences (on/off, mode, mute).
#define SA_LAUNCH_VIDEO_CONFIG "/home/pi/simplearcades/config/videos/game_launch.cfg"
#define SA_LAUNCH_VIDEO_BASE   "/home/pi/simplearcades/media/videos/game_start/"

// --- GAME EXIT VIDEO CONFIG ---
#define SA_EXIT_VIDEO_CONFIG   "/home/pi/simplearcades/config/videos/game_exit.cfg"

// ============================================================================
//  Helper: SA font accessor (performance-optimized)
//
//  Use saFont(size) anywhere you would normally use Font::get(size).
//  If SA_FONT_PATH is set and the file exists, it uses the custom font.
//  Otherwise falls back to the built-in default.
//
//  Fonts are cached per size so the filesystem is only hit ONCE per size,
//  not every frame. This is critical for smooth scrolling on the Pi.
//
//  Example:  auto font = saFont(FONT_SIZE_MEDIUM);
// ============================================================================

#include "resources/Font.h"
#include "utils/FileSystemUtil.h"
#include <string>
#include <unordered_map>

inline std::shared_ptr<Font> saFont(int size)
{
	static std::unordered_map<int, std::shared_ptr<Font>> cache;
	auto it = cache.find(size);
	if (it != cache.end())
		return it->second;

	static const bool useCustom = []() {
		const std::string p = SA_FONT_PATH;
		return !p.empty() && Utils::FileSystem::exists(p);
	}();

	auto font = useCustom ? Font::get(size, SA_FONT_PATH) : Font::get(size);
	cache[size] = font;
	return font;
}

inline std::shared_ptr<Font> saFontLight(int size)
{
	static std::unordered_map<int, std::shared_ptr<Font>> cache;
	auto it = cache.find(size);
	if (it != cache.end())
		return it->second;

	static const bool useCustom = []() {
		const std::string p = SA_FONT_PATH;
		return !p.empty() && Utils::FileSystem::exists(p);
	}();

	auto font = useCustom ? Font::get(size, SA_FONT_PATH) : Font::get(size, FONT_PATH_LIGHT);
	cache[size] = font;
	return font;
}

#endif // ES_CORE_SA_STYLE_H