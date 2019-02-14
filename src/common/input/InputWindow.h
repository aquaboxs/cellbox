#pragma once

#include "InputManager.h"
#include "common\Settings.hpp"

#define PROFILE_LOAD   1
#define PROFILE_SAVE   2
#define PROFILE_DELETE 3


class InputWindow
{
	public:
		typedef std::vector<Settings::s_input_profiles>::iterator ProfileIt;
		void Initialize(HWND hwnd, int port_num, int dev_type);
		~InputWindow();
		void UpdateDeviceList();
		void BindButton(int ControlID, std::string DeviceName, int ms);
		void BindXInput();
		void ClearBindings();
		void UpdateProfile(std::string& name, int command);


	private:
		InputDevice::Input* Detect(InputDevice* const Device, int ms);
		ProfileIt FindProfile(std::string& name);
		void LoadProfile(std::string& name);
		void SaveProfile(std::string& name);
		void DeleteProfile(std::string& name);
		void LoadDefaultProfile();
		void AssignBindingsToDevice();

		// xbox device under configuration
		EmuDevice* m_DeviceConfig;
		// handle of the window
		HWND m_hwnd_window;
		// device list control handle
		HWND m_hwnd_device_list;
		// type of the device
		int m_dev_type;
		// num of buttons of device under configuration
		int m_max_num_buttons;
		// xbox port number the host device attaches to (0 - 3)
		int m_port_num;
};

extern InputWindow* g_InputWindow;
