# EmulationStationSA - Simple Arcades Modification Changelog

All modifications made to the EmulationStation fork for Simple Arcades
custom arcade cabinets (RetroPie/RPi4/armv7).

---

## Session 1: SAStyle.h Centralization & Font System

### SAStyle.h - Centralized Style Constants
- Created `es-core/src/SAStyle.h` with 22 design constants
- Centralized all colors, font sizes, menu dimensions across 41 files
- `saFont()` / `saFontLight()` functions cache Font shared_ptr per size
- CPU usage reduced from 39% to 35% via font caching

### Files modified:
- `es-core/src/SAStyle.h` (new)
- 41 files across es-app and es-core updated to use SA constants

---

## Session 2: Screensaver, Shuffle, Music Popup, Help/QR Viewer

### Screensaver Enhancements
- Pause on trigger-only skip
- Track popup display
- Rescan support
- Shuffle allowlist and shuffle settings submenu

### GuiMusicPopup
- Music popup with album art display

### Help/QR Viewer
- In-app help system with QR code viewer

---

## Session 3: Gamelist Scrolling Bug Fix

### TextListComponent.h
- Viewport safety clamp to prevent scroll overflow

### VideoGameListView
- Media debounce (150ms) to prevent rapid texture loading
- OMX video overlay glitch fixed with 300ms onShow() delay

---

## Session 4: Startup Video & Restart Reason System

### Restart Reason Detection (`Window.h` / `Window.cpp`)
- `~/.restart_reason` file system for context-aware restarts
- Reason types: game_save, music_update, screensaver_change, theme_change
- Custom PNG boot images per reason (1920px wide)
- Lazy splash image initialization (GPU-safe, no black screen)
- Phase-aware loading text via `getRestartText()`:
  - System load phase: "NEW GAME SAVED!"
  - Preload phase (progress bar): "REBUILDING GAME LIST..."
  - Finished phase: "HERE WE GO! :)"
- Boot image caching for 2-3x faster loading screens

### Boot image paths:
- `/home/pi/simplearcades/media/images/boot_images/boot_splash.png`
- `/home/pi/simplearcades/media/images/boot_images/game_saved.png`
- `/home/pi/simplearcades/media/images/boot_images/music_update.png`

### Game Launch Video Settings (`GuiMenu.cpp`)
- Kiosk-accessible menu for game launch video configuration
- Enable/disable, mode selection (random tips, standard, controller layout)
- Mute toggle
- Settings stored in `/home/pi/simplearcades/config/videos/game_launch.cfg`

---

## Session 5: Restart Mechanism & Platform Fixes

### main.cpp - Restart Polling
- `pendingRestart` global flag check in main loop (before isSleeping)
- Backup file-based check for `/tmp/es-restart`
- Eliminates old F4/uinput restart mechanism entirely

### platform.h / platform.cpp - Restart Flag System
- Added `extern bool pendingRestart` global flag
- `runSystemCommand()` checks for `/tmp/es-restart` after system() returns
- Sets `pendingRestart = true` immediately (cannot be flushed like SDL events)
- Main loop detects flag on next iteration, exits cleanly
- **Removed direct `sudo reboot` / `sudo shutdown`** from processQuitMode()
- REBOOT/SHUTDOWN now only touch flag files (`/tmp/es-sysrestart`, `/tmp/es-shutdown`)
- Shell wrapper (`emulationstation.sh`) handles video playback then system commands

### Restart Flow (complete pipeline):
1. Game running → watcher detects save state → sets flag file to "1"
2. Game exits → onend.sh reads flag → writes `~/.restart_reason` + touches `/tmp/es-restart`
3. `runSystemCommand()` returns → detects `/tmp/es-restart` → sets `pendingRestart = true`
4. Main loop reads `pendingRestart` → sets `running = false` → ES exits cleanly
5. Shell wrapper detects `/tmp/es-restart` → restarts ES
6. New ES reads `~/.restart_reason` → shows custom boot image + phased text

### Reboot/Shutdown Video Flow:
1. User selects Reboot/Shutdown from ES menu
2. `quitES()` pushes SDL_QUIT → `processQuitMode()` touches flag file
3. ES exits → shell wrapper detects flag → plays transition video → executes system command

---

## Files Modified (cumulative)

### es-core/src/
- `SAStyle.h` - Style constants, font caching (new file)
- `Window.h` - Restart reason members, splash image, boot image path
- `Window.cpp` - Restart reason detection, lazy splash init, phase-aware text, boot image caching
- `platform.h` - pendingRestart declaration, QuitMode enum
- `platform.cpp` - pendingRestart flag, runSystemCommand restart detection, removed direct reboot/shutdown

### es-app/src/
- `main.cpp` - pendingRestart polling in main loop
- `GuiMenu.cpp` - Game launch video settings menu
- `views/ViewController.cpp` - getRestartText() integration for loading screens

### Shell Scripts (deployed on Pi, not in this repo):
- `emulationstation.sh` - Wrapper with video playback for reboot/shutdown/restart
- `onstart.sh` - Controller priority, launch videos, watcher startup
- `onend.sh` - Process cleanup, restart signaling, background orphan cleanup
- `watcher_script.sh` - Save state detection, metadata/gamelist/media pipeline

---

## Architecture Notes

### Why external scripts are still needed:
ES is **suspended** while RetroArch runs. It cannot detect save states during
gameplay. The watcher script runs alongside RetroArch and handles detection.
onstart.sh launches the watcher; onend.sh reads the result and signals ES.

### What moved INTO ES (from external scripts):
- Restart detection (was F4/uinput python script → now pendingRestart flag)
- Boot image/text customization (was nothing → now Window.cpp)
- Reboot/shutdown video coordination (was direct sudo calls → now flag files)

### Flag file locations:
- `/tmp/es-restart` - ES restart requested (cleared by wrapper on restart)
- `/tmp/es-sysrestart` - System reboot requested
- `/tmp/es-shutdown` - System shutdown requested
- `/home/pi/.restart_reason` - Reason string for custom loading screen
- `/home/pi/simplearcades/flags/save_state_flag.flag` - Save detected during gameplay
