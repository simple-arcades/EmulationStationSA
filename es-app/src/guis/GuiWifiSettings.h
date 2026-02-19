#pragma once
#ifndef ES_APP_GUIS_GUI_WIFI_SETTINGS_H
#define ES_APP_GUIS_GUI_WIFI_SETTINGS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include <string>
#include <vector>

class GuiWifiSettings : public GuiComponent
{
public:
	GuiWifiSettings(Window* window);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	MenuComponent mMenu;

	struct NetworkInfo
	{
		std::string ssid;
		int signalLevel;
		std::string flags;
		bool isSecured;
	};

	struct ConnectionStatus
	{
		bool connected;
		std::string ssid;
		std::string wifiIp;
		std::string ethIp;
	};

	std::vector<NetworkInfo> mNetworks;
	ConnectionStatus mStatus;
	bool mScanning;

	void scan();
	void buildList();
	void connectToNetwork(const std::string& ssid, bool secured);
	void connectHidden();
	void disconnect();
	void clearSavedNetworks();

	void refreshStatus();
	std::string getSignalPercent(int level);

	std::string runCommand(const std::string& cmd);
	std::string shellEscape(const std::string& input);
	std::vector<NetworkInfo> parseScanResults(const std::string& output);
	ConnectionStatus parseStatus(const std::string& output);
	bool wpaConnect(const std::string& ssid, const std::string& psk, bool hidden = false);
	void wpaDisconnect();
	void wpaClearAll();
};

#endif // ES_APP_GUIS_GUI_WIFI_SETTINGS_H
