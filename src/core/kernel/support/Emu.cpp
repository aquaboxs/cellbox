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
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************

#define LOG_PREFIX CXBXR_MODULE::X86


#include <core\kernel\exports\xboxkrnl.h>
#include "core\kernel\init\CxbxKrnl.h"
#include "core/kernel/support/PatchRdtsc.hpp"
#include "Emu.h"
#include "devices\x86\EmuX86.h"
#include "EmuShared.h"
#include "core\hle\Intercept.hpp"
#include "CxbxDebugger.h"

#ifdef _DEBUG
#include <Dbghelp.h>
CRITICAL_SECTION dbgCritical;
#endif

namespace NtDll
{
#include "EmuNtDll.h"
};

// Including ntstatus.h will cause many macro redefinition warnings because of the previous definitions from winnt.h, so we define them here ourselves instead
#define STATUS_SUCCESS              ((NTSTATUS) 0x00000000L)
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS) 0xC0000004L)

// Global Variable(s)
volatile thread_local  bool    g_bEmuException = false;
static thread_local bool bOverrideEmuException;
bool g_DisablePixelShaders = false;
bool g_UseAllCores = false;
bool g_SkipRdtscPatching = false;

// Delta added to host SystemTime, used in KiClockIsr and KeSetSystemTime
// This shouldn't need to be atomic, but because raising the IRQL to high lv in KeSetSystemTime doesn't really stop KiClockIsr from running,
// we need it for now to prevent reading a corrupted value while KeSetSystemTime is in the middle of updating it
std::atomic_int64_t HostSystemTimeDelta(0);

// Static Function(s)
static int ExitException(LPEXCEPTION_POINTERS e);

std::string EIPToString(xbox::addr_xt EIP)
{
	char buffer[256];
	
	if (EIP < g_SystemMaxMemory) {
		int symbolOffset = 0;
		std::string symbolName = GetDetectedSymbolName(EIP, &symbolOffset);
		sprintf(buffer, "0x%.08X(=%s+0x%x)", EIP, symbolName.c_str(), symbolOffset);
	} else {
		sprintf(buffer, "0x%.08X", EIP);
	}
	
	std::string result = buffer;

	return result;
}

void EmuExceptionPrintDebugInformation(LPEXCEPTION_POINTERS e, bool IsBreakpointException)
{
	// print debug information
	{
		if (IsBreakpointException)
			printf("[0x%.4X] MAIN: Received Breakpoint Exception (int 3)\n", GetCurrentThreadId());
		else
			printf("[0x%.4X] MAIN: Received Exception (Code := 0x%.8X)\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode);

		printf("\n"
			" EIP := %s\n"
			" EFL := 0x%.08X\n"
			" EAX := 0x%.08X EBX := 0x%.08X ECX := 0x%.08X EDX := 0x%.08X\n"
			" ESI := 0x%.08X EDI := 0x%.08X ESP := 0x%.08X EBP := 0x%.08X\n"
			" CR2 := 0x%.08X\n"
			"\n",
			EIPToString(e->ContextRecord->Eip).c_str(),
			e->ContextRecord->EFlags,
			e->ContextRecord->Eax, e->ContextRecord->Ebx, e->ContextRecord->Ecx, e->ContextRecord->Edx,
			e->ContextRecord->Esi, e->ContextRecord->Edi, e->ContextRecord->Esp, e->ContextRecord->Ebp,
			e->ContextRecord->Dr2);

#ifdef _DEBUG
		CONTEXT Context = *(e->ContextRecord);
		EmuPrintStackTrace(&Context);
#endif
	}

	fflush(stdout);
}

void EmuExceptionExitProcess()
{
	printf("[0x%.4X] MAIN: Aborting Emulation\n", GetCurrentThreadId());
	fflush(stdout);

	if (CxbxKrnl_hEmuParent != NULL)
		SendMessage(CxbxKrnl_hEmuParent, WM_PARENTNOTIFY, WM_DESTROY, 0);

	EmuShared::Cleanup();
	ExitProcess(1);
}

bool EmuExceptionBreakpointAsk(LPEXCEPTION_POINTERS e)
{
	EmuExceptionPrintDebugInformation(e, /*IsBreakpointException=*/true);

	// We can skip Xbox as long as they are logged so we know about them
	// There's no need to prevent emulation, we can just pretend we have a debugger attached and continue
	// This is because some games (such as Crash Bandicoot) spam exceptions;
	e->ContextRecord->Eip += EmuX86_OpcodeSize((uint8_t*)e->ContextRecord->Eip); // Skip 1 instruction
	return true;

#if 1
#else
	char buffer[256];
	sprintf(buffer,
		"Received Breakpoint Exception (int 3) @ EIP := %s\n"
		"\n"
		"  Press Abort to terminate emulation.\n"
		"  Press Retry to debug.\n"
		"  Press Ignore to continue emulation.",
		EIPToString(e->ContextRecord->Eip).c_str());

	PopupReturn ret = PopupWarningEx(g_hEmuWindow, PopupButtons::AbortRetryIgnore, PopupReturn::Ignore, buffer);
	if (ret == PopupReturn::Abort)
	{
		EmuExceptionExitProcess();
	}
	else if (ret == PopupReturn::Ignore)
	{
		printf("[0x%.4X] MAIN: Ignored Breakpoint Exception\n", GetCurrentThreadId());
		fflush(stdout);

		e->ContextRecord->Eip += EmuX86_OpcodeSize((uint8_t*)e->ContextRecord->Eip); // Skip instruction size bytes

		return true;
	}

	return false;
#endif
}

bool EmuExceptionNonBreakpointUnhandledShow(LPEXCEPTION_POINTERS e)
{
	EmuExceptionPrintDebugInformation(e, /*IsBreakpointException=*/false);

	auto result = PopupFatalEx(nullptr, PopupButtons::AbortRetryIgnore, PopupReturn::Abort,
		"  The running xbe has encountered an unhandled exception (Code := 0x%.8X) at address 0x%.08X.\n"
		"\n"
		"  Press \"Abort\" to terminate emulation.\n"
		"  Press \"Retry\" to debug.\n"
		"  Press \"Ignore\" to attempt to continue emulation.",
		e->ExceptionRecord->ExceptionCode, e->ContextRecord->Eip);

	if (result == PopupReturn::Abort) {
		EmuExceptionExitProcess();
	} else if (result == PopupReturn::Ignore) {
		e->ContextRecord->Eip += EmuX86_OpcodeSize((uint8_t*)e->ContextRecord->Eip); // Skip 1 instruction
		return true;
	}

	return false;
}

// Returns weither the given address is part of an Xbox managed memory region
bool IsXboxCodeAddress(xbox::addr_xt addr)
{
	// TODO : Replace the following with a (fast) check weither
	// the given address lies in xbox allocated virtual memory,
	// for example by g_VMManager.CheckConflictingVMA(addr, 0).
	return (addr >= XBE_IMAGE_BASE) && (addr <= XBE_MAX_VA);
	// Note : Not IS_USER_ADDRESS(), that would include host DLL code
}

#include "distorm.h"
bool EmuX86_DecodeOpcode(const uint8_t* Eip, _DInst& info);
void EmuX86_DistormLogInstruction(const uint8_t* Eip, _DInst& info, LOG_LEVEL log_level);
bool genericException(EXCEPTION_POINTERS *e)
{
	_DInst info;
	if (EmuX86_DecodeOpcode((uint8_t*)e->ContextRecord->Eip, info)) {
		EmuX86_DistormLogInstruction((uint8_t*)e->ContextRecord->Eip, info, LOG_LEVEL::FATAL);
	}
	// Try to report this exception to the debugger, which may allow handling of this exception
	if (CxbxDebugger::CanReport()) {
		bool DebuggerHandled = false;
		CxbxDebugger::ReportAndHandleException(e->ExceptionRecord, DebuggerHandled);
		if (!DebuggerHandled) {
			// Kill the process immediately without the Cxbx notifier
			EmuExceptionExitProcess();
		}

		// Bypass exception
		return false;
	}
	else {
		// notify user
		return EmuExceptionNonBreakpointUnhandledShow(e);
	}

	return false;
}

bool IsRdtscInstruction(xbox::addr_xt addr); // Implemented in CxbxKrnl.cpp
void EmuX86_Opcode_RDTSC(EXCEPTION_POINTERS *e); // Implemented in EmuX86.cpp
bool lleTryHandleException(EXCEPTION_POINTERS *e)
{
	// Initalize local thread variable
	bOverrideEmuException = false;

	// Only handle exceptions which originate from Xbox code
	if (!IsXboxCodeAddress(e->ContextRecord->Eip)) {
		return false;
	}

	// Make sure access-violations reach EmuX86_DecodeException() as soon as possible
	if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
		switch (e->ExceptionRecord->ExceptionCode) {
		case STATUS_PRIVILEGED_INSTRUCTION:
			// Check if this exception came from rdtsc 
			if (IsRdtscInstruction(e->ContextRecord->Eip)) {
				// If so, use a return value that updates with Xbox frequency;
				EmuX86_Opcode_RDTSC(e);
				e->ContextRecord->Eip += 2; // Skip OPCODE_PATCH_RDTSC 0xEF 0x90 (OUT DX, EAX; NOP)
				return true;
			}
			break;
		case STATUS_BREAKPOINT:
			// Pass breakpoint down to EmuException since VEH doesn't have call stack viewable.
			return false;
		default:
			// Skip past CxbxDebugger-specific exceptions thrown when an unsupported was attached (ie Visual Studio)
			if (CxbxDebugger::IsDebuggerException(e->ExceptionRecord->ExceptionCode)) {
				return true;
			}
		}
	}

	// Pass the exception to our X86 implementation, to try and execute the failing instruction
	if (EmuX86_DecodeException(e)) {
		// We're allowed to continue :
		return true;
	}

	// We do not need EmuException to handle it again.
	bOverrideEmuException = true;

	// Unhandled exception :
	return false;
}

// Only for LLE emulation coding (to help performance a little bit better)
long WINAPI lleException(EXCEPTION_POINTERS *e)
{
	g_bEmuException = true;
	long result = lleTryHandleException(e) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
	g_bEmuException = false;
	return result;
}

// Only for Cxbx emulation coding (to catch all of last resort exception may occur.)
bool EmuTryHandleException(EXCEPTION_POINTERS *e)
{

	// Check if lle exception is already called first before emu exception.
	if (bOverrideEmuException) {
		return genericException(e);
	}

	if (e->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
		bool isInt2Dh = *(uint16_t*)(e->ContextRecord->Eip - 2) == 0x2DCD;

		switch (e->ExceptionRecord->ExceptionCode) {
		case STATUS_BREAKPOINT:
			// First, check if the breakpoint was prefixed with int 0x2dh, if so, it's NOT a breakpoint, but a Debugger Command!
			// Note: Because of a Windows quirk, this does NOT work when a debugger is attached, we'll get an UNCATCHABLE breakpoint instead
			// But at least this gives us a working implementaton of Xbox DbgPrint when we don't attach a debugger
			if (isInt2Dh ){
				e->ContextRecord->Eip += 1;

				// Now perform the command (stored in EAX)
				switch (e->ContextRecord->Eax) {
					case 1: // DEBUG_PRINT
						// In this case, ECX should point to an ANSI String
						printf("DEBUG_PRINT: %s\n", ((xbox::PANSI_STRING)e->ContextRecord->Ecx)->Buffer);
						break;
					default:
						printf("Unhandled Debug Command: int 2Dh, EAX = %d", e->ContextRecord->Eip);
				}

				return true;
			}

			// Otherwise, let the user choose between continue or break
			return EmuExceptionBreakpointAsk(e);
		default:
			printf("Unhandled Debug Command: int 2Dh, EAX = %d", e->ContextRecord->Eip);
			// Skip past CxbxDebugger-specific exceptions thrown when an unsupported was attached (ie Visual Studio)
			if (CxbxDebugger::IsDebuggerException(e->ExceptionRecord->ExceptionCode)) {
				return true;
			}
		}
	}

	return genericException(e);
}

long WINAPI EmuException(struct _EXCEPTION_POINTERS* e)
{
	g_bEmuException = true;
	long result = EmuTryHandleException(e) ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
	g_bEmuException = false;
	return result;
}

// exception handle for that tough final exit :)
// TODO: We might just well as delete this, duplicate of EmuExceptionNonBreakpointUnhandledShow
int ExitException(LPEXCEPTION_POINTERS e)
{
    static int count = 0;

	// debug information
    printf("[0x%.4X] MAIN: * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());
    printf("[0x%.4X] MAIN: Received Exception [0x%.8X]@%s\n", GetCurrentThreadId(), e->ExceptionRecord->ExceptionCode, EIPToString(e->ContextRecord->Eip).c_str());
    printf("[0x%.4X] MAIN: * * * * * EXCEPTION * * * * *\n", GetCurrentThreadId());

    fflush(stdout);

    PopupFatal(nullptr, "Warning: Could not safely terminate process!");

    count++;

    if(count > 1)
    {
        PopupFatal(nullptr, "Warning: Multiple Problems!");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    if(CxbxKrnl_hEmuParent != NULL)
        SendMessage(CxbxKrnl_hEmuParent, WM_PARENTNOTIFY, WM_DESTROY, 0);

    ExitProcess(1);

    return EXCEPTION_CONTINUE_SEARCH;
}

// Exception Mananger class; Any custom exceptions must be above this line.
ExceptionManager *g_ExceptionManager = nullptr;

ExceptionManager::ExceptionManager()
{
	accept_request = true;
	// Last call plus show exception error than terminate early.
#ifdef _MSC_VER // Windows' C++ exception is using SEH, we cannot use VEH for error reporter system.
	(void)SetUnhandledExceptionFilter(EmuException);
#else // Untested for other platforms, may will behave as expected.
	AddVEH(0, EmuException, true);
#endif
}

ExceptionManager::~ExceptionManager()
{
	for (auto i_handle : veh_handles) {
		(void)RemoveVectoredExceptionHandler(i_handle);
	}
	veh_handles.clear();
#ifdef _MSC_VER // Windows' C++ exception is using SEH, we cannot use VEH for error reporter system.
	(void)SetUnhandledExceptionFilter(nullptr);
#endif
}

// Require to be set right before we call xbe's entry point.
void ExceptionManager::EmuX86_Init()
{
	accept_request = false; // Do not allow add VEH during emulation.
	AddVEH(1, lleException, true); // Front line call
}

bool ExceptionManager::AddVEH(unsigned long first, PVECTORED_EXCEPTION_HANDLER veh_handler)
{
	return AddVEH(first, veh_handler, false);
}

bool ExceptionManager::AddVEH(unsigned long first, PVECTORED_EXCEPTION_HANDLER veh_handler, bool override_request)
{
	bool isSuccess = false;
	if (accept_request || override_request) {
		void* veh_handle = AddVectoredExceptionHandler(first, veh_handler);
		if (veh_handle) {
			veh_handles.push_back(veh_handle);
			isSuccess = true;
		}
	}
	return isSuccess;
}

#ifdef _DEBUG
// print call stack trace
void EmuPrintStackTrace(PCONTEXT ContextRecord)
{
    static int const STACK_MAX     = 16;
    static int const SYMBOL_MAXLEN = 64;

	// TODO: Figure out why this causes a loop of Exceptions until the process dies
    //EnterCriticalSection(&dbgCritical);

    IMAGEHLP_MODULE64 module = { sizeof(IMAGEHLP_MODULE) };

    BOOL fSymInitialized = SymInitialize(g_CurrentProcessHandle, NULL, TRUE);

    STACKFRAME64 frame = { sizeof(STACKFRAME64) };
    frame.AddrPC.Offset    = ContextRecord->Eip;
    frame.AddrPC.Mode      = AddrModeFlat;
    frame.AddrFrame.Offset = ContextRecord->Ebp;
    frame.AddrFrame.Mode   = AddrModeFlat;
    frame.AddrStack.Offset = ContextRecord->Esp;
    frame.AddrStack.Mode   = AddrModeFlat;

    for(int i = 0; i < STACK_MAX; i++)
    {
        if(!StackWalk64(
            IMAGE_FILE_MACHINE_I386,
			g_CurrentProcessHandle,
            GetCurrentThread(),
            &frame,
            ContextRecord,
            NULL,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            NULL))
            break;

        SymGetModuleInfo64(g_CurrentProcessHandle, frame.AddrPC.Offset, &module);
        if(module.ModuleName)
            printf(" %2d: %-8s 0x%.08llX", i, module.ModuleName, frame.AddrPC.Offset);
        else
            printf(" %2d: %8c 0x%.08llX", i, ' ', frame.AddrPC.Offset);

		BYTE symbol[sizeof(SYMBOL_INFO) + SYMBOL_MAXLEN] = { 0 };
		std::string symbolName = "";
        DWORD64 dwDisplacement = 0;

		if (fSymInitialized)
		{
			PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)&symbol;
			pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO) + SYMBOL_MAXLEN - 1;
			pSymbol->MaxNameLen = SYMBOL_MAXLEN;
			if (SymFromAddr(g_CurrentProcessHandle, frame.AddrPC.Offset, &dwDisplacement, pSymbol))
				symbolName = pSymbol->Name;
		}

		if (symbolName.empty()) {
			// Try getting a symbol name from the HLE cache :
			int symbolOffset = 0;

			symbolName = GetDetectedSymbolName((xbox::addr_xt)frame.AddrPC.Offset, &symbolOffset);
			if (symbolOffset < 1000)
				dwDisplacement = (DWORD64)symbolOffset;
			else
				symbolName = "";
        }

		if (!symbolName.empty())
			printf(" %s+0x%.04llX\n", symbolName.c_str(), dwDisplacement);
        else
            printf("\n");
    }

    printf("\n");

    if(fSymInitialized)
        SymCleanup(g_CurrentProcessHandle);

    // LeaveCriticalSection(&dbgCritical);
}
#endif

static std::unique_ptr<char[]> Capture()
{
	NtDll::ULONG BufferSize = PAGE_SIZE;
	NtDll::ULONG SizeNeeded = 0;
	// We can't know how many processes are currently running on the user's system, so this iterates until our buffer is large enough to hold all of them
	while (true) {
		std::unique_ptr<char[]> Buffer(new char[BufferSize]);

		NtDll::NTSTATUS Status = NtDll::NtQuerySystemInformation(NtDll::SystemProcessInformation, Buffer.get(), BufferSize, &SizeNeeded);
		if (Status == STATUS_INFO_LENGTH_MISMATCH) {
			// More processes could have started in the meantime, so increase the buffer size
			BufferSize = SizeNeeded + PAGE_SIZE;
			continue;
		}
		else if (Status == STATUS_SUCCESS) {
			return Buffer;
		}
		else {
			CxbxrAbort("NtDll::NtQuerySystemInformation failed! The returned NTSTATUS was 0x%X", Status);
		}
	}
}

static NtDll::SYSTEM_PROCESS *FindProcessByPid(PVOID ProcessesBuffer, NtDll::DWORD Pid)
{
	NtDll::SYSTEM_PROCESS *Proc = (NtDll::SYSTEM_PROCESS *)ProcessesBuffer;
	while (true) {
		if ((DWORD)(DWORD_PTR)Proc->UniqueProcessId == Pid) {
			return Proc;
		}

		if (!Proc->NextEntryOffset) {
			return nullptr;
		}

		Proc = (NtDll::SYSTEM_PROCESS *)((NtDll::BYTE *)Proc + Proc->NextEntryOffset);
	}
}

static NtDll::SYSTEM_THREAD *FindThreadByTid(NtDll::SYSTEM_PROCESS *Proc, NtDll::DWORD Tid)
{
	NtDll::SYSTEM_THREAD *Thread = (NtDll::SYSTEM_THREAD *)((BYTE *)Proc + sizeof(NtDll::SYSTEM_PROCESS));
	for (NtDll::ULONG i = 0; i < Proc->ThreadCount; ++i) {
		if (Thread->ClientID.UniqueThread == (NtDll::HANDLE)(NtDll::DWORD_PTR)Tid) {
			return Thread;
		}

		++Thread;
	}

	return nullptr;
}

bool IsThreadSuspended(DWORD Tid, DWORD Pid)
{
	// NOTE: we could find the suspend state of a thread much more quickly by passing the class ThreadSuspendCount to NtDll::NtQueryInformationThread,
	// but unfortunately that class has only been added since Windows 8.1

	std::unique_ptr<char[]> Buffer = Capture();

	NtDll::SYSTEM_PROCESS *Proc = FindProcessByPid(Buffer.get(), Pid);
	if (!Proc) {
		EmuLog(LOG_LEVEL::ERROR2, "The process does not exist");
		return false;
	}

	NtDll::SYSTEM_THREAD *Thread = FindThreadByTid(Proc, Tid);
	if (!Thread) {
		EmuLog(LOG_LEVEL::ERROR2, "The thread does not exist");
		return false;
	}

	return ((Thread->ThreadState == NtDll::Waiting) && (Thread->WaitReason == NtDll::Suspended));
}
