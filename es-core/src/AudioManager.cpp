#include "AudioManager.h"

#include "Log.h"
#include "utils/FileSystemUtil.h"
#include "utils/StringUtil.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>

#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "Settings.h"
#include "Sound.h"
#include <SDL.h>

std::vector<std::shared_ptr<Sound>> AudioManager::sSoundVector;
SDL_AudioSpec AudioManager::sAudioFormat;
std::shared_ptr<AudioManager> AudioManager::sInstance;


void AudioManager::mixAudio(void* /*unused*/, Uint8 *stream, int len)
{
	bool stillPlaying = false;

	//initialize the buffer to "silence"
	SDL_memset(stream, 0, len);

	//iterate through all our samples
	std::vector<std::shared_ptr<Sound>>::const_iterator soundIt = sSoundVector.cbegin();
	while (soundIt != sSoundVector.cend())
	{
		std::shared_ptr<Sound> sound = *soundIt;
		if(sound->isPlaying())
		{
			//calculate rest length of current sample
			Uint32 restLength = (sound->getLength() - sound->getPosition());
			if (restLength > (Uint32)len) {
				//if stream length is smaller than smaple lenght, clip it
				restLength = len;
			}
			//mix sample into stream
			SDL_MixAudio(stream, &(sound->getData()[sound->getPosition()]), restLength, SDL_MIX_MAXVOLUME);
			if (sound->getPosition() + restLength < sound->getLength())
			{
				//sample hasn't ended yet
				stillPlaying = true;
			}
			//set new sound position. if this is at or beyond the end of the sample, it will stop automatically
			sound->setPosition(sound->getPosition() + restLength);
		}
		//advance to next sound
		++soundIt;
	}

	//we have processed all samples. check if some will still be playing
	if (!stillPlaying) {
		//no. pause audio till a Sound::play() wakes us up
		SDL_PauseAudio(1);
	}
}

AudioManager::AudioManager()
{
	init();
}

AudioManager::~AudioManager()
{
	deinit();
}

std::shared_ptr<AudioManager> & AudioManager::getInstance()
{
	//check if an AudioManager instance is already created, if not create one
	if (sInstance == nullptr && Settings::getInstance()->getBool("EnableSounds")) {
		sInstance = std::shared_ptr<AudioManager>(new AudioManager);
	}
	return sInstance;
}

void AudioManager::init()
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0)
	{
		LOG(LogError) << "Error initializing SDL audio!\n" << SDL_GetError();
		return;
	}

	//stop playing all Sounds
	for(unsigned int i = 0; i < sSoundVector.size(); i++)
	{
		if(sSoundVector.at(i)->isPlaying())
		{
			sSoundVector[i]->stop();
		}
	}

	//Set up format and callback. Play 16-bit stereo audio at 44.1Khz
	sAudioFormat.freq = 44100;
	sAudioFormat.format = AUDIO_S16;
	sAudioFormat.channels = 2;
	sAudioFormat.samples = 4096;
	sAudioFormat.callback = mixAudio;
	sAudioFormat.userdata = NULL;

	//Open the audio device and pause
	if (SDL_OpenAudio(&sAudioFormat, NULL) < 0) {
		LOG(LogDebug) << "AudioManager: SDL audio unavailable (expected during game launch): " << SDL_GetError();
	}
}

void AudioManager::deinit()
{
	//stop all playback
	stop();
	//completely tear down SDL audio. else SDL hogs audio resources and emulators might fail to start...
	SDL_CloseAudio();
	SDL_QuitSubSystem(SDL_INIT_AUDIO);
	sInstance = NULL;
}

void AudioManager::registerSound(std::shared_ptr<Sound> & sound)
{
	getInstance();
	sSoundVector.push_back(sound);
}

void AudioManager::unregisterSound(std::shared_ptr<Sound> & sound)
{
	getInstance();
	for(unsigned int i = 0; i < sSoundVector.size(); i++)
	{
		if(sSoundVector.at(i) == sound)
		{
			sSoundVector[i]->stop();
			sSoundVector.erase(sSoundVector.cbegin() + i);
			return;
		}
	}
	LOG(LogError) << "AudioManager Error - tried to unregister a sound that wasn't registered!";
}

void AudioManager::play()
{
	getInstance();

	//unpause audio, the mixer will figure out if samples need to be played...
	SDL_PauseAudio(0);
}

void AudioManager::stop()
{
	//stop playing all Sounds
	for(unsigned int i = 0; i < sSoundVector.size(); i++)
	{
		if(sSoundVector.at(i)->isPlaying())
		{
			sSoundVector[i]->stop();
		}
	}
	//pause audio
	SDL_PauseAudio(1);
}


// ------------------------------------------------------------------------------------------------
// SimpleArcadesMusicManager (external mpg123 player)
// ------------------------------------------------------------------------------------------------

namespace
{
	static std::string saGetHome()
	{
		const std::string home = Utils::FileSystem::getHomePath();
		return home.empty() ? std::string("/home/pi") : home;
	}

	static std::string saMusicConfigPath()
	{
		return saGetHome() + "/simplearcades/config/music/music.cfg";
	}

	static std::string saMusicRootDir()
	{
		return saGetHome() + "/simplearcades/media/music/soundtracks";
	}

	static std::string saShuffleAllowlistPath()
	{
		return saGetHome() + "/simplearcades/config/music/shuffle_allowlist.cfg";
	}

	// ---- Music v3: Radio + Spotify helpers ----

	static std::string saRadioStationsPath()
	{
		return saGetHome() + "/simplearcades/config/music/radio_stations.cfg";
	}

	static std::string saRadioImageDir()
	{
		return saGetHome() + "/simplearcades/media/music/images/radio";
	}

	static std::string saFindRadioCoverArt(const std::string& stationName)
	{
		const std::string dir = saRadioImageDir();
		if (!stationName.empty() && !dir.empty())
		{
			const std::string png = dir + "/" + stationName + ".png";
			if (Utils::FileSystem::exists(png)) return png;
			const std::string jpg = dir + "/" + stationName + ".jpg";
			if (Utils::FileSystem::exists(jpg)) return jpg;
		}
		const std::string fallback = saGetHome() + "/simplearcades/media/music/images/no_art_found.jpg";
		if (Utils::FileSystem::exists(fallback)) return fallback;
		return "";
	}

	static bool saIsMp3File(const std::string& path)
	{
		const std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(path));
		return ext == ".mp3";
	}

	static void saCollectMp3Recursive(const std::string& root, std::vector<std::string>& out)
	{
		if (!Utils::FileSystem::exists(root) || !Utils::FileSystem::isDirectory(root))
			return;

		std::vector<std::string> stack;
		stack.push_back(root);

		while (!stack.empty())
		{
			const std::string dir = stack.back();
			stack.pop_back();

			const auto contents = Utils::FileSystem::getDirContent(dir);
			for (const auto& p : contents)
			{
				if (Utils::FileSystem::isDirectory(p))
				{
					// Skip hidden folders.
					const std::string name = Utils::FileSystem::getFileName(p);
					if (!name.empty() && name[0] == '.')
						continue;

					stack.push_back(p);
				}
				else if (saIsMp3File(p))
				{
					out.push_back(p);
				}
			}
		}
	}

	static std::vector<std::string> saListSoundtrackFolders()
	{
		std::vector<std::string> folders;
		const std::string root = saMusicRootDir();

		if (!Utils::FileSystem::exists(root) || !Utils::FileSystem::isDirectory(root))
			return folders;

		const auto contents = Utils::FileSystem::getDirContent(root);
		for (const auto& p : contents)
		{
			if (!Utils::FileSystem::isDirectory(p))
				continue;

			const std::string name = Utils::FileSystem::getFileName(p);
			if (name.empty() || name[0] == '.')
				continue;

			folders.push_back(name);
		}

		std::sort(folders.begin(), folders.end());
		return folders;
	}

	static int saClamp(int v, int lo, int hi)
	{
		if (v < lo) return lo;
		if (v > hi) return hi;
		return v;
	}

	static void saKillMusicPid(pid_t pid, bool isRadio = false)
	{
		if (pid <= 0)
			return;

		if (isRadio)
		{
			// Radio streams: SIGSTOP first to halt audio output,
			// brief delay for VCHI buffers to drain, then SIGTERM.
			// This avoids the Pi 4 firmware deadlock caused by
			// abruptly closing an active bcm2835_audio VCHI channel.
			kill(pid, SIGSTOP);
			usleep(300000);  // 300ms
			kill(pid, SIGCONT);
			kill(pid, SIGTERM);
		}
		else
		{
			// Local files: simple resume + terminate.
			kill(pid, SIGCONT);
			kill(pid, SIGTERM);
		}
	}

	// Suspend mpg123 instead of killing it.
	static void saSuspendMusicPid(pid_t pid)
	{
		if (pid <= 0)
			return;
		kill(pid, SIGSTOP);
	}

	// Resume a suspended mpg123 process.
	static void saResumeMusicPid(pid_t pid)
	{
		if (pid <= 0)
			return;
		kill(pid, SIGCONT);
	}

	static pid_t saSpawnMpg123(const std::string& filePath, int volumePercent)
	{
		// mpg123 scale factor; default is 32768.
		// We map 0-100% to 0..32768.
		const int vol = saClamp(volumePercent, 0, 100);
		const int scale = static_cast<int>(32768.0 * (static_cast<double>(vol) / 100.0));
		const std::string scaleStr = std::to_string(scale);

		pid_t pid = fork();
		if (pid < 0)
			return -1;

		if (pid == 0)
		{
			// Child: best effort silence stdout/stderr.
			int devnull = ::open("/dev/null", O_WRONLY);
			if (devnull >= 0)
			{
				dup2(devnull, STDOUT_FILENO);
				dup2(devnull, STDERR_FILENO);
			}

			// Exec mpg123.
			char* const args[] = {
				(char*)"mpg123",
				(char*)"-q",
				(char*)"--timeout", (char*)"10",
				(char*)"-f",
				(char*)scaleStr.c_str(),
				(char*)filePath.c_str(),
				nullptr
			};
			execvp(args[0], args);
			_exit(127);
		}

		return pid;
	}

	static std::string saTrim(const std::string& s)
	{
		return Utils::String::trim(s);
	}

	// Make a relative path from an absolute track path, relative to music root.
	// e.g. "/home/pi/.../soundtracks/Classic Rock/song.mp3" -> "Classic Rock/song.mp3"
	static std::string saRelativePath(const std::string& absPath)
	{
		const std::string root = saMusicRootDir() + "/";
		if (absPath.size() > root.size() && absPath.substr(0, root.size()) == root)
			return absPath.substr(root.size());
		return absPath;
	}

	// Title-case a string: capitalize first letter of each word.
	static std::string saTitleCase(const std::string& s)
	{
		std::string result = s;
		bool capNext = true;
		for (size_t i = 0; i < result.size(); ++i)
		{
			if (result[i] == ' ' || result[i] == '\t')
			{
				capNext = true;
			}
			else if (capNext)
			{
				result[i] = static_cast<char>(toupper(static_cast<unsigned char>(result[i])));
				capNext = false;
			}
		}
		return result;
	}

	// Clean a filename stem for display: strip extension, replace underscores/hyphens with spaces,
	// collapse whitespace, title-case.
	static std::string saCleanName(const std::string& raw)
	{
		std::string s = raw;

		// Strip .mp3 extension if present.
		{
			const size_t dot = s.rfind('.');
			if (dot != std::string::npos)
			{
				const std::string ext = Utils::String::toLower(s.substr(dot));
				if (ext == ".mp3")
					s = s.substr(0, dot);
			}
		}

		// Replace underscores and hyphens with spaces.
		for (size_t i = 0; i < s.size(); ++i)
		{
			if (s[i] == '_')
				s[i] = ' ';
		}

		// Collapse multiple spaces.
		std::string collapsed;
		bool lastSpace = false;
		for (char c : s)
		{
			if (c == ' ')
			{
				if (!lastSpace)
					collapsed += c;
				lastSpace = true;
			}
			else
			{
				collapsed += c;
				lastSpace = false;
			}
		}

		// Trim.
		collapsed = saTrim(collapsed);

		// Title case.
		return saTitleCase(collapsed);
	}

	// Format a full track path for popup display (two lines):
	//   Soundtrack
	//   Track Name
	static std::string saFormatTrackDisplay(const std::string& absPath)
	{
		const std::string rel = saRelativePath(absPath);

		// Split into soundtrack folder and track filename.
		const size_t slash = rel.find('/');
		if (slash == std::string::npos || slash == 0)
		{
			// Track is directly in soundtracks/ (no subfolder).
			return saCleanName(rel);
		}

		const std::string soundtrackRaw = rel.substr(0, slash);
		// Get just the filename (last component after all slashes).
		const size_t lastSlash = rel.rfind('/');
		const std::string trackRaw = (lastSlash != std::string::npos)
			? rel.substr(lastSlash + 1)
			: rel;

		const std::string soundtrack = saCleanName(soundtrackRaw);
		const std::string track = saCleanName(trackRaw);

		if (soundtrack.empty())
			return track;

		return soundtrack + "\n" + track;
	}

	// Fallback image when no cover art exists.
	static std::string saNoArtFallbackPath()
	{
		return Utils::FileSystem::getHomePath() + "/simplearcades/media/music/images/no_art_found.jpg";
	}

	// Extract the soundtrack folder name from a relative path.
	// "Classic Rock/song.mp3" -> "Classic Rock"
	static std::string saExtractSoundtrackFolder(const std::string& relPath)
	{
		const size_t slash = relPath.find('/');
		return (slash != std::string::npos) ? relPath.substr(0, slash) : "";
	}

	// Extract just the filename from a relative path.
	// "Classic Rock/song.mp3" -> "song.mp3"
	static std::string saExtractFilename(const std::string& relPath)
	{
		const size_t slash = relPath.rfind('/');
		return (slash != std::string::npos) ? relPath.substr(slash + 1) : relPath;
	}

	// Find cover art for a given track's soundtrack folder.
	// Returns cover.png, cover.jpg, or the no-art fallback.
	static std::string saFindCoverArt(const std::string& absTrackPath)
	{
		const std::string rel = saRelativePath(absTrackPath);
		const std::string folder = saExtractSoundtrackFolder(rel);

		if (!folder.empty())
		{
			const std::string root = saMusicRootDir() + "/" + folder;

			const std::string png = root + "/cover.png";
			if (Utils::FileSystem::exists(png))
				return png;

			const std::string jpg = root + "/cover.jpg";
			if (Utils::FileSystem::exists(jpg))
				return jpg;
		}

		// Fallback.
		const std::string fallback = saNoArtFallbackPath();
		if (Utils::FileSystem::exists(fallback))
			return fallback;

		return "";
	}

} // namespace

SimpleArcadesMusicManager& SimpleArcadesMusicManager::getInstance()
{
	static SimpleArcadesMusicManager inst;
	return inst;
}

SimpleArcadesMusicManager::SimpleArcadesMusicManager()
	: mInit(false)
	, mStopThread(false)
	, mEnabled(false)
	, mVolumePercent(70)
	, mMode("shuffle_all")
	, mFolder("")
	, mPausedForGame(false)
	, mInGameplay(false)
	, mPausedForScreensaver(false)
	, mPlayDuringScreensaver(true)
	, mShowTrackPopup(true)
	, mRebuildRequested(false)
	, mRestartRequested(false)
	, mAdvanceRequested(0)
	, mIndex(0)
	, mPid(-1)
	, mIsRadioProcess(false)
	, mNewTrackFlag(false)
	, mRadioIndex(0)
	, mPlayDuringGameplay(false)
	, mGameplayVolume(50)
{
}

SimpleArcadesMusicManager::~SimpleArcadesMusicManager()
{
	shutdown();
}

void SimpleArcadesMusicManager::init()
{
	// Only protect the init flag with the mutex.
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mInit)
			return;
		mInit = true;
	}

	// Load config (best effort). This function already locks internally.
	loadConfig();

	// Start worker thread + kick playback under lock.
	{
		std::lock_guard<std::mutex> lock(mMutex);

		mStopThread = false;
		mThread = std::thread(&SimpleArcadesMusicManager::threadMain, this);

		// Kick playback if enabled.
		if (mEnabled && !mPausedForGame)
		{
			if (mMode == "spotify")
			{
				// Spotify mode: start the sa-spotify service.
				// Do it outside the lock below.
			}
			else
			{
				mRebuildRequested = true;
				mRestartRequested = false;
				mAdvanceRequested = 0;
				mCv.notify_all();
			}
		}
	}

	// Start spotify service if that's the active mode (outside lock).
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mEnabled && !mPausedForGame && mMode == "spotify")
			startSpotifyService();
	}
}

void SimpleArcadesMusicManager::shutdown()
{
	bool wasSpotify = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mInit)
			return;

		wasSpotify = (mMode == "spotify");
		mStopThread = true;

		if (mPid > 0)
			saKillMusicPid(static_cast<pid_t>(mPid), mIsRadioProcess);

		mCv.notify_all();
	}

	if (mThread.joinable())
		mThread.join();

	// Stop spotify service if it was running.
	if (wasSpotify)
		stopSpotifyService();

	{
		std::lock_guard<std::mutex> lock(mMutex);
		mInit = false;
		mStopThread = false;
		mPid = -1;
	}
}

bool SimpleArcadesMusicManager::isEnabled() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mEnabled;
}

void SimpleArcadesMusicManager::setEnabled(bool enabled)
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;
	bool startSpotify = false;
	bool stopSpotify = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);

		// No-change guard: avoid killing/restarting when value hasn't changed.
		if (mEnabled == enabled)
			return;

		mEnabled = enabled;
		mPausedForGame = false;
		mInGameplay = false;
		mPausedForScreensaver = false;

		if (!mEnabled)
		{
			if (mMode == "spotify")
				stopSpotify = true;
			else if (mPid > 0)
				{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
		}
		else
		{
			if (mMode == "spotify")
				startSpotify = true;
			else
			{
				mRebuildRequested = true;
				mRestartRequested = false;
				mAdvanceRequested = 0;
			}
		}

		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
	if (startSpotify)
		startSpotifyService();
	if (stopSpotify)
		stopSpotifyService();
}

int SimpleArcadesMusicManager::getVolumePercent() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mVolumePercent;
}

void SimpleArcadesMusicManager::setVolumePercent(int percent)
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		const int clamped = saClamp(percent, 0, 100);

		// No-change guard.
		if (mVolumePercent == clamped)
			return;

		mVolumePercent = clamped;

		// Apply immediately by restarting the current track.
		if (mEnabled && !mPausedForGame && !mPausedForScreensaver && mPid > 0)
		{
			mRestartRequested = true;
			pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess;
		}

		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

std::string SimpleArcadesMusicManager::getMode() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mMode;
}

void SimpleArcadesMusicManager::setMode(const std::string& mode)
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;
	pid_t pidToSuspend = -1;
	bool startSpotify = false;
	bool stopSpotify = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		const std::string newMode = (mode == "folder" || mode == "shuffle_all" || mode == "radio" || mode == "spotify") ? mode : std::string("shuffle_all");

		// No-change guard.
		if (mMode == newMode)
			return;

		const std::string oldMode = mMode;
		mMode = newMode;

		if (mEnabled && !mPausedForGame && !mPausedForScreensaver)
		{
			// Stop old mode.
			if (oldMode == "spotify")
				stopSpotify = true;
			else if (mPid > 0)
			{
				if (oldMode == "radio")
					pidToSuspend = static_cast<pid_t>(mPid);  // SIGSTOP: avoid VCHI deadlock
				else
					{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
			}

			// Start new mode.
			if (newMode == "spotify")
				startSpotify = true;
			else
			{
				mRebuildRequested = true;
				mRestartRequested = false;
			}
		}

		mCv.notify_all();
	}

	if (pidToSuspend > 0)
		saKillMusicPid(pidToSuspend, true);
	else if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
	if (stopSpotify)
		stopSpotifyService();
	if (startSpotify)
		startSpotifyService();
}

std::string SimpleArcadesMusicManager::getFolder() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mFolder;
}

void SimpleArcadesMusicManager::setFolder(const std::string& folderName)
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);

		// No-change guard.
		if (mFolder == folderName)
			return;

		mFolder = folderName;

		if (mEnabled && !mPausedForGame && !mPausedForScreensaver)
		{
			mRebuildRequested = true;
			mRestartRequested = false;

			if (mPid > 0)
				{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
		}

		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

std::vector<std::string> SimpleArcadesMusicManager::getAvailableFolders() const
{
	return saListSoundtrackFolders();
}


// ---- Internet Radio ----

void SimpleArcadesMusicManager::loadRadioStations()
{
	std::lock_guard<std::mutex> lock(mMutex);
	loadRadioStationsLocked();
}

std::vector<RadioStation> SimpleArcadesMusicManager::getRadioStations() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mRadioStations;
}

int SimpleArcadesMusicManager::getRadioStationIndex() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mRadioIndex;
}

std::string SimpleArcadesMusicManager::getRadioStationName() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	if (mRadioIndex >= 0 && mRadioIndex < static_cast<int>(mRadioStations.size()))
		return mRadioStations[mRadioIndex].name;
	return "";
}

void SimpleArcadesMusicManager::setRadioStation(int index)
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;
	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (mRadioStations.empty()) return;
		const int clamped = saClamp(index, 0, static_cast<int>(mRadioStations.size()) - 1);
		if (mRadioIndex == clamped) return;
		mRadioIndex = clamped;
		if (mMode == "radio" && mEnabled && !mPausedForGame && !mPausedForScreensaver)
		{
			mRebuildRequested = true;
			if (mPid > 0) { pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
		}
		mCv.notify_all();
	}
	if (pidToKill > 0) saKillMusicPid(pidToKill, killIsRadio);
}

void SimpleArcadesMusicManager::loadRadioStationsLocked()
{
	mRadioStations.clear();
	const std::string path = saRadioStationsPath();
	if (!Utils::FileSystem::exists(path)) return;

	std::ifstream in(path);
	if (!in.is_open()) return;

	std::string line;
	while (std::getline(in, line))
	{
		line = saTrim(line);
		if (line.empty() || line[0] == '#') continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string name = saTrim(line.substr(0, eq));
		std::string url  = saTrim(line.substr(eq + 1));
		if (!name.empty() && !url.empty())
		{
			RadioStation station;
			station.name = name;
			station.url = url;
			mRadioStations.push_back(station);
		}
	}

	LOG(LogInfo) << "SimpleArcadesMusicManager: Loaded " << mRadioStations.size() << " radio station(s).";
	if (!mRadioStations.empty() && mRadioIndex >= static_cast<int>(mRadioStations.size()))
		mRadioIndex = 0;
}


// ---- Spotify ----

bool SimpleArcadesMusicManager::isSpotifyAvailable()
{
	return (std::system("which librespot > /dev/null 2>&1") == 0);
}

void SimpleArcadesMusicManager::startSpotifyService()
{
	std::system("sudo systemctl start sa-spotify 2>/dev/null");
}

void SimpleArcadesMusicManager::stopSpotifyService()
{
	std::system("sudo systemctl stop sa-spotify 2>/dev/null");
}

void SimpleArcadesMusicManager::pauseSpotifyService()
{
	// Use systemctl kill to send SIGSTOP to the exact librespot PID tracked by systemd.
	// This avoids pkill -f which can match its own shell wrapper and freeze it.
	std::system("sudo systemctl kill -s SIGSTOP sa-spotify 2>/dev/null");
}

void SimpleArcadesMusicManager::resumeSpotifyService()
{
	// Resume the suspended librespot process via SIGCONT.
	std::system("sudo systemctl kill -s SIGCONT sa-spotify 2>/dev/null");
}


// ---- Gameplay volume settings ----

void SimpleArcadesMusicManager::setPlayDuringGameplay(bool play)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mPlayDuringGameplay = play;
}

bool SimpleArcadesMusicManager::getPlayDuringGameplay() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mPlayDuringGameplay;
}

void SimpleArcadesMusicManager::setGameplayVolume(int percent)
{
	std::lock_guard<std::mutex> lock(mMutex);
	mGameplayVolume = saClamp(percent, 10, 100);
}

int SimpleArcadesMusicManager::getGameplayVolume() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mGameplayVolume;
}


// ---- Track controls ----

void SimpleArcadesMusicManager::nextTrack()
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mEnabled || mPausedForGame || mPausedForScreensaver || mPid <= 0 || mPlaylist.empty())
			return;

		// For radio: set popup info NOW (while old stream is still active)
		// so the popup renders before the audio device transitions.
		if (mMode == "radio" && !mRadioStations.empty())
		{
			int nextIdx = saClamp(mIndex + 1, 0, static_cast<int>(mPlaylist.size()) - 1);
			if (nextIdx >= static_cast<int>(mPlaylist.size())) nextIdx = 0;
			int ri = saClamp(nextIdx, 0, static_cast<int>(mRadioStations.size()) - 1);
			mNewTrackSoundtrack = "Internet Radio";
			mNewTrackName       = mRadioStations[ri].name;
			mNewTrackCoverPath  = saFindRadioCoverArt(mRadioStations[ri].name);
			mNewTrackFlag       = true;
		}

		mAdvanceRequested = +1;
		pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess;
		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

void SimpleArcadesMusicManager::prevTrack()
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mEnabled || mPausedForGame || mPausedForScreensaver || mPid <= 0 || mPlaylist.empty())
			return;

		// For radio: set popup info NOW (while old stream is still active).
		if (mMode == "radio" && !mRadioStations.empty())
		{
			int prevIdx = mIndex - 1;
			if (prevIdx < 0) prevIdx = static_cast<int>(mPlaylist.size()) - 1;
			int ri = saClamp(prevIdx, 0, static_cast<int>(mRadioStations.size()) - 1);
			mNewTrackSoundtrack = "Internet Radio";
			mNewTrackName       = mRadioStations[ri].name;
			mNewTrackCoverPath  = saFindRadioCoverArt(mRadioStations[ri].name);
			mNewTrackFlag       = true;
		}

		mAdvanceRequested = -1;
		pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess;
		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

void SimpleArcadesMusicManager::onGameLaunched()
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;
	pid_t pidToSuspend = -1;
	bool isSpotify = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mEnabled)
			return;

		isSpotify = (mMode == "spotify");
		mInGameplay = true;

		// Always stop music here — the launch video needs the audio device.
		mPausedForGame = true;
		mAdvanceRequested = 0;
		mRestartRequested = false;

		if (mPid > 0)
		{
			if (mMode == "radio")
				pidToSuspend = static_cast<pid_t>(mPid);  // SIGSTOP: avoid VCHI deadlock
			else
				{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
		}

		mCv.notify_all();
	}

	if (isSpotify)
		pauseSpotifyService();
	else if (pidToSuspend > 0)
		saSuspendMusicPid(pidToSuspend);
	else if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

void SimpleArcadesMusicManager::startGameplayMusic()
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mEnabled || !mPlayDuringGameplay || !mInGameplay)
			return;
		if (mMode == "spotify")
			return; // TODO: spotify gameplay volume not yet supported

		// Kill any suspended radio process — the game has taken over
		// the audio device, so it's safe to close the stream now.
		if (mPid > 0 && mMode == "radio")
			{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }

		// Unpause and let the thread respawn at gameplay volume.
		mPausedForGame = false;
		mRestartRequested = true;
		mAdvanceRequested = 0;
		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

void SimpleArcadesMusicManager::stopGameplayMusic()
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;
	pid_t pidToSuspend = -1;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mEnabled || !mInGameplay)
			return;
		if (mMode == "spotify")
			return;

		// Pause music before the exit video plays.
		mPausedForGame = true;
		mAdvanceRequested = 0;
		mRestartRequested = false;

		if (mPid > 0)
		{
			if (mMode == "radio")
				pidToSuspend = static_cast<pid_t>(mPid);
			else
				{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
		}

		mCv.notify_all();
	}

	if (pidToSuspend > 0)
		saSuspendMusicPid(pidToSuspend);
	else if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);
}

void SimpleArcadesMusicManager::onGameReturned()
{
	pid_t pidToResume = -1;
	pid_t pidToKill = -1;
	bool killIsRadio = false;
	bool isSpotify = false;
	bool radioSuspended = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);
		if (!mEnabled)
			return;

		isSpotify = (mMode == "spotify");
		mInGameplay = false;

		if (mPid > 0 && mMode == "radio" && mPausedForGame)
		{
			// Radio was suspended — just resume it. No kill, no VCHI
			// channel close/open, no firmware deadlock risk.
			pidToResume = static_cast<pid_t>(mPid);
			radioSuspended = true;
			mPausedForGame = false;
		}
		else
		{
			// Normal (non-radio) path: respawn.
			mPausedForGame = false;
			mRestartRequested = true;
			mCv.notify_all();
		}
	}

	if (isSpotify)
		resumeSpotifyService();
	else if (pidToResume > 0)
		saResumeMusicPid(pidToResume);
}

// --- Music v2: Screensaver hooks ---

void SimpleArcadesMusicManager::onScreenSaverStarted()
{
	std::lock_guard<std::mutex> lock(mMutex);

	if (!mEnabled || mPausedForGame || mPlayDuringScreensaver)
		return;

	// Pause the mpg123 child process via SIGSTOP.
	if (mPid > 0)
	{
		kill(static_cast<pid_t>(mPid), SIGSTOP);
		mPausedForScreensaver = true;
	}
}

void SimpleArcadesMusicManager::onScreenSaverStopped()
{
	std::lock_guard<std::mutex> lock(mMutex);

	if (!mPausedForScreensaver)
		return;

	// Resume the mpg123 child process via SIGCONT.
	if (mPid > 0)
		kill(static_cast<pid_t>(mPid), SIGCONT);

	mPausedForScreensaver = false;
}

void SimpleArcadesMusicManager::setPlayDuringScreensaver(bool play)
{
	std::lock_guard<std::mutex> lock(mMutex);

	// No-change guard.
	if (mPlayDuringScreensaver == play)
		return;

	mPlayDuringScreensaver = play;

	// If toggling ON while currently paused for screensaver, resume immediately.
	if (play && mPausedForScreensaver && mPid > 0)
	{
		kill(static_cast<pid_t>(mPid), SIGCONT);
		mPausedForScreensaver = false;
	}
}

bool SimpleArcadesMusicManager::getPlayDuringScreensaver() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mPlayDuringScreensaver;
}

void SimpleArcadesMusicManager::setShowTrackPopup(bool show)
{
	std::lock_guard<std::mutex> lock(mMutex);

	// No-change guard.
	if (mShowTrackPopup == show)
		return;

	mShowTrackPopup = show;
}

bool SimpleArcadesMusicManager::getShowTrackPopup() const
{
	std::lock_guard<std::mutex> lock(mMutex);
	return mShowTrackPopup;
}

TrackDisplayInfo SimpleArcadesMusicManager::consumeNewTrackInfo()
{
	std::lock_guard<std::mutex> lock(mMutex);
	TrackDisplayInfo info;
	if (!mShowTrackPopup || !mNewTrackFlag)
		return info;  // info.valid is false

	info.soundtrack = mNewTrackSoundtrack;
	info.trackName  = mNewTrackName;
	info.coverPath  = mNewTrackCoverPath;
	info.valid      = true;

	mNewTrackFlag = false;
	return info;
}

// --- Music v2: Rescan ---

bool SimpleArcadesMusicManager::rescanMusic()
{
	pid_t pidToKill = -1;
	bool killIsRadio = false;

	{
		std::lock_guard<std::mutex> lock(mMutex);

		// Collect all tracks from disk.
		std::vector<std::string> allTracks;
		saCollectMp3Recursive(saMusicRootDir(), allTracks);

		// Sync allowlist with what's on disk.
		syncShuffleAllowlistLocked(allTracks);
		saveShuffleAllowlistLocked();

		// Trigger playlist rebuild.
		if (mEnabled && !mPausedForGame)
		{
			mRebuildRequested = true;
			mRestartRequested = false;

			if (mPid > 0)
				{ pidToKill = static_cast<pid_t>(mPid); killIsRadio = mIsRadioProcess; }
		}

		mCv.notify_all();
	}

	if (pidToKill > 0)
		saKillMusicPid(pidToKill, killIsRadio);

	return true;
}

// --- Public shuffle allowlist access (Pass 2) ---

std::vector<std::pair<std::string, bool>> SimpleArcadesMusicManager::getShuffleAllowlist()
{
	std::lock_guard<std::mutex> lock(mMutex);

	// Ensure allowlist is loaded and synced with disk.
	std::vector<std::string> allTracks;
	saCollectMp3Recursive(saMusicRootDir(), allTracks);
	syncShuffleAllowlistLocked(allTracks);

	// Build sorted output.
	std::vector<std::pair<std::string, bool>> result(mShuffleAllowlist.begin(), mShuffleAllowlist.end());
	std::sort(result.begin(), result.end());
	return result;
}

void SimpleArcadesMusicManager::setTrackEnabled(const std::string& relPath, bool enabled)
{
	std::lock_guard<std::mutex> lock(mMutex);
	auto it = mShuffleAllowlist.find(relPath);
	if (it != mShuffleAllowlist.end())
		it->second = enabled;
}

void SimpleArcadesMusicManager::saveShuffleAllowlist()
{
	std::lock_guard<std::mutex> lock(mMutex);
	saveShuffleAllowlistLocked();
}

int SimpleArcadesMusicManager::getTrackCount(const std::string& folderName) const
{
	const std::string folderPath = saMusicRootDir() + "/" + folderName;
	std::vector<std::string> tracks;
	saCollectMp3Recursive(folderPath, tracks);
	return static_cast<int>(tracks.size());
}

std::string SimpleArcadesMusicManager::getCoverArtPath(const std::string& folderName) const
{
	const std::string root = saMusicRootDir() + "/" + folderName;

	const std::string png = root + "/cover.png";
	if (Utils::FileSystem::exists(png))
		return png;

	const std::string jpg = root + "/cover.jpg";
	if (Utils::FileSystem::exists(jpg))
		return jpg;

	return "";
}

// --- Shuffle Allowlist ---

void SimpleArcadesMusicManager::loadShuffleAllowlistLocked()
{
	mShuffleAllowlist.clear();

	const std::string path = saShuffleAllowlistPath();
	if (!Utils::FileSystem::exists(path))
		return;

	std::ifstream in(path);
	if (!in.is_open())
		return;

	std::string line;
	while (std::getline(in, line))
	{
		line = saTrim(line);
		if (line.empty())
			continue;

		// Comment header lines (starting with "# ") are skipped.
		// Lines starting with exactly "#" followed by a path are disabled entries.
		if (line.size() >= 2 && line[0] == '#' && line[1] == ' ')
		{
			// Could be a header comment like "# Simple Arcades shuffle allowlist"
			// or a disabled entry like "# Classic Rock/song.mp3"
			// Heuristic: if it contains a slash or .mp3, it's a track entry.
			const std::string rest = saTrim(line.substr(1));
			if (rest.find('/') != std::string::npos || rest.find(".mp3") != std::string::npos ||
				rest.find(".MP3") != std::string::npos)
			{
				mShuffleAllowlist[rest] = false;
			}
			// Otherwise skip header comments.
			continue;
		}

		if (line[0] == '#')
		{
			// "#path/to/file.mp3" (no space after #)
			const std::string rest = saTrim(line.substr(1));
			if (!rest.empty())
				mShuffleAllowlist[rest] = false;
			continue;
		}

		// Uncommented line = enabled track.
		mShuffleAllowlist[line] = true;
	}
}

void SimpleArcadesMusicManager::saveShuffleAllowlistLocked()
{
	const std::string path = saShuffleAllowlistPath();
	const std::string dir = Utils::FileSystem::getParent(path);
	if (!dir.empty() && !Utils::FileSystem::exists(dir))
		Utils::FileSystem::createDirectory(dir);

	std::ofstream out(path, std::ios::trunc);
	if (!out.is_open())
		return;

	out << "# Simple Arcades shuffle track allowlist\n";
	out << "# Lines starting with # are disabled tracks.\n\n";

	// Sort entries for consistent file output.
	std::vector<std::pair<std::string, bool>> sorted(mShuffleAllowlist.begin(), mShuffleAllowlist.end());
	std::sort(sorted.begin(), sorted.end());

	for (const auto& entry : sorted)
	{
		if (entry.second)
			out << entry.first << "\n";
		else
			out << "#" << entry.first << "\n";
	}

	out.close();
}

void SimpleArcadesMusicManager::syncShuffleAllowlistLocked(const std::vector<std::string>& allTracks)
{
	// Build a set of relative paths currently on disk.
	std::map<std::string, bool> onDisk;
	for (const auto& absPath : allTracks)
	{
		const std::string rel = saRelativePath(absPath);
		onDisk[rel] = true;
	}

	// Load current allowlist from file if not loaded yet.
	if (mShuffleAllowlist.empty())
		loadShuffleAllowlistLocked();

	// Remove entries no longer on disk.
	for (auto it = mShuffleAllowlist.begin(); it != mShuffleAllowlist.end(); )
	{
		if (onDisk.find(it->first) == onDisk.end())
			it = mShuffleAllowlist.erase(it);
		else
			++it;
	}

	// Add new tracks (not yet in allowlist) as enabled.
	for (const auto& diskEntry : onDisk)
	{
		if (mShuffleAllowlist.find(diskEntry.first) == mShuffleAllowlist.end())
			mShuffleAllowlist[diskEntry.first] = true; // enabled by default
	}
}

// --- End Music v2 additions ---

void SimpleArcadesMusicManager::rebuildPlaylistLocked()
{
	mPlaylist.clear();
	mIndex = 0;

	const std::string root = saMusicRootDir();

	if (mMode == "radio")
	{
		// Radio mode: fill playlist with station URLs.
		if (mRadioStations.empty())
			loadRadioStationsLocked();

		if (!mRadioStations.empty())
		{
			for (const auto& station : mRadioStations)
				mPlaylist.push_back(station.url);

			// Start at the selected station.
			mIndex = saClamp(mRadioIndex, 0, static_cast<int>(mPlaylist.size()) - 1);
		}
		return;
	}

	if (mMode == "spotify")
	{
		// Spotify mode: service handles playback, nothing to do here.
		return;
	}

	if (mMode == "folder")
	{
		const std::string folderPath = root + "/" + mFolder;
		saCollectMp3Recursive(folderPath, mPlaylist);
	}
	else
	{
		// Shuffle All mode: collect all, then filter by allowlist.
		std::vector<std::string> allTracks;
		saCollectMp3Recursive(root, allTracks);

		// Sync allowlist with disk (self-healing).
		syncShuffleAllowlistLocked(allTracks);

		// Filter to enabled tracks only.
		for (const auto& track : allTracks)
		{
			const std::string rel = saRelativePath(track);
			auto it = mShuffleAllowlist.find(rel);
			if (it == mShuffleAllowlist.end() || it->second)
				mPlaylist.push_back(track);
		}

		// Safety: if all tracks disabled, play everything.
		if (mPlaylist.empty() && !allTracks.empty())
			mPlaylist = allTracks;
	}

	// If folder mode is selected but empty, fall back to shuffle_all.
	if (mPlaylist.empty() && mMode == "folder")
	{
		saCollectMp3Recursive(root, mPlaylist);
		mMode = "shuffle_all";
	}

	// Shuffle for variety (not for radio — station order matters).
	if (mPlaylist.size() > 1)
	{
		std::mt19937 rng(static_cast<unsigned int>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()));
		std::shuffle(mPlaylist.begin(), mPlaylist.end(), rng);
	}

	mIndex = 0;
}

void SimpleArcadesMusicManager::threadMain()
{
	while (true)
	{
		pid_t pidToWait = -1;

		{
			std::unique_lock<std::mutex> lock(mMutex);

			// Wait until we're enabled and not paused, and either need a rebuild or need to start.
			// In spotify mode, the thread has nothing to do — the sa-spotify service handles playback.
			mCv.wait(lock, [this] {
				return mStopThread ||
					(mEnabled && !mPausedForGame && !mPausedForScreensaver &&
					 mMode != "spotify" &&
						(mPid <= 0 || mRebuildRequested || mRestartRequested));
			});

			if (mStopThread)
				break;

			// Rebuild playlist if requested.
			if (mRebuildRequested || mPlaylist.empty())
			{
				rebuildPlaylistLocked();
				mRebuildRequested = false;
				mRestartRequested = false;
				mAdvanceRequested = 0;
			}

			if (mPlaylist.empty())
			{
				// Nothing to play; wait again.
				continue;
			}

			// If restart was requested, do not change index.
			if (mRestartRequested)
				mRestartRequested = false;

			// Start a track if none is running.
			if (mPid <= 0)
			{
				// Normalize index.
				if (mIndex < 0) mIndex = 0;
				if (mIndex >= static_cast<int>(mPlaylist.size()))
					mIndex = 0;

				const std::string& track = mPlaylist[mIndex];
				const int vol = (mInGameplay && mPlayDuringGameplay) ? mGameplayVolume : mVolumePercent;
				const pid_t pid = saSpawnMpg123(track, vol);
				mPid = static_cast<int>(pid);
				mIsRadioProcess = (mMode == "radio");

				// Set new track info for popup display.
				if (mMode == "radio")
				{
					const int ri = saClamp(mIndex, 0,
						mRadioStations.empty() ? 0 : static_cast<int>(mRadioStations.size()) - 1);
					const std::string stationName =
						(ri < static_cast<int>(mRadioStations.size()))
						? mRadioStations[ri].name : "Internet Radio";

					mNewTrackSoundtrack = "Internet Radio";
					mNewTrackName       = stationName;
					mNewTrackCoverPath  = saFindRadioCoverArt(stationName);
					// NOTE: Do NOT set mNewTrackFlag here for radio.
					// The popup must be delayed until the audio stream is
					// established, otherwise the popup render + audio VCHI
					// open can deadlock the Pi 4 VideoCore firmware.
					// Flag is set after the lock is released below.
					mRadioIndex = mIndex;
				}
				else
				{
					const std::string rel = saRelativePath(track);
					const std::string folder = saExtractSoundtrackFolder(rel);
					const std::string file = saExtractFilename(rel);

					mNewTrackSoundtrack = folder.empty() ? "Music" : saCleanName(folder);
					mNewTrackName       = saCleanName(file);
					mNewTrackCoverPath  = saFindCoverArt(track);
					mNewTrackFlag       = true;
				}
			}

			pidToWait = static_cast<pid_t>(mPid);
		}

		// For radio: delay the popup until the stream is established.
		// This avoids the Pi 4 VCHI firmware deadlock where popup
		// rendering + audio stream init collide.
		{
			bool radioNeedsPopup = false;
			{
				std::lock_guard<std::mutex> lock(mMutex);
				radioNeedsPopup = (mMode == "radio" && !mNewTrackFlag
					&& !mNewTrackName.empty() && pidToWait > 0 && !mStopThread);
			}
			if (radioNeedsPopup)
			{
				// Wait 5 seconds for the stream to fully establish.
				std::this_thread::sleep_for(std::chrono::seconds(5));
				std::lock_guard<std::mutex> lock(mMutex);
				if (!mStopThread && mEnabled && mMode == "radio" && mPid > 0)
					mNewTrackFlag = true;
			}
		}

		// Wait for the mpg123 child to exit.
		if (pidToWait > 0)
		{
			int status = 0;
			waitpid(pidToWait, &status, 0);
		}

		// Decide what to do next.
		{
			std::lock_guard<std::mutex> lock(mMutex);

			// Child ended.
			if (mPid > 0)
				mPid = -1;

			if (mStopThread)
				break;

			if (!mEnabled || mPausedForGame)
				continue;

			if (mPlaylist.empty())
			{
				mRebuildRequested = true;
				continue;
			}

			// Apply pending advance if requested (from next/prev).
			if (mAdvanceRequested != 0)
			{
				mIndex += mAdvanceRequested;
				mAdvanceRequested = 0;
			}
			else if (mMode == "radio")
			{
				// Radio stream ended unexpectedly (network drop).
				// Stay on same station index — it will reconnect.
				// Brief delay to avoid tight reconnect loop.
			}
			else
			{
				// Natural end -> next
				mIndex += 1;
			}

			if (mIndex < 0)
				mIndex = static_cast<int>(mPlaylist.size()) - 1;
			if (mIndex >= static_cast<int>(mPlaylist.size()))
				mIndex = 0;

			// Loop will start the next track on the next iteration.
			mCv.notify_all();
		}

		// Radio reconnect delay (outside lock).
		// Prevents tight loop if a radio stream keeps dropping immediately.
		{
			bool doDelay = false;
			{
				std::lock_guard<std::mutex> lock(mMutex);
				doDelay = (mMode == "radio" && mAdvanceRequested == 0 && !mStopThread && mEnabled);
			}
			if (doDelay)
				std::this_thread::sleep_for(std::chrono::seconds(3));
		}
	}
}

void SimpleArcadesMusicManager::loadConfig()
{
	std::lock_guard<std::mutex> lock(mMutex);

	// Defaults.
	mEnabled = false;
	mVolumePercent = 70;
	mMode = "shuffle_all";
	mFolder = "";
	mPlayDuringScreensaver = true;
	mShowTrackPopup = true;
	mPlayDuringGameplay = false;
	mGameplayVolume = 50;

	std::string savedStationName;

	const std::string cfg = saMusicConfigPath();
	if (!Utils::FileSystem::exists(cfg))
	{
		// If there are folders, default to first.
		const auto folders = saListSoundtrackFolders();
		if (!folders.empty())
			mFolder = folders.front();

		// Load shuffle allowlist (best effort).
		loadShuffleAllowlistLocked();
		return;
	}

	std::ifstream in(cfg);
	if (!in.is_open())
		return;

	std::string line;
	while (std::getline(in, line))
	{
		line = saTrim(line);
		if (line.empty() || line[0] == '#')
			continue;

		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;

		std::string key = saTrim(line.substr(0, eq));
		std::string val = saTrim(line.substr(eq + 1));

		key = Utils::String::toLower(key);

		if (key == "enabled")
			mEnabled = (val == "1" || Utils::String::toLower(val) == "true" || Utils::String::toLower(val) == "yes");
		else if (key == "volume")
			mVolumePercent = saClamp(std::atoi(val.c_str()), 0, 100);
		else if (key == "mode")
		{
			val = Utils::String::toLower(val);
			if (val == "folder" || val == "shuffle_all" || val == "radio" || val == "spotify")
				mMode = val;
		}
		else if (key == "folder")
			mFolder = val;
		else if (key == "station")
			savedStationName = val;
		else if (key == "play_during_screensaver")
			mPlayDuringScreensaver = (val == "1" || Utils::String::toLower(val) == "true" || Utils::String::toLower(val) == "yes");
		else if (key == "show_track_popup")
			mShowTrackPopup = (val == "1" || Utils::String::toLower(val) == "true" || Utils::String::toLower(val) == "yes");
		else if (key == "play_during_gameplay")
			mPlayDuringGameplay = (val == "1" || Utils::String::toLower(val) == "true");
		else if (key == "gameplay_volume")
			mGameplayVolume = saClamp(std::atoi(val.c_str()), 10, 100);
	}

	// If folder is empty, default to first available.
	if (mFolder.empty())
	{
		const auto folders = saListSoundtrackFolders();
		if (!folders.empty())
			mFolder = folders.front();
	}

	// Load radio stations and resolve saved station name to index.
	loadRadioStationsLocked();
	if (!savedStationName.empty())
	{
		for (int i = 0; i < static_cast<int>(mRadioStations.size()); ++i)
		{
			if (mRadioStations[i].name == savedStationName)
			{
				mRadioIndex = i;
				break;
			}
		}
	}

	// Validate spotify mode.
	if (mMode == "spotify" && !isSpotifyAvailable())
		mMode = "shuffle_all";

	// Load shuffle allowlist.
	loadShuffleAllowlistLocked();
}

void SimpleArcadesMusicManager::saveConfig()
{
	std::lock_guard<std::mutex> lock(mMutex);

	const std::string cfg = saMusicConfigPath();
	const std::string dir = Utils::FileSystem::getParent(cfg);
	if (!dir.empty() && !Utils::FileSystem::exists(dir))
		Utils::FileSystem::createDirectory(dir);

	std::ofstream out(cfg, std::ios::trunc);
	if (!out.is_open())
		return;

	out << "# Simple Arcades background music\n";
	out << "# Values are saved by EmulationStation when you exit the Music Settings menu.\n\n";
	out << "enabled=" << (mEnabled ? "1" : "0") << "\n";
	out << "volume=" << mVolumePercent << "\n";
	out << "mode=" << mMode << "\n";
	out << "folder=" << mFolder << "\n";

	// Save radio station by name (survives reordering of radio_stations.cfg).
	if (mRadioIndex >= 0 && mRadioIndex < static_cast<int>(mRadioStations.size()))
		out << "station=" << mRadioStations[mRadioIndex].name << "\n";
	else
		out << "station=\n";

	out << "play_during_screensaver=" << (mPlayDuringScreensaver ? "1" : "0") << "\n";
	out << "show_track_popup=" << (mShowTrackPopup ? "1" : "0") << "\n";
	out << "play_during_gameplay=" << (mPlayDuringGameplay ? "1" : "0") << "\n";
	out << "gameplay_volume=" << mGameplayVolume << "\n";
	out.close();

	// Also persist the shuffle allowlist.
	saveShuffleAllowlistLocked();
}