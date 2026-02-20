#pragma once
#ifndef ES_CORE_AUDIO_MANAGER_H
#define ES_CORE_AUDIO_MANAGER_H

#include <SDL_audio.h>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <map>

class Sound;

class AudioManager
{
	static SDL_AudioSpec sAudioFormat;
	static std::vector<std::shared_ptr<Sound>> sSoundVector;
	static std::shared_ptr<AudioManager> sInstance;

	static void mixAudio(void *unused, Uint8 *stream, int len);

	AudioManager();

public:
	static std::shared_ptr<AudioManager> & getInstance();

	void init();
	void deinit();

	void registerSound(std::shared_ptr<Sound> & sound);
	void unregisterSound(std::shared_ptr<Sound> & sound);

	void play();
	void stop();

	virtual ~AudioManager();
};


// Simple Arcades background music (external mpg123 player).
// This is intentionally separate from EmulationStation UI sound effects.

// Track display info for the "Now Playing" popup.
struct TrackDisplayInfo
{
	std::string soundtrack;  // Cleaned soundtrack folder name.
	std::string trackName;   // Cleaned track file name.
	std::string coverPath;   // Absolute path to cover art (or fallback).
	bool valid;              // True if there is new info to consume.

	TrackDisplayInfo() : valid(false) {}
};

// Radio station entry loaded from radio_stations.cfg.
struct RadioStation
{
	std::string name;
	std::string url;
};

class SimpleArcadesMusicManager
{
public:
	static SimpleArcadesMusicManager& getInstance();

	// Safe to call multiple times.
	void init();
	void shutdown();

	// Runtime controls.
	void setEnabled(bool enabled);
	bool isEnabled() const;

	void setVolumePercent(int percent); // 0-100
	int  getVolumePercent() const;

	// mode: "shuffle_all", "folder", "radio", or "spotify"
	void setMode(const std::string& mode);
	std::string getMode() const;

	// Only used when mode == "folder"
	void setFolder(const std::string& folderName);
	std::string getFolder() const;

	// Folder names under ~/simplearcades/media/music/soundtracks
	std::vector<std::string> getAvailableFolders() const;

	// --- Internet Radio ---

	void loadRadioStations();
	std::vector<RadioStation> getRadioStations() const;
	int  getRadioStationIndex() const;
	std::string getRadioStationName() const;
	void setRadioStation(int index);

	// --- Spotify Connect ---

	static bool isSpotifyAvailable();
	void startSpotifyService();
	void stopSpotifyService();
	void pauseSpotifyService();
	void resumeSpotifyService();

	// Track controls.
	void nextTrack();
	void prevTrack();

	// Called by the game launcher hooks.
	void onGameLaunched();
	void onGameReturned();
	void startGameplayMusic();  // call after launch video, before game starts
	void stopGameplayMusic();   // call after game exits, before exit video

	// --- Music v2 additions ---

	// Screensaver hooks: pause/resume music via SIGSTOP/SIGCONT.
	void onScreenSaverStarted();
	void onScreenSaverStopped();

	// Play music during screensaver toggle.
	void setPlayDuringScreensaver(bool play);
	bool getPlayDuringScreensaver() const;

	// Show track popup toggle.
	void setShowTrackPopup(bool show);
	bool getShowTrackPopup() const;

	// Returns track display info if a new track has started since the last
	// call; info.valid == false otherwise. Clears the flag.
	TrackDisplayInfo consumeNewTrackInfo();

	// Rescan soundtrack folders and sync shuffle allowlist.
	// Returns true if successful.
	bool rescanMusic();

	// Public shuffle allowlist access for the track-toggle settings UI.
	// Returns a list of (relative_path, enabled) pairs, sorted alphabetically.
	std::vector<std::pair<std::string, bool>> getShuffleAllowlist();

	// Enable or disable a single track in the shuffle allowlist.
	void setTrackEnabled(const std::string& relPath, bool enabled);

	// Persist the shuffle allowlist to disk (without rebuilding playlist).
	void saveShuffleAllowlist();

	// Returns the number of MP3 tracks in a specific soundtrack folder.
	int getTrackCount(const std::string& folderName) const;

	// Returns the path to cover art for a soundtrack, or empty if not found.
	std::string getCoverArtPath(const std::string& folderName) const;

	// --- Music v3: Gameplay volume ---

	void setPlayDuringGameplay(bool play);
	bool getPlayDuringGameplay() const;

	void setGameplayVolume(int percent); // 10-100
	int  getGameplayVolume() const;

	// --- End Music v3 additions ---

	// Persist current config to ~/simplearcades/config/music/music.cfg
	void saveConfig();
	void loadConfig();

private:
	SimpleArcadesMusicManager();
	~SimpleArcadesMusicManager();
	SimpleArcadesMusicManager(const SimpleArcadesMusicManager&) = delete;
	SimpleArcadesMusicManager& operator=(const SimpleArcadesMusicManager&) = delete;

	void threadMain();
	void rebuildPlaylistLocked();

	// Shuffle allowlist helpers (caller must hold mMutex).
	void loadShuffleAllowlistLocked();
	void saveShuffleAllowlistLocked();
	void syncShuffleAllowlistLocked(const std::vector<std::string>& allTracks);

	// Radio station helpers (caller must hold mMutex).
	void loadRadioStationsLocked();

	mutable std::mutex              mMutex;
	std::condition_variable         mCv;
	bool                            mInit;
	bool                            mStopThread;

	bool                            mEnabled;
	int                             mVolumePercent;
	std::string                     mMode;
	std::string                     mFolder;

	bool                            mPausedForGame;
	bool                            mInGameplay;  // true between onGameLaunched/onGameReturned
	bool                            mPausedForScreensaver;
	bool                            mPlayDuringScreensaver;
	bool                            mShowTrackPopup;
	bool                            mRebuildRequested;
	bool                            mRestartRequested;
	int                             mAdvanceRequested; // -1, 0, +1

	std::vector<std::string>        mPlaylist;
	int                             mIndex;
	int                             mPid; // child pid, or -1
	bool                            mIsRadioProcess; // true if mPid is streaming radio

	// Track popup: set by worker thread, consumed by UI poll.
	bool                            mNewTrackFlag;
	std::string                     mNewTrackSoundtrack;
	std::string                     mNewTrackName;
	std::string                     mNewTrackCoverPath;

	// Shuffle allowlist: relative path -> enabled.
	std::map<std::string, bool>     mShuffleAllowlist;

	// Radio stations.
	std::vector<RadioStation>       mRadioStations;
	int                             mRadioIndex;

	// Gameplay volume.
	bool                            mPlayDuringGameplay;
	int                             mGameplayVolume;

	std::thread                     mThread;
};


#endif // ES_CORE_AUDIO_MANAGER_H