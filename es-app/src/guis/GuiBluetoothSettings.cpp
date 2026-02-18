// ============================================================================
//  GuiBluetoothSettings.cpp
//
//  Native EmulationStation Bluetooth manager.
//  Scans, pairs, connects, disconnects, removes devices via bluetoothctl.
//  Follows same UX patterns as GuiWifiSettings.
// ============================================================================
#include "guis/GuiBluetoothSettings.h"
#include "SAStyle.h"

#include "guis/GuiMsgBox.h"
#include "components/TextComponent.h"
#include "components/ComponentList.h"
#include "Window.h"

#include <cstdio>
#include <array>
#include <algorithm>
#include <sstream>
#include <thread>
#include <chrono>

GuiBluetoothSettings::GuiBluetoothSettings(Window* window)
	: GuiComponent(window),
	  mMenu(window, "BLUETOOTH SETTINGS"),
	  mScanning(false)
{
	addChild(&mMenu);

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
	mMenu.setPosition(
		(Renderer::getScreenWidth() - mMenu.getSize().x()) / 2,
		Renderer::getScreenHeight() * 0.15f);

	// Ensure bluetooth service is running and unblocked
	mWindow->renderLoadingScreen("STARTING BLUETOOTH...");
	runCommand("sudo systemctl start bluetooth.service");
	runCommand("sudo rfkill unblock bluetooth 2>/dev/null");

	refreshPairedDevices();
	buildList();
}

// ============================================================================
//  refreshPairedDevices
// ============================================================================

void GuiBluetoothSettings::refreshPairedDevices()
{
	mPairedDevices.clear();

	std::string output = runCommand("bluetoothctl devices Paired 2>/dev/null || bluetoothctl paired-devices 2>/dev/null");

	std::istringstream stream(output);
	std::string line;

	while (std::getline(stream, line))
	{
		if (line.substr(0, 7) != "Device ") continue;

		std::string rest = line.substr(7);
		size_t spacePos = rest.find(' ');
		if (spacePos == std::string::npos) continue;

		std::string mac = rest.substr(0, spacePos);
		std::string name = rest.substr(spacePos + 1);

		if (mac.empty() || name.empty()) continue;

		DeviceInfo dev;
		dev.mac = mac;
		dev.name = name;
		dev.paired = true;
		dev.connected = isConnected(mac);
		dev.trusted = true;

		mPairedDevices.push_back(dev);
	}
}

// ============================================================================
//  buildList
// ============================================================================

void GuiBluetoothSettings::buildList()
{
	int pairedCount = (int)mPairedDevices.size();
	int connectedCount = 0;
	for (const auto& dev : mPairedDevices)
		if (dev.connected) connectedCount++;

	std::string statusLine = "PAIRED: " + std::to_string(pairedCount) +
	                         "  CONNECTED: " + std::to_string(connectedCount);
	unsigned int statusColor = (connectedCount > 0) ? 0x44DD44FF : 0x888888FF;
	mMenu.setSubtitle(statusLine, statusColor);

	for (const auto& dev : mPairedDevices)
	{
		std::string info = dev.connected ? "CONNECTED" : "PAIRED";
		unsigned int color = dev.connected ? 0x44DD44FF : SA_TEXT_COLOR;

		auto infoText = std::make_shared<TextComponent>(
			mWindow, info, saFont(FONT_SIZE_SMALL), color);

		ComponentListRow row;
		auto nameText = std::make_shared<TextComponent>(
			mWindow, dev.name, saFont(FONT_SIZE_MEDIUM), color);
		row.addElement(nameText, true);
		row.addElement(infoText, false);

		std::string mac = dev.mac;
		std::string name = dev.name;
		bool connected = dev.connected;

		row.makeAcceptInputHandler([this, mac, name, connected]() {
			if (connected)
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					"\"" + name + "\" IS CONNECTED.\n\nWHAT WOULD YOU LIKE TO DO?",
					"DISCONNECT", [this, mac, name]() { disconnectDevice(mac, name); },
					"REMOVE", [this, mac, name]() { removeDevice(mac, name); },
					"BACK", nullptr));
			}
			else
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					"\"" + name + "\" IS PAIRED BUT NOT CONNECTED.\n\nWHAT WOULD YOU LIKE TO DO?",
					"CONNECT", [this, mac, name]() { connectDevice(mac, name); },
					"REMOVE", [this, mac, name]() { removeDevice(mac, name); },
					"BACK", nullptr));
			}
		});

		mMenu.addRow(row);
	}

	if (mPairedDevices.empty())
	{
		ComponentListRow row;
		auto noDevs = std::make_shared<TextComponent>(
			mWindow, "NO PAIRED CONTROLLERS",
			saFont(FONT_SIZE_MEDIUM), 0x888888FF);
		row.addElement(noDevs, true);
		mMenu.addRow(row);
	}

	mMenu.addButton("PAIR NEW", "pair", [this]() { pairDevice(); });
	mMenu.addButton("RESTART BT", "restart", [this]() { restartBluetooth(); });
	mMenu.addButton("BACK", "back", [this]() { delete this; });
}

// ============================================================================
//  pairDevice — instructions, scan, then offer discovered devices
// ============================================================================

void GuiBluetoothSettings::pairDevice()
{
	mWindow->pushGui(new GuiMsgBox(mWindow,
		"PAIR A NEW CONTROLLER\n\n"
		"1. TURN THE CONTROLLER ON\n"
		"2. PUT IT IN PAIRING MODE\n"
		"3. KEEP IT NEAR THE ARCADE\n\n"
		"PRESS OK WHEN READY TO SCAN.",
		"OK", [this]() {
			auto discovered = scanForNewDevices();

			if (discovered.empty())
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					"NO CONTROLLERS FOUND.\n\n"
					"MAKE SURE THE CONTROLLER IS ON\n"
					"AND IN PAIRING MODE, THEN TRY AGAIN.",
					"OK", nullptr));
				return;
			}

			if (discovered.size() == 1)
			{
				offerNextDevice(discovered, 0);
			}
			else
			{
				showDevicePicker(discovered);
			}
		},
		"CANCEL", nullptr));
}

// ============================================================================
//  showDevicePicker / offerNextDevice — walk through discovered devices
// ============================================================================

void GuiBluetoothSettings::showDevicePicker(const std::vector<DeviceInfo>& devices)
{
	offerNextDevice(devices, 0);
}

void GuiBluetoothSettings::offerNextDevice(const std::vector<DeviceInfo>& devices, int index)
{
	if (index >= (int)devices.size())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow,
			"NO MORE CONTROLLERS TO PAIR.", "OK", nullptr));
		return;
	}

	std::string mac = devices[index].mac;
	std::string name = devices[index].name;
	int remaining = (int)devices.size() - index - 1;

	std::string msg = "FOUND: \"" + name + "\"";
	if (remaining > 0)
		msg += "\n\n(" + std::to_string(remaining) + " MORE AVAILABLE)";
	msg += "\n\nPAIR THIS CONTROLLER?";

	std::string skipLabel = (remaining > 0) ? "SKIP" : "CANCEL";

	mWindow->pushGui(new GuiMsgBox(mWindow, msg,
		"YES", [this, mac, name]() {
			mWindow->renderLoadingScreen("PAIRING WITH \"" + name + "\"...");
			bool paired = btPair(mac);

			if (paired)
			{
				mWindow->renderLoadingScreen("TRUSTING \"" + name + "\"...");
				btTrust(mac);

				mWindow->renderLoadingScreen("CONNECTING TO \"" + name + "\"...");
				bool connected = btConnect(mac);

				std::string result = connected ?
					"CONTROLLER PAIRED AND CONNECTED!" :
					"CONTROLLER PAIRED BUT NOT CONNECTED.\n\n"
					"TRY CONNECTING FROM THE MAIN MENU.";

				mWindow->pushGui(new GuiMsgBox(mWindow, result, "OK",
					[this]() {
						Window* w = mWindow;
						delete this;
						w->pushGui(new GuiBluetoothSettings(w));
					}));
			}
			else
			{
				mWindow->pushGui(new GuiMsgBox(mWindow,
					"PAIRING FAILED.\n\n"
					"MAKE SURE THE CONTROLLER IS IN\n"
					"PAIRING MODE AND TRY AGAIN.",
					"OK", nullptr));
			}
		},
		skipLabel, [this, devices, index]() {
			offerNextDevice(devices, index + 1);
		}));
}

// ============================================================================
//  connectDevice
// ============================================================================

void GuiBluetoothSettings::connectDevice(const std::string& mac, const std::string& name)
{
	mWindow->renderLoadingScreen("CONNECTING TO \"" + name + "\"...");
	bool ok = btConnect(mac);

	std::string msg = ok ?
		"CONNECTED TO \"" + name + "\"!" :
		"COULD NOT CONNECT TO \"" + name + "\".\n\n"
		"MAKE SURE THE CONTROLLER IS ON AND NEARBY.";

	mWindow->pushGui(new GuiMsgBox(mWindow, msg, "OK",
		[this]() {
			Window* w = mWindow;
			delete this;
			w->pushGui(new GuiBluetoothSettings(w));
		}));
}

// ============================================================================
//  disconnectDevice
// ============================================================================

void GuiBluetoothSettings::disconnectDevice(const std::string& mac, const std::string& name)
{
	mWindow->renderLoadingScreen("DISCONNECTING \"" + name + "\"...");
	btDisconnect(mac);

	mWindow->pushGui(new GuiMsgBox(mWindow,
		"\"" + name + "\" DISCONNECTED.", "OK",
		[this]() {
			Window* w = mWindow;
			delete this;
			w->pushGui(new GuiBluetoothSettings(w));
		}));
}

// ============================================================================
//  removeDevice
// ============================================================================

void GuiBluetoothSettings::removeDevice(const std::string& mac, const std::string& name)
{
	mWindow->pushGui(new GuiMsgBox(mWindow,
		"REMOVE \"" + name + "\"?\n\n"
		"THIS WILL FORGET THE CONTROLLER.\n"
		"YOU WILL NEED TO PAIR IT AGAIN.",
		"YES", [this, mac, name]() {
			mWindow->renderLoadingScreen("REMOVING \"" + name + "\"...");
			btRemove(mac);

			mWindow->pushGui(new GuiMsgBox(mWindow,
				"\"" + name + "\" REMOVED.", "OK",
				[this]() {
					Window* w = mWindow;
					delete this;
					w->pushGui(new GuiBluetoothSettings(w));
				}));
		},
		"NO", nullptr));
}

// ============================================================================
//  restartBluetooth
// ============================================================================

void GuiBluetoothSettings::restartBluetooth()
{
	mWindow->pushGui(new GuiMsgBox(mWindow,
		"RESTART BLUETOOTH?\n\n"
		"THIS WILL TEMPORARILY DISCONNECT\n"
		"ALL CONTROLLERS.",
		"YES", [this]() {
			mWindow->renderLoadingScreen("RESTARTING BLUETOOTH...");
			runCommand("sudo systemctl restart bluetooth.service");
			std::this_thread::sleep_for(std::chrono::seconds(2));

			mWindow->pushGui(new GuiMsgBox(mWindow,
				"BLUETOOTH RESTARTED.", "OK",
				[this]() {
					Window* w = mWindow;
					delete this;
					w->pushGui(new GuiBluetoothSettings(w));
				}));
		},
		"NO", nullptr));
}

// ============================================================================
//  bluetoothctl command helpers
// ============================================================================

std::string GuiBluetoothSettings::runCommand(const std::string& cmd)
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

std::vector<GuiBluetoothSettings::DeviceInfo> GuiBluetoothSettings::scanForNewDevices()
{
	std::vector<DeviceInfo> discovered;

	mWindow->renderLoadingScreen("SCANNING FOR CONTROLLERS...");

	// Power on and prepare for pairing
	runCommand("sudo bluetoothctl power on");
	runCommand("sudo bluetoothctl discoverable on");
	runCommand("sudo bluetoothctl pairable on");
	runCommand("sudo bluetoothctl agent NoInputNoOutput");
	runCommand("sudo bluetoothctl default-agent");

	// Start scan in background
	runCommand("sudo bluetoothctl --timeout 1 scan on &");

	// Wait with loading screen updates
	for (int i = 0; i < 12; i++)
	{
		std::this_thread::sleep_for(std::chrono::seconds(1));
		mWindow->renderLoadingScreen("SCANNING FOR CONTROLLERS... (" + std::to_string(i + 1) + "s)");
	}

	// Stop scan
	runCommand("sudo bluetoothctl scan off 2>/dev/null");

	// Get all known devices
	std::string allDevices = runCommand("bluetoothctl devices");

	// Get paired devices to exclude
	std::string pairedOutput = runCommand("bluetoothctl devices Paired 2>/dev/null || bluetoothctl paired-devices 2>/dev/null");

	std::vector<std::string> pairedMacs;
	{
		std::istringstream stream(pairedOutput);
		std::string line;
		while (std::getline(stream, line))
		{
			if (line.substr(0, 7) != "Device ") continue;
			std::string rest = line.substr(7);
			size_t sp = rest.find(' ');
			if (sp != std::string::npos)
				pairedMacs.push_back(rest.substr(0, sp));
		}
	}

	// Parse discovered devices, filter out paired and unnamed
	std::istringstream stream(allDevices);
	std::string line;
	while (std::getline(stream, line))
	{
		if (line.substr(0, 7) != "Device ") continue;

		std::string rest = line.substr(7);
		size_t sp = rest.find(' ');
		if (sp == std::string::npos) continue;

		std::string mac = rest.substr(0, sp);
		std::string name = rest.substr(sp + 1);

		if (mac.empty() || name.empty()) continue;

		// Skip unnamed devices (name looks like a MAC address)
		if (name.find(':') != std::string::npos && name.length() == 17) continue;

		// Skip "Unknown" devices
		if (name.find("Unknown") == 0) continue;

		// Skip already paired
		bool alreadyPaired = false;
		for (const auto& pm : pairedMacs)
			if (pm == mac) { alreadyPaired = true; break; }
		if (alreadyPaired) continue;

		DeviceInfo dev;
		dev.mac = mac;
		dev.name = name;
		dev.paired = false;
		dev.connected = false;
		dev.trusted = false;

		discovered.push_back(dev);
	}

	return discovered;
}

bool GuiBluetoothSettings::btPair(const std::string& mac)
{
	std::string result = runCommand("sudo timeout 15 bluetoothctl pair " + mac + " 2>&1");

	if (result.find("Pairing successful") != std::string::npos)
		return true;

	return isPaired(mac);
}

bool GuiBluetoothSettings::btTrust(const std::string& mac)
{
	std::string result = runCommand("sudo bluetoothctl trust " + mac + " 2>&1");
	return (result.find("trust succeeded") != std::string::npos ||
	        result.find("Changing") != std::string::npos);
}

bool GuiBluetoothSettings::btConnect(const std::string& mac)
{
	std::string result = runCommand("sudo timeout 10 bluetoothctl connect " + mac + " 2>&1");

	if (result.find("Connection successful") != std::string::npos)
		return true;

	// Give it a moment and verify
	std::this_thread::sleep_for(std::chrono::seconds(2));
	return isConnected(mac);
}

bool GuiBluetoothSettings::btDisconnect(const std::string& mac)
{
	std::string result = runCommand("sudo bluetoothctl disconnect " + mac + " 2>&1");
	return (result.find("Successful") != std::string::npos || !isConnected(mac));
}

bool GuiBluetoothSettings::btRemove(const std::string& mac)
{
	std::string result = runCommand("sudo bluetoothctl remove " + mac + " 2>&1");
	return (result.find("Device has been removed") != std::string::npos ||
	        result.find("removed") != std::string::npos);
}

bool GuiBluetoothSettings::isConnected(const std::string& mac)
{
	std::string info = runCommand("bluetoothctl info " + mac + " 2>/dev/null");
	return (info.find("Connected: yes") != std::string::npos);
}

bool GuiBluetoothSettings::isPaired(const std::string& mac)
{
	std::string info = runCommand("bluetoothctl info " + mac + " 2>/dev/null");
	return (info.find("Paired: yes") != std::string::npos);
}

// ============================================================================
//  Input / Help
// ============================================================================

bool GuiBluetoothSettings::input(InputConfig* config, Input input)
{
	if (config->isMappedTo("b", input) && input.value)
	{
		delete this;
		return true;
	}

	return mMenu.input(config, input);
}

std::vector<HelpPrompt> GuiBluetoothSettings::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt("b", "back"));
	return prompts;
}
