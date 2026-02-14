#pragma once
#ifndef ES_APP_GUIS_GUI_SIMPLE_ARCADES_SCREENSAVER_GALLERY_OPTIONS_H
#define ES_APP_GUIS_GUI_SIMPLE_ARCADES_SCREENSAVER_GALLERY_OPTIONS_H

#include "guis/GuiScreensaverOptions.h"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

class SwitchComponent;

class GuiSimpleArcadesScreensaverGalleryOptions : public GuiScreensaverOptions
{
public:
	GuiSimpleArcadesScreensaverGalleryOptions(Window* window, const char* title);
	virtual ~GuiSimpleArcadesScreensaverGalleryOptions();
	
private:
	struct Entry {
		std::string relPath;                          // e.g. "generic_screensavers/Foo.mp4"
		std::shared_ptr<SwitchComponent> toggle;      // enabled/disabled
	};

	std::string mRootDir;
	std::string mAllowListPath;

	std::vector<Entry> mEntries;

	bool initPaths();
	static std::string prettyLabelFromRel(const std::string& rel);

	void loadAndSync(std::vector<std::string>& allRel,
		std::unordered_map<std::string, bool>& enabledByRel);
};

#endif // ES_APP_GUIS_GUI_SIMPLE_ARCADES_SCREENSAVER_GALLERY_OPTIONS_H