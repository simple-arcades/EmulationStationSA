#pragma once
#ifndef ES_APP_GUIS_GUI_BLUETOOTH_SETTINGS_H
#define ES_APP_GUIS_GUI_BLUETOOTH_SETTINGS_H

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include <string>
#include <vector>

class GuiBluetoothSettings : public GuiComponent
{
public:
	GuiBluetoothSettings(Window* window);

	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	MenuComponent mMenu;

	struct DeviceInfo
	{
		std::string mac;
		std::string name;
		bool paired;
		bool connected;
		bool trusted;
	};

	std::vector<DeviceInfo> mPairedDevices;
	bool mScanning;

	void buildList();
	void refreshPairedDevices();
	void pairDevice();
	void showDevicePicker(const std::vector<DeviceInfo>& devices);
	void offerNextDevice(const std::vector<DeviceInfo>& devices, int index);
	void connectDevice(const std::string& mac, const std::string& name);
	void disconnectDevice(const std::string& mac, const std::string& name);
	void removeDevice(const std::string& mac, const std::string& name);
	void restartBluetooth();

	// bluetoothctl helpers
	std::string runCommand(const std::string& cmd);
	std::vector<DeviceInfo> scanForNewDevices();
	bool btPair(const std::string& mac);
	bool btTrust(const std::string& mac);
	bool btConnect(const std::string& mac);
	bool btDisconnect(const std::string& mac);
	bool btRemove(const std::string& mac);
	bool isConnected(const std::string& mac);
	bool isPaired(const std::string& mac);
};

#endif // ES_APP_GUIS_GUI_BLUETOOTH_SETTINGS_H
