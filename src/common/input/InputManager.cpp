// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2018 ergo720
// *
// *  All rights reserved
// *
// ******************************************************************
#if 1 // Reenable this when LLE USB actually works
#define _XBOXKRNL_DEFEXTRN_
#define LOG_PREFIX CXBXR_MODULE::SDL

// prevent name collisions
namespace xboxkrnl
{
	#include <xboxkrnl/xboxkrnl.h> // For PKINTERRUPT, etc.
};

#include <thread>
#include "SdlJoystick.h"
#include "XInputPad.h"
#include "InputManager.h"
#include "..\devices\usb\XidGamepad.h"
#include "core\kernel\exports\EmuKrnl.h" // For EmuLog
#include "EmuShared.h"


InputDeviceManager g_InputDeviceManager;

void InputDeviceManager::Initialize(bool GUImode)
{
	// Sdl::Init must be called last since it blocks when it succeeds
	std::unique_lock<std::mutex> lck(m_Mtx);
	m_bInitOK = true;

	m_PollingThread = std::thread([this, GUImode]() {
		XInput::Init(m_Mtx);
		Sdl::Init(m_Mtx, m_Cv, GUImode);
	});

	m_Cv.wait(lck, []() {
		return (Sdl::SdlInitStatus != Sdl::SDL_NOT_INIT) &&
			(XInput::XInputInitStatus != XInput::XINPUT_NOT_INIT);
	});

	if (Sdl::SdlInitStatus < 0 || XInput::XInputInitStatus < 0) {
		CxbxKrnlCleanupEx(CXBXR_MODULE::INIT, "Failed to initialize input subsystem! Consult debug log for more information");
	}
}

void InputDeviceManager::Shutdown()
{
	// Prevent additional devices from being added during shutdown.
	m_bInitOK = false;

	for (const auto& d : m_Devices)
	{
		// Set outputs to ZERO before destroying device
		for (InputDevice::Output* o : d->GetOutputs()) {
			o->SetState(0);
		}
	}
	m_Devices.clear();

	XInput::DeInit();
	Sdl::DeInit(m_PollingThread);
}

void InputDeviceManager::AddDevice(std::shared_ptr<InputDevice> Device)
{
	// If we are shutdown (or in process of shutting down) ignore this request:
	if (!m_bInitOK) {
		return;
	}

	//std::lock_guard<std::mutex> lk(m_devices_mutex);
	// Try to find an ID for this device
	int ID = 0;
	while (true)
	{
		const auto it =
			std::find_if(m_Devices.begin(), m_Devices.end(), [&Device, &ID](const auto& d) {
			return d->GetAPI() == Device->GetAPI() && d->GetDeviceName() == Device->GetDeviceName() &&
				d->GetId() == ID;
		});
		if (it == m_Devices.end()) { // no device with the same name with this ID, so we can use it
			break;
		}
		else {
			ID++;
		}
	}
	Device->SetId(ID);
	Device->SetPort(PORT_INVALID);
	Device->SetType(to_underlying(XBOX_INPUT_DEVICE::DEVICE_INVALID));

	EmuLog(LOG_LEVEL::INFO, "Added device: %s", Device->GetQualifiedName().c_str());
	m_Devices.emplace_back(std::move(Device));
}

void InputDeviceManager::RemoveDevice(std::function<bool(const InputDevice*)> Callback)
{
	//std::lock_guard<std::mutex> lk(m_devices_mutex);
	auto it = std::remove_if(m_Devices.begin(), m_Devices.end(), [&Callback](const auto& Device) {
		if (Callback(Device.get()))
		{
			EmuLog(LOG_LEVEL::INFO, "Removed device: %s", Device->GetQualifiedName().c_str());
			return true;
		}
		return false;
	});
	if (it != m_Devices.end()) {
		m_Devices.erase(it, m_Devices.end());
	}
}



void InputDeviceManager::ConnectDevice(int port, int type)
{
	switch (type)
	{
		case to_underlying(XBOX_INPUT_DEVICE::MS_CONTROLLER_DUKE): {
			if (ConstructHub(port)) {
				if (!ConstructXpadDuke(port)) {
					DestructHub(port);
				}
			}	
		}
		break;

		case to_underlying(XBOX_INPUT_DEVICE::MS_CONTROLLER_S):
		case to_underlying(XBOX_INPUT_DEVICE::LIGHT_GUN):
		case to_underlying(XBOX_INPUT_DEVICE::STEERING_WHEEL):
		case to_underlying(XBOX_INPUT_DEVICE::MEMORY_UNIT):
		case to_underlying(XBOX_INPUT_DEVICE::IR_DONGLE):
		case to_underlying(XBOX_INPUT_DEVICE::STEEL_BATTALION_CONTROLLER): {
			EmuLog(LOG_LEVEL::INFO, "Device type %d is not yet supported", type);
			return;
		}

		default:
			EmuLog(LOG_LEVEL::WARNING, "Attempted to attach an unknown device type (type was %d)", type);
			return;
	}

	BindHostDevice(port, type);
}

void InputDeviceManager::DisconnectDevice(int port)
{
	if (port > PORT_4 || port < PORT_1) {
		EmuLog(LOG_LEVEL::WARNING, "Invalid port number. The port was %d", port);
		return;
	}

	if (g_XidControllerObjArray[port - 1] != nullptr) {
		assert(g_HubObjArray[port - 1] != nullptr);
		int type = to_underlying(XBOX_INPUT_DEVICE::MS_CONTROLLER_DUKE); // hardcoded for now

		switch (type)
		{
			case to_underlying(XBOX_INPUT_DEVICE::MS_CONTROLLER_DUKE): {
				DestructHub(port);
				DestructXpadDuke(port);
			}
			break;

			case to_underlying(XBOX_INPUT_DEVICE::MS_CONTROLLER_S):
			case to_underlying(XBOX_INPUT_DEVICE::LIGHT_GUN):
			case to_underlying(XBOX_INPUT_DEVICE::STEERING_WHEEL):
			case to_underlying(XBOX_INPUT_DEVICE::MEMORY_UNIT):
			case to_underlying(XBOX_INPUT_DEVICE::IR_DONGLE):
			case to_underlying(XBOX_INPUT_DEVICE::STEEL_BATTALION_CONTROLLER): {
				EmuLog(LOG_LEVEL::INFO, "Device type %d is not yet supported", type);
			}
			break;

			default:
				EmuLog(LOG_LEVEL::WARNING, "Attempted to detach an unknown device type (type was %d)", type);
		}
	}
	else {
		EmuLog(LOG_LEVEL::WARNING, "Attempted to detach a device not attached to the emulated machine.");
	}
}

void InputDeviceManager::BindHostDevice(int port, int type)
{
	std::array<Settings::s_input, 4> input;
	std::array<std::vector<Settings::s_input_profiles>, to_underlying(XBOX_INPUT_DEVICE::DEVICE_MAX)> profile;
	int port_index = port - 1;

	g_EmuShared->GetInputSettings(&input);
	g_EmuShared->GetInputProfileSettings(&profile);

	std::string device_name(input[port_index].DeviceName);
	std::string profile_name(input[port_index].ProfileName);

	auto dev = FindDevice(std::string(device_name));
	if (dev != nullptr) {
		auto it_profile = std::find_if(profile[type].begin(), profile[type].end(), [&profile_name](const auto& profile) {
			if (profile.ProfileName == profile_name) {
				return true;
			}
			return false;
		});
		if (it_profile != profile[type].end()) {
			std::vector<InputDevice::IoControl*> controls = dev->GetIoControls();
			for (int index = 0; index < dev_num_buttons[type]; index++) {
				std::string dev_button(it_profile->ControlList[index]);
				auto it_control = std::find_if(controls.begin(), controls.end(), [&dev_button](const auto control) {
					if (control->GetName() == dev_button) {
						return true;
					}
					return false;
				});
				dev->SetBindings(index, (it_control != controls.end()) ? *it_control : nullptr);
			}
		}
		else {
			for (int index = 0; index < dev_num_buttons[type]; index++) {
				dev->SetBindings(index, nullptr);
			}
		}
		dev->SetPort(port);
		dev->SetType(type);
	}
}

bool InputDeviceManager::UpdateXboxPortInput(int Port, void* Buffer, int Direction)
{
	assert(Direction == DIRECTION_IN || Direction == DIRECTION_OUT);

	InputDevice* pDev = nullptr;
	for (auto dev_ptr : m_Devices) {
		if (dev_ptr.get()->GetPort() == Port) {
			pDev = dev_ptr.get();
			break;
		}
	}
	if (pDev == nullptr) {
		return false;
	}

	switch (pDev->GetType())
	{
		case to_underlying(XBOX_INPUT_DEVICE::MS_CONTROLLER_DUKE): {
			UpdateInputXpad(pDev, Buffer, Direction);
		}
		break;

		default: {
			EmuLog(LOG_LEVEL::WARNING, "Port %d has an unsupported device attached! The type was %d", Port, pDev->GetType());
			return false;
		}
	}

	return true;
}

void InputDeviceManager::UpdateInputXpad(InputDevice* Device, void* Buffer, int Direction)
{
	int i;
	std::map<int, InputDevice::IoControl*> bindings;

	if (Direction == DIRECTION_IN) {
		XpadInput* in_buf = reinterpret_cast<XpadInput*>(static_cast<uint8_t*>(Buffer) + 2);
		bindings = Device->GetBindings();
		Device->UpdateInput();
		for (i = GAMEPAD_A; i < GAMEPAD_DPAD_UP; i++) {
			const auto it = std::find_if(bindings.begin(), bindings.end(), [&i](const auto& d) {
				return i == d.first;
			});
			if (it != bindings.end()) {
				if (i == GAMEPAD_LEFT_TRIGGER || i == GAMEPAD_RIGHT_TRIGGER) {
					//in_buf->bAnalogButtons[i] = it->second->GetState(); //>> 7; likely to fix
				}
				else {
					// At the moment, we don't support intermediate values for the analog buttons, so report them as full pressed or released
					//in_buf->bAnalogButtons[i] = it->second->GetState() ? 0xFF : 0; // likely to fix
				}
			}
		}
		for (i = GAMEPAD_DPAD_UP; i < GAMEPAD_LEFT_THUMB_X; i++) {
			const auto it = std::find_if(bindings.begin(), bindings.end(), [&i](const auto& d) {
				return i == d.first;
			});
			if (it != bindings.end()) {
				//if (it->second->GetState()) { // likely to fix
				//	in_buf->wButtons |= BUTTON_MASK(i);
				//}
				//else {
				//	in_buf->wButtons &= ~(BUTTON_MASK(i));
				//}
			}
		}
		for (i = GAMEPAD_LEFT_THUMB_X; i < GAMEPAD_BUTTON_MAX; i++) {
			const auto it = std::find_if(bindings.begin(), bindings.end(), [&i](const auto& d) {
				return i == d.first;
			});
			if (it != bindings.end()) {
				switch (i)
				{
					case GAMEPAD_LEFT_THUMB_X: {
						//in_buf->sThumbLX = it->second->GetState(); // likely to fix
					}
					break;

					case GAMEPAD_LEFT_THUMB_Y: {
						//in_buf->sThumbLY = -it->second->GetState() - 1;
					}
					break;

					case GAMEPAD_RIGHT_THUMB_X: {
						//in_buf->sThumbRX = it->second->GetState();
					}
					break;

					case GAMEPAD_RIGHT_THUMB_Y: {
						//in_buf->sThumbRY = -it->second->GetState() - 1;
					}
					break;

					default: {
						// unreachable
					}
				}
			}
		}
	}
	else {
		// TODO
	}

}

void InputDeviceManager::RefreshDevices()
{
	std::unique_lock<std::mutex> lck(m_Mtx);
	Sdl::SdlPopulateOK = false;
	m_Devices.clear();
	XInput::PopulateDevices();
	Sdl::PopulateDevices();
	m_Cv.wait(lck, []() {
		return Sdl::SdlPopulateOK;
	});
}

std::vector<std::string> InputDeviceManager::GetDeviceList() const
{
	std::vector<std::string> dev_list;

	std::for_each(m_Devices.begin(), m_Devices.end(), [&dev_list](const auto& Device) {
		dev_list.push_back(Device.get()->GetQualifiedName());
	});

	return dev_list;
}

std::shared_ptr<InputDevice> InputDeviceManager::FindDevice(std::string& QualifiedName) const
{
	auto it = std::find_if(m_Devices.begin(), m_Devices.end(), [&QualifiedName](const auto& Device) {
		return QualifiedName == Device.get()->GetQualifiedName();
	});
	if (it != m_Devices.end()) {
		return *it;
	}
	else {
		return nullptr;
	}
}

#endif
