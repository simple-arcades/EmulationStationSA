// ============================================================================
//  GuiWifiSettings.cpp
//
//  Native EmulationStation WiFi manager.
//  Uses MenuComponent::setSubtitle() for non-interactive status display.
// ============================================================================
#include "guis/GuiWifiSettings.h"
#include "SAStyle.h"

#include "guis/GuiMsgBox.h"
#include "guis/GuiTextInput.h"
#include "components/TextComponent.h"
#include "components/ComponentList.h"
#include "Window.h"

#include <cstdio>
#include <array>
#include <algorithm>
#include <sstream>
#include <thread>
#include <chrono>

GuiWifiSettings::GuiWifiSettings(Window* window)
	: GuiComponent(window),
	  mMenu(window, "WI-FI SETTINGS"),
	  mScanning(false)
{
	addChild(&mMenu);

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition(
		(Renderer::getScreenWidth() - mMenu.getSize().x()) / 2,
		Renderer::getScreenHeight() * 0.15f);

	scan();
}

void GuiWifiSettings::scan()
{
	mScanning = true;
	mWindow->renderLoadingScreen("SCANNING FOR WI-FI NETWORKS...");

	runCommand("sudo iw reg set US");
	runCommand("sudo wpa_cli -i wlan0 scan");

	for (int i = 0; i < 5; i++)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		mWindow->renderLoadingScreen("SCANNING FOR WI-FI NETWORKS...");

		std::string results = runCommand("sudo wpa_cli -i wlan0 scan_results");
		auto nets = parseScanResults(results);
		if (!nets.empty())
		{
			mNetworks = nets;
			break;
		}
	}

	if (mNetworks.empty())
	{
		std::string results = runCommand("sudo wpa_cli -i wlan0 scan_results");
		mNetworks = parseScanResults(results);
	}

	mScanning = false;
	refreshStatus();
	buildList();
}

void GuiWifiSettings::refreshStatus()
{
	std::string statusOutput = runCommand("sudo wpa_cli -i wlan0 status");
	mStatus = parseStatus(statusOutput);

	std::string wifiIp = runCommand("ip -4 addr show wlan0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1");
	std::string ethIp = runCommand("ip -4 addr show eth0 2>/dev/null | awk '/inet /{print $2}' | cut -d/ -f1");

	while (!wifiIp.empty() && (wifiIp.back() == '\n' || wifiIp.back() == ' '))
		wifiIp.pop_back();
	while (!ethIp.empty() && (ethIp.back() == '\n' || ethIp.back() == ' '))
		ethIp.pop_back();

	mStatus.wifiIp = wifiIp;
	mStatus.ethIp = ethIp;
}

std::string GuiWifiSettings::getSignalPercent(int level)
{
	int pct;
	if (level >= -30)      pct = 100;
	else if (level <= -90) pct = 0;
	else
	{
		pct = (int)(((float)(level + 90) / 60.0f) * 100.0f);
		if (pct > 100) pct = 100;
		if (pct < 0) pct = 0;
	}
	return std::to_string(pct) + "%";
}

void GuiWifiSettings::buildList()
{
	// Build subtitle lines as simple strings
	bool hasEth = !mStatus.ethIp.empty();
	bool hasWifi = mStatus.connected && !mStatus.ssid.empty();

	// Ethernet line
	std::string ethLine;
	unsigned int ethColor = 0x888888FF;
	if (hasEth)
	{
		ethLine = "ETHERNET: " + mStatus.ethIp;
		ethColor = 0x44DD44FF;
	}

	// Wi-Fi line
	std::string wifiLine;
	unsigned int wifiColor = 0x888888FF;
	if (hasWifi)
	{
		wifiLine = "WI-FI: " + mStatus.ssid;
		if (!mStatus.wifiIp.empty())
			wifiLine += " " + mStatus.wifiIp;
		wifiColor = 0x44DD44FF;
	}
	else
	{
		wifiLine = "WI-FI: NOT CONNECTED";
		wifiColor = 0x888888FF;
	}

	// Set subtitle: ethernet first (if present), wifi second
	if (hasEth)
		mMenu.setSubtitle(ethLine, ethColor, wifiLine, wifiColor);
	else
		mMenu.setSubtitle(wifiLine, wifiColor);

	// Sort networks by signal
	std::sort(mNetworks.begin(), mNetworks.end(),
		[](const NetworkInfo& a, const NetworkInfo& b) {
			return a.signalLevel > b.signalLevel;
		});

	// Network list
	for (const auto& net : mNetworks)
	{
		bool isConnected = (mStatus.connected && net.ssid == mStatus.ssid);

		std::string info;
		if (isConnected)
			info = "CONNECTED";
		else
		{
			info = getSignalPercent(net.signalLevel);
			info += net.isSecured ? " SECURED" : " OPEN";
		}

		auto infoText = std::make_shared<TextComponent>(
			mWindow, info,
			saFont(FONT_SIZE_SMALL),
			isConnected ? 0x44DD44FF : SA_TEXT_COLOR);

		ComponentListRow row;
		auto nameText = std::make_shared<TextComponent>(
			mWindow, net.ssid,
			saFont(FONT_SIZE_MEDIUM),
			isConnected ? 0x44DD44FF : SA_TEXT_COLOR);
		row.addElement(nameText, true);
		row.addElement(infoText, false);

		std::string ssid = net.ssid;
		bool secured = net.isSecured;
		row.makeAcceptInputHandler([this, ssid, secured]() {
			connectToNetwork(ssid, secured);
		});

		mMenu.addRow(row);
	}

	if (mNetworks.empty())
	{
		ComponentListRow row;
		auto noNets = std::make_shared<TextComponent>(
			mWindow, "NO NETWORKS FOUND",
			saFont(FONT_SIZE_MEDIUM), 0x888888FF);
		row.addElement(noNets, true);
		mMenu.addRow(row);
	}

	mMenu.addButton("SCAN", "scan", [this]() {
		Window* w = mWindow;
		delete this;
		w->pushGui(new GuiWifiSettings(w));
	});

	mMenu.addButton("HIDDEN", "hidden", [this]() { connectHidden(); });
	mMenu.addButton("DISCONNECT", "disconnect", [this]() { disconnect(); });
	mMenu.addButton("CLEAR ALL", "clear", [this]() { clearSavedNetworks(); });
	mMenu.addButton("BACK", "back", [this]() { delete this; });
}

void GuiWifiSettings::connectToNetwork(const std::string& ssid, bool secured)
{
	if (!secured)
	{
		mWindow->pushGui(new GuiMsgBox(mWindow,
			"CONNECT TO \"" + ssid + "\" WITHOUT A PASSWORD?",
			"YES", [this, ssid]() {
				mWindow->renderLoadingScreen("CONNECTING TO \"" + ssid + "\"...");
				bool ok = wpaConnect(ssid, "");
				if (ok)
				{
					mWindow->pushGui(new GuiMsgBox(mWindow,
						"CONNECTED TO \"" + ssid + "\"!", "OK",
						[this]() {
							Window* w = mWindow;
							delete this;
							w->pushGui(new GuiWifiSettings(w));
						}));
				}
				else
				{
					mWindow->pushGui(new GuiMsgBox(mWindow,
						"COULD NOT CONNECT TO \"" + ssid + "\".\n\n"
						"CHECK SIGNAL STRENGTH AND TRY AGAIN.", "OK", nullptr));
				}
			},
			"NO", nullptr));
		return;
	}

	mWindow->pushGui(new GuiTextInput(mWindow,
		"PASSWORD FOR \"" + ssid + "\":",
		"",
		[this, ssid](const std::string& password) {
			mWindow->renderLoadingScreen("CONNECTING TO \"" + ssid + "\"...");
			bool ok = wpaConnect(ssid, password);
			if (ok)
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					"CONNECTED TO \"" + ssid + "\"!", "OK",
					[this]() {
						Window* w = mWindow;
						delete this;
						w->pushGui(new GuiWifiSettings(w));
					}));
			}
			else
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					"COULD NOT CONNECT TO \"" + ssid + "\".\n\n"
					"DOUBLE CHECK YOUR PASSWORD AND TRY AGAIN.", "OK", nullptr));
			}
		},
		true, 8));
}

void GuiWifiSettings::connectHidden()
{
	mWindow->pushGui(new GuiTextInput(mWindow,
		"ENTER HIDDEN NETWORK NAME (SSID):",
		"",
		[this](const std::string& ssid) {
			mWindow->pushGui(new GuiTextInput(mWindow,
				"PASSWORD FOR \"" + ssid + "\":",
				"",
				[this, ssid](const std::string& password) {
					mWindow->renderLoadingScreen("CONNECTING TO \"" + ssid + "\"...");
					bool ok = wpaConnect(ssid, password, true);
					if (ok)
					{
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"CONNECTED TO \"" + ssid + "\"!", "OK",
							[this]() {
								Window* w = mWindow;
								delete this;
								w->pushGui(new GuiWifiSettings(w));
							}));
					}
					else
					{
						mWindow->pushGui(new GuiMsgBox(mWindow,
							"COULD NOT CONNECT TO \"" + ssid + "\".\n\n"
							"CHECK SSID, PASSWORD, AND SIGNAL.", "OK", nullptr));
					}
				},
				true, 0));
		},
		false, 1));
}

void GuiWifiSettings::disconnect()
{
	if (!mStatus.connected)
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, "NOT CURRENTLY CONNECTED.", "OK", nullptr));
		return;
	}

	mWindow->pushGui(new GuiMsgBox(mWindow,
		"DISCONNECT FROM \"" + mStatus.ssid + "\"?",
		"YES", [this]() {
			wpaDisconnect();
			mWindow->pushGui(new GuiMsgBox(mWindow, "WI-FI DISCONNECTED.", "OK",
				[this]() {
					Window* w = mWindow;
					delete this;
					w->pushGui(new GuiWifiSettings(w));
				}));
		},
		"NO", nullptr));
}

void GuiWifiSettings::clearSavedNetworks()
{
	mWindow->pushGui(new GuiMsgBox(mWindow,
		"ERASE ALL SAVED WI-FI NETWORKS?\n\nYOU WILL NEED TO RE-ENTER PASSWORDS.",
		"YES", [this]() {
			wpaClearAll();
			mWindow->pushGui(new GuiMsgBox(mWindow, "ALL SAVED NETWORKS ERASED.", "OK",
				[this]() {
					Window* w = mWindow;
					delete this;
					w->pushGui(new GuiWifiSettings(w));
				}));
		},
		"NO", nullptr));
}

// ============================================================================
//  wpa_cli helpers
// ============================================================================

std::string GuiWifiSettings::runCommand(const std::string& cmd)
{
	std::array<char, 4096> buffer;
	std::string result;
	FILE* pipe = popen(cmd.c_str(), "r");
	if (!pipe) return "";
	while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
		result += buffer.data();
	pclose(pipe);
	return result;
}

std::vector<GuiWifiSettings::NetworkInfo> GuiWifiSettings::parseScanResults(const std::string& output)
{
	std::vector<NetworkInfo> results;
	std::istringstream stream(output);
	std::string line;
	bool firstLine = true;

	while (std::getline(stream, line))
	{
		if (firstLine) { firstLine = false; continue; }
		if (line.empty() || line.substr(0, 5) == "bssid") continue;

		std::istringstream lineStream(line);
		std::string bssid, freq, signal, flags, ssid;

		if (!std::getline(lineStream, bssid, '\t')) continue;
		if (!std::getline(lineStream, freq, '\t')) continue;
		if (!std::getline(lineStream, signal, '\t')) continue;
		if (!std::getline(lineStream, flags, '\t')) continue;
		std::getline(lineStream, ssid);

		if (ssid.empty()) continue;
		if (ssid[0] == '\0' || ssid.substr(0, 4) == "\\x00") continue;

		bool duplicate = false;
		for (auto& existing : results)
		{
			if (existing.ssid == ssid)
			{
				duplicate = true;
				int sig = 0;
				try { sig = std::stoi(signal); } catch (...) {}
				if (sig > existing.signalLevel)
					existing.signalLevel = sig;
				break;
			}
		}
		if (duplicate) continue;

		NetworkInfo info;
		info.ssid = ssid;
		info.flags = flags;
		try { info.signalLevel = std::stoi(signal); } catch (...) { info.signalLevel = -100; }
		info.isSecured = (flags.find("WPA") != std::string::npos ||
		                  flags.find("WEP") != std::string::npos);

		results.push_back(info);
	}

	return results;
}

GuiWifiSettings::ConnectionStatus GuiWifiSettings::parseStatus(const std::string& output)
{
	ConnectionStatus status;
	status.connected = false;

	std::istringstream stream(output);
	std::string line;

	while (std::getline(stream, line))
	{
		size_t eq = line.find('=');
		if (eq == std::string::npos) continue;

		std::string key = line.substr(0, eq);
		std::string val = line.substr(eq + 1);

		if (key == "wpa_state" && val == "COMPLETED")
			status.connected = true;
		else if (key == "ssid")
			status.ssid = val;
	}

	return status;
}

bool GuiWifiSettings::wpaConnect(const std::string& ssid, const std::string& psk, bool hidden)
{
	std::string listOutput = runCommand("sudo wpa_cli -i wlan0 list_networks");
	std::istringstream listStream(listOutput);
	std::string listLine;
	while (std::getline(listStream, listLine))
	{
		if (listLine.find(ssid) != std::string::npos)
		{
			std::string id = listLine.substr(0, listLine.find('\t'));
			if (!id.empty() && id != "network")
				runCommand("sudo wpa_cli -i wlan0 remove_network " + id);
		}
	}

	std::string idStr = runCommand("sudo wpa_cli -i wlan0 add_network");
	while (!idStr.empty() && (idStr.back() == '\n' || idStr.back() == ' '))
		idStr.pop_back();

	if (idStr.empty() || idStr == "FAIL") return false;

	std::string setResult = runCommand("sudo wpa_cli -i wlan0 set_network " + idStr + " ssid '\"" + ssid + "\"'");
	if (setResult.find("FAIL") != std::string::npos) return false;

	if (hidden)
		runCommand("sudo wpa_cli -i wlan0 set_network " + idStr + " scan_ssid 1");

	if (!psk.empty())
	{
		setResult = runCommand("sudo wpa_cli -i wlan0 set_network " + idStr + " psk '\"" + psk + "\"'");
		if (setResult.find("FAIL") != std::string::npos) return false;
	}
	else
	{
		runCommand("sudo wpa_cli -i wlan0 set_network " + idStr + " key_mgmt NONE");
	}

	runCommand("sudo wpa_cli -i wlan0 enable_network " + idStr);
	runCommand("sudo wpa_cli -i wlan0 select_network " + idStr);
	runCommand("sudo wpa_cli -i wlan0 save_config");
	runCommand("sudo wpa_cli -i wlan0 reconfigure");

	for (int i = 0; i < 15; i++)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		mWindow->renderLoadingScreen("CONNECTING... (" + std::to_string(i + 1) + "s)");

		std::string st = runCommand("sudo wpa_cli -i wlan0 status");
		if (st.find("wpa_state=COMPLETED") != std::string::npos)
		{
			mWindow->renderLoadingScreen("WAITING FOR IP ADDRESS...");
			for (int j = 0; j < 5; j++)
			{
				std::this_thread::sleep_for(std::chrono::seconds(1));
				std::string ip = runCommand("ip -4 addr show wlan0 | awk '/inet /{print $2}'");
				if (!ip.empty() && ip != "\n")
					break;
			}
			return true;
		}
	}

	return false;
}

void GuiWifiSettings::wpaDisconnect()
{
	runCommand("sudo wpa_cli -i wlan0 disable_network all");
	runCommand("sudo wpa_cli -i wlan0 save_config");
}

void GuiWifiSettings::wpaClearAll()
{
	runCommand("sudo bash -c 'cat > /etc/wpa_supplicant/wpa_supplicant.conf << EOF\n"
	           "ctrl_interface=DIR=/var/run/wpa_supplicant GROUP=netdev\n"
	           "update_config=1\n"
	           "country=US\n"
	           "EOF'");
	runCommand("sudo chmod 600 /etc/wpa_supplicant/wpa_supplicant.conf");
	runCommand("sudo wpa_cli -i wlan0 reconfigure");
}

bool GuiWifiSettings::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value)
	{
		delete this;
		return true;
	}

	return mMenu.input(config, input);
}

std::vector<HelpPrompt> GuiWifiSettings::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}
