#include "scrapers/Scraper.h"

#include "FileData.h"
#include "GamesDBJSONScraper.h"
#include "ScreenScraper.h"
#include "Log.h"
#include "Settings.h"
#include "SystemData.h"
#include "utils/FileSystemUtil.h"
#include <FreeImage.h>
#include <fstream>

const std::map<std::string, generate_scraper_requests_func> scraper_request_funcs {
	{ "TheGamesDB", &thegamesdb_generate_json_scraper_requests },
	{ "ScreenScraper", &screenscraper_generate_scraper_requests }
};

std::unique_ptr<ScraperSearchHandle> startScraperSearch(const ScraperSearchParams& params)
{
	const std::string& name = Settings::getInstance()->getString("Scraper");
	std::unique_ptr<ScraperSearchHandle> handle(new ScraperSearchHandle());

	// Check if the Scraper in the settings still exists as a registered scraping source.
	if (scraper_request_funcs.find(name) == scraper_request_funcs.end())
	{
		LOG(LogWarning) << "Configured scraper (" << name << ") unavailable, scraping aborted.";
	}
	else
	{
		scraper_request_funcs.at(name)(params, handle->mRequestQueue, handle->mResults);
	}

	return handle;
}

std::vector<std::string> getScraperList()
{
	std::vector<std::string> list;
	for(auto it = scraper_request_funcs.cbegin(); it != scraper_request_funcs.cend(); it++)
	{
		list.push_back(it->first);
	}

	return list;
}

bool isValidConfiguredScraper()
{
	const std::string& name = Settings::getInstance()->getString("Scraper");
	return scraper_request_funcs.find(name) != scraper_request_funcs.end();
}

// ScraperSearchHandle
ScraperSearchHandle::ScraperSearchHandle()
{
	setStatus(ASYNC_IN_PROGRESS);
}

void ScraperSearchHandle::update()
{
	if(mStatus == ASYNC_DONE)
		return;

	if(!mRequestQueue.empty())
	{
		// a request can add more requests to the queue while running,
		// so be careful with references into the queue
		auto& req = *(mRequestQueue.front());
		AsyncHandleStatus status = req.status();

		if(status == ASYNC_ERROR)
		{
			// propegate error
			setError(req.getStatusString());

			// empty our queue
			while(!mRequestQueue.empty())
				mRequestQueue.pop();

			return;
		}

		// finished this one, see if we have any more
		if(status == ASYNC_DONE)
		{
			mRequestQueue.pop();
		}

		// status == ASYNC_IN_PROGRESS
	}

	// we finished without any errors!
	if(mRequestQueue.empty())
	{
		setStatus(ASYNC_DONE);
		return;
	}
}



// ScraperRequest
ScraperRequest::ScraperRequest(std::vector<ScraperSearchResult>& resultsWrite) : mResults(resultsWrite)
{
}


// ScraperHttpRequest
ScraperHttpRequest::ScraperHttpRequest(std::vector<ScraperSearchResult>& resultsWrite, const std::string& url)
	: ScraperRequest(resultsWrite)
{
	setStatus(ASYNC_IN_PROGRESS);
	mReq = std::unique_ptr<HttpReq>(new HttpReq(url));
}

void ScraperHttpRequest::update()
{
	HttpReq::Status status = mReq->status();
	if(status == HttpReq::REQ_SUCCESS)
	{
		setStatus(ASYNC_DONE); // if process() has an error, status will be changed to ASYNC_ERROR
		process(mReq, mResults);
		return;
	}

	// not ready yet
	if(status == HttpReq::REQ_IN_PROGRESS)
		return;

	// everything else is some sort of error
	LOG(LogError) << "ScraperHttpRequest network error (status: " << status << ") - " << mReq->getErrorMsg();
	setError(mReq->getErrorMsg());
}


// metadata resolving stuff

std::unique_ptr<MDResolveHandle> resolveMetaDataAssets(const ScraperSearchResult& result, const ScraperSearchParams& search)
{
	return std::unique_ptr<MDResolveHandle>(new MDResolveHandle(result, search));
}

MDResolveHandle::MDResolveHandle(const ScraperSearchResult& result, const ScraperSearchParams& search) : mResult(result)
{
	// Precompute the ROM directory for relative path conversion
	std::string romDir = Utils::FileSystem::getParent(search.game->getPath());

	if(!result.imageUrl.empty())
	{
		std::string ext;

		if (!result.imageType.empty())
		{
			ext = result.imageType;
		}else{
			size_t dot = result.imageUrl.find_last_of('.');
			if (dot != std::string::npos)
				ext = result.imageUrl.substr(dot, std::string::npos);
		}

		std::string imgPath = getSaveAsPath(search, "image", ext);

		// Convert to relative path for gamelist.xml
		std::string imgRelPath = imgPath;
		if (imgRelPath.find(romDir) == 0)
			imgRelPath = "./" + imgRelPath.substr(romDir.length() + 1);

		// Skip download if image already exists on disk
		if (!Utils::FileSystem::exists(imgPath))
		{
			mFuncs.push_back(ResolvePair(downloadImageAsync(result.imageUrl, imgPath), [this, imgRelPath]
			{
				mResult.mdl.set("image", imgRelPath);
				mResult.imageUrl = "";
			}));
		}
		else
		{
			mResult.mdl.set("image", imgRelPath);
			mResult.imageUrl = "";
		}
	}

	// Video download support
	if (!result.videoUrl.empty())
	{
		std::string videoPath = getSaveAsPath(search, "video", ".mp4");

		// Convert to relative path for gamelist.xml
		std::string videoRelPath = videoPath;
		if (videoRelPath.find(romDir) == 0)
			videoRelPath = "./" + videoRelPath.substr(romDir.length() + 1);

		// Skip download if video already exists on disk
		if (!Utils::FileSystem::exists(videoPath))
		{
			mFuncs.push_back(ResolvePair(
				std::unique_ptr<AsyncHandle>(new ImageDownloadHandle(result.videoUrl, videoPath, 0, 0)),
				[this, videoRelPath]
				{
					mResult.mdl.set("video", videoRelPath);
					mResult.videoUrl = "";
				}));
		}
		else
		{
			mResult.mdl.set("video", videoRelPath);
			mResult.videoUrl = "";
		}
	}
}

void MDResolveHandle::update()
{
	if(mStatus == ASYNC_DONE || mStatus == ASYNC_ERROR)
		return;

	auto it = mFuncs.cbegin();
	while(it != mFuncs.cend())
	{
		if(it->first->status() == ASYNC_ERROR)
		{
			setError(it->first->getStatusString());
			return;
		}else if(it->first->status() == ASYNC_DONE)
		{
			it->second();
			it = mFuncs.erase(it);
			continue;
		}
		it++;
	}

	if(mFuncs.empty())
		setStatus(ASYNC_DONE);
}

std::unique_ptr<ImageDownloadHandle> downloadImageAsync(const std::string& url, const std::string& saveAs)
{
	return std::unique_ptr<ImageDownloadHandle>(new ImageDownloadHandle(url, saveAs,
		Settings::getInstance()->getInt("ScraperResizeWidth"), Settings::getInstance()->getInt("ScraperResizeHeight")));
}

ImageDownloadHandle::ImageDownloadHandle(const std::string& url, const std::string& path, int maxWidth, int maxHeight) :
	mSavePath(path), mMaxWidth(maxWidth), mMaxHeight(maxHeight), mReq(new HttpReq(url))
{
}

void ImageDownloadHandle::update()
{
	if(mReq->status() == HttpReq::REQ_IN_PROGRESS)
		return;

	if(mReq->status() != HttpReq::REQ_SUCCESS)
	{
		std::stringstream ss;
		ss << "Network error: " << mReq->getErrorMsg();
		setError(ss.str());
		return;
	}

	// download is done, save it to disk
	std::ofstream stream(mSavePath, std::ios_base::out | std::ios_base::binary);
	if(stream.bad())
	{
		setError("Failed to open image path to write. Permission error? Disk full?");
		return;
	}

	const std::string& content = mReq->getContent();
	stream.write(content.data(), content.length());
	stream.close();
	if(stream.bad())
	{
		setError("Failed to save image. Disk full?");
		return;
	}

	// resize it
	if(!resizeImage(mSavePath, mMaxWidth, mMaxHeight))
	{
		setError("Error saving resized image. Out of memory? Disk full?");
		return;
	}

	setStatus(ASYNC_DONE);
}

//you can pass 0 for width or height to keep aspect ratio
bool resizeImage(const std::string& path, int maxWidth, int maxHeight)
{
	// nothing to do
	if(maxWidth == 0 && maxHeight == 0)
		return true;

	FREE_IMAGE_FORMAT format = FIF_UNKNOWN;
	FIBITMAP* image = NULL;

	//detect the filetype
	format = FreeImage_GetFileType(path.c_str(), 0);
	if(format == FIF_UNKNOWN)
		format = FreeImage_GetFIFFromFilename(path.c_str());
	if(format == FIF_UNKNOWN)
	{
		LOG(LogError) << "Error - could not detect filetype for image \"" << path << "\"!";
		return false;
	}

	//make sure we can read this filetype first, then load it
	if(FreeImage_FIFSupportsReading(format))
	{
		image = FreeImage_Load(format, path.c_str());
	}else{
		LOG(LogError) << "Error - file format reading not supported for image \"" << path << "\"!";
		return false;
	}

	float width = (float)FreeImage_GetWidth(image);
	float height = (float)FreeImage_GetHeight(image);

	if(maxWidth == 0)
	{
		maxWidth = (int)((maxHeight / height) * width);
	}else if(maxHeight == 0)
	{
		maxHeight = (int)((maxWidth / width) * height);
	}

	FIBITMAP* imageRescaled = FreeImage_Rescale(image, maxWidth, maxHeight, FILTER_BILINEAR);
	FreeImage_Unload(image);

	if(imageRescaled == NULL)
	{
		LOG(LogError) << "Could not resize image! (not enough memory? invalid bitdepth?)";
		return false;
	}

	bool saved = (FreeImage_Save(format, imageRescaled, path.c_str()) != 0);
	FreeImage_Unload(imageRescaled);

	if(!saved)
		LOG(LogError) << "Failed to save resized image!";

	return saved;
}

std::string getSaveAsPath(const ScraperSearchParams& params, const std::string& suffix, const std::string& extension)
{
	// Simple Arcades folder structure:
	//   [romdir]/media/images/[romStem].png   (for images)
	//   [romdir]/media/videos/[romStem].mp4   (for videos)
	const std::string romDir = Utils::FileSystem::getParent(params.game->getPath());
	const std::string romStem = Utils::FileSystem::getStem(params.game->getPath());

	std::string subdir;
	if (suffix == "video")
		subdir = "videos";
	else
		subdir = "images";

	std::string mediaDir = romDir + "/media/";
	if (!Utils::FileSystem::exists(mediaDir))
		Utils::FileSystem::createDirectory(mediaDir);

	std::string path = mediaDir + subdir + "/";
	if (!Utils::FileSystem::exists(path))
		Utils::FileSystem::createDirectory(path);

	// Use just the ROM stem (no "-image" suffix) to match Skraper output
	path += romStem + extension;
	return path;
}
