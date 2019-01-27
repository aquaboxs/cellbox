// This is an open source non-commercial project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;;
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   src->Common->Input->InputDevice.h
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

#ifndef INPUT_DEVICE
#define INPUT_DEVICE

#include <string>
#include <vector>
#include <atomic>
#include <map>

#define XBOX_PORT_INVALID  -1
#define XBOX_PORT_1         1
#define XBOX_PORT_2         2
#define XBOX_PORT_3         3
#define XBOX_PORT_4         4

#define DIRECTION_IN  0
#define DIRECTION_OUT 1


typedef double ControlState;

/* enum indicating the device type to attach to the virtual xbox */
typedef enum class _XBOX_INPUT_DEVICE : int {
	DEVICE_INVALID = -1,
	MS_CONTROLLER_DUKE,
	MS_CONTROLLER_S,
	LIGHT_GUN,
	STEERING_WHEEL,
	MEMORY_UNIT,
	IR_DONGLE,
	STEEL_BATTALION_CONTROLLER,
}
XBOX_INPUT_DEVICE;

/* Abstract class which represents a host device usable for input/output */
class InputDevice
{
	public:
		class Input
		{
			public:
				virtual std::string GetName() const = 0;
				virtual ControlState GetState() const = 0;
		};

		class Output
		{
			public:
				virtual std::string GetName() const = 0;
				virtual void SetState(ControlState state) = 0;
		};

		// retrieves the vector of input controls
		const std::vector<Input*>& GetInputs() const { return m_Inputs; }
		// retrieves the vector of output controls
		const std::vector<Output*>& GetOutputs() const { return m_Outputs; }
		// retrieves the map of input bindings
		const std::map<int, Input*>& GetBindings() const { return m_Bindings; }
		// sets a pair in the map of the input bindings
		void SetBindings(int XButton, Input* Control) { m_Bindings.emplace(XButton, Control); }
		// retrieves the full name of the device (API/ID/API-specific name)
		std::string GetQualifiedName() const;
		// retrieves the API-specific name of the device
		virtual std::string GetDeviceName() const = 0;
		// retrieves the API used to control this device
		virtual std::string GetAPI() const = 0;
		// updates the state of the device
		virtual void UpdateInput() {}
		// retrieves the ID of this device
		int GetId() const { return m_ID; }
		// sets the ID of this device
		void SetId(int ID) { m_ID = ID; }
		// retrieves the xbox port this device is attached to
		int GetXPort() const { return m_XboxPort; }
		// sets the xbox port this device is attached to
		void SetXPort(int Port) { m_XboxPort = Port; }
		// retrieves the xbox device type
		int GetXType() const { return m_Type; }
		// sets the xbox device type
		void SetXType(int Type) { m_Type = Type; }


	protected:
		// adds an input control to the device
		void AddInput(Input* const In);
		// adds an output control to the device
		void AddOutput(Output* const Out);

		class FullAnalogSurface : public Input
		{
			public:
				FullAnalogSurface(Input* Low, Input* High) : m_Low(*Low), m_High(*High) {}
				ControlState GetState() const override
				{
					return (1 + m_High.GetState() - m_Low.GetState()) / 2;
				}

				std::string GetName() const override { return m_Low.GetName() + *m_High.GetName().rbegin(); }

			private:
				Input& m_Low;
				Input& m_High;
		};

		void AddAnalogInputs(Input* Low, Input* High)
		{
			AddInput(Low);
			AddInput(High);
			AddInput(new FullAnalogSurface(Low, High));
			AddInput(new FullAnalogSurface(High, Low));
		}

	private:
		// arbitrary ID assigned by to the device
		int m_ID;
		// all the input controls detected and usable on this device
		std::vector<Input*> m_Inputs;
		// all the output controls detected and usable on this device
		std::vector<Output*> m_Outputs;
		// xbox port this device is attached to
		int m_XboxPort;
		// the emulated xbox device type
		int m_Type;
		// button bindings to the xbox device buttons
		std::map<int, Input*> m_Bindings;
};

#endif
