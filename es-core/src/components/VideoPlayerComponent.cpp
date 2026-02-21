#if defined(_OMX_) || defined(_MPV_PLAYER_)
#include "components/VideoPlayerComponent.h"

#include "renderers/Renderer.h"
#include "utils/StringUtil.h"
#include "AudioManager.h"
#include "Settings.h"
#include <fcntl.h>
#include <unistd.h>
#include <wait.h>
#include <dirent.h>

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

/* Find the DRM card fd in our own process */
static int find_drm_fd(void)
{
	DIR *dir = opendir("/proc/self/fd");
	if (!dir) return -1;

	struct dirent *ent;
	while ((ent = readdir(dir)) != NULL) {
		if (ent->d_name[0] == '.') continue;
		int fd = atoi(ent->d_name);
		if (fd <= 2) continue;

		char link[256];
		char path[64];
		snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
		ssize_t len = readlink(path, link, sizeof(link) - 1);
		if (len < 0) continue;
		link[len] = '\0';

		if (strstr(link, "/dev/dri/card") != NULL) {
			closedir(dir);
			return fd;
		}
	}
	closedir(dir);
	return -1;
}

void VideoPlayerComponent::startVideo()
{
	if (!mIsPlaying)
	{
		mVideoWidth = 0;
		mVideoHeight = 0;

		std::string path(mVideoPath.c_str());

		if ((path.size() > 0) && (mPlayerPid == -1))
		{
			mPlayingVideoPath = mVideoPath;

			/* Find the DRM fd before forking */
			int drm_fd = find_drm_fd();

			pid_t pid = fork();
			if (pid == -1)
			{
				mPlayingVideoPath = "";
			}
			else if (pid > 0)
			{
				mPlayerPid = pid;
				signal(SIGCHLD, catch_child);
				mIsPlaying = true;
				mFadeIn = 0.0f;
			}
			else
			{
				/* Child process - launch sa_videoplayer */

				/* IMPORTANT: Save DRM fd to a high number BEFORE
				 * any open/dup2 calls that might clobber it */
				int safe_drm_fd = -1;
				if (drm_fd >= 0) {
					safe_drm_fd = fcntl(drm_fd, F_DUPFD, 100); /* dup to fd >= 100 */
				}
				char drm_fd_str2[16];
				snprintf(drm_fd_str2, sizeof(drm_fd_str2), "%d", safe_drm_fd);

				bool mute = (!Settings::getInstance()->getBool("VideoAudio") ||
					(float)VolumeControl::getInstance()->getVolume() == 0) ||
					(Settings::getInstance()->getBool("ScreenSaverVideoMute") && mScreensaverMode);

				/* Redirect stdout/stderr to /dev/null */
				int fdin = open("/dev/null", O_RDONLY);
				int fdout = open("/dev/null", O_WRONLY);
				dup2(fdin, 0);
				dup2(fdout, 1);
				dup2(fdout, 2);

				if (safe_drm_fd >= 0)
				{
					if (mute)
					{
						execlp("/opt/simplearcades/tools/sa_videoplayer",
							"sa_videoplayer",
							"--drm-fd", drm_fd_str2,
							"--loop",
							"--no-audio",
							"--layer", "10",
							mPlayingVideoPath.c_str(),
							(char*)NULL);
					}
					else
					{
						execlp("/opt/simplearcades/tools/sa_videoplayer",
							"sa_videoplayer",
							"--drm-fd", drm_fd_str2,
							"--loop",
							"--layer", "10",
							mPlayingVideoPath.c_str(),
							(char*)NULL);
					}
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

	if (mPlayerPid != -1)
	{
		int status;
		kill(mPlayerPid, SIGKILL);
		waitpid(mPlayerPid, &status, WNOHANG);
		mPlayerPid = -1;
	}
}

#endif
