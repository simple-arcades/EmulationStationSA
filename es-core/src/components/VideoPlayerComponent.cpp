#if defined(_OMX_) || defined(_MPV_PLAYER_)
#include "components/VideoPlayerComponent.h"

#include "renderers/Renderer.h"
#include "utils/StringUtil.h"
#include "AudioManager.h"
#include "Settings.h"
#include <fcntl.h>
#include <unistd.h>
#include <wait.h>

class VolumeControl
{
public:
	static std::shared_ptr<VolumeControl> & getInstance();
	int getVolume() const;
};

VideoPlayerComponent::VideoPlayerComponent(Window* window, std::string path) :
	VideoComponent(window),
	mPlayerPid(-1),
	subtitlePath(path)
{
}

VideoPlayerComponent::~VideoPlayerComponent()
{
	stopVideo();
}

void VideoPlayerComponent::render(const Transform4x4f& parentTrans)
{
	if (!isVisible())
		return;

	VideoComponent::render(parentTrans);

	if (!mIsPlaying || mPlayerPid == -1)
		VideoComponent::renderSnapshot(parentTrans);
}

void VideoPlayerComponent::setResize(float width, float height)
{
	setSize(width, height);
	mTargetSize = Vector2f(width, height);
	mTargetIsMax = false;
	mStaticImage.setResize(width, height);
	onSizeChanged();
}

void VideoPlayerComponent::setMaxSize(float width, float height)
{
	setSize(width, height);
	mTargetSize = Vector2f(width, height);
	mTargetIsMax = true;
	mStaticImage.setMaxSize(width, height);
	onSizeChanged();
}

void VideoPlayerComponent::startVideo()
{
	if (!mIsPlaying)
	{
		mVideoWidth = 0;
		mVideoHeight = 0;

		std::string path(mVideoPath.c_str());

		// Make sure we have a video path
		if ((path.size() > 0) && (mPlayerPid == -1))
		{
			// Set the video that we are going to be playing so we don't attempt to restart it
			mPlayingVideoPath = mVideoPath;

			// Start the player process
			pid_t pid = fork();
			if (pid == -1)
			{
				// Failed
				mPlayingVideoPath = "";
			}
			else if (pid > 0)
			{
				mPlayerPid = pid;
				// Update the playing state
				signal(SIGCHLD, catch_child);
				mIsPlaying = true;
				mFadeIn = 0.0f;
			}
			else
			{
				// Child process - launch mpv as external video player

				// Check if we want to mute the audio
				bool mute = (!Settings::getInstance()->getBool("VideoAudio") ||
					(float)VolumeControl::getInstance()->getVolume() == 0) ||
					(Settings::getInstance()->getBool("ScreenSaverVideoMute") && mScreensaverMode);

				// Redirect stdout/stderr to /dev/null
				int fdin = open("/dev/null", O_RDONLY);
				int fdout = open("/dev/null", O_WRONLY);
				dup2(fdin, 0);
				dup2(fdout, 1);
				dup2(fdout, 2);

				if (mute)
				{
					execlp("mpv", "mpv",
						"--fullscreen",
						"--no-input-terminal",
						"--really-quiet",
						"--loop",
						"--no-osd-bar",
						"--hwdec=auto",
						"--vo=gpu",
						"--no-audio",
						mPlayingVideoPath.c_str(),
						(char*)NULL);
				}
				else
				{
					execlp("mpv", "mpv",
						"--fullscreen",
						"--no-input-terminal",
						"--really-quiet",
						"--loop",
						"--no-osd-bar",
						"--hwdec=auto",
						"--vo=gpu",
						mPlayingVideoPath.c_str(),
						(char*)NULL);
				}

				_exit(EXIT_FAILURE);
			}
		}
	}
}

void catch_child(int sig_num)
{
	while (1)
	{
		int child_status;
		pid_t pid = waitpid(-1, &child_status, WNOHANG);
		if (pid <= 0)
		{
			break;
		}
	}
}

void VideoPlayerComponent::stopVideo()
{
	mIsPlaying = false;
	mStartDelayed = false;

	// Stop the player process
	if (mPlayerPid != -1)
	{
		int status;
		kill(mPlayerPid, SIGKILL);
		waitpid(mPlayerPid, &status, WNOHANG);
		mPlayerPid = -1;
	}
}

#endif
