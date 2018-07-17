// host.cc
// 8/24/2013 jichi
// Branch IHF/main.cpp, rev 111
// 8/24/2013 TODO: Clean up this file

//#ifdef _MSC_VER
//# pragma warning(disable:4800) // C4800: forcing value to bool (performance warning)
//#endif // _MSC_VER

//#include "customfilter.h"
#include "growl.h"
#include "host.h"
#include "vnrhook/include/const.h"
#include "vnrhook/include/defs.h"
#include "vnrhook/include/types.h"
#include <commctrl.h>
#include <string>
#include "extensions/Extensions.h"

#define DEBUG "vnrhost/host.cc"

HANDLE preventDuplicationMutex;

HookManager* man;
HWND dummyWindow;
bool running;

namespace 
{ // unnamed

	void GetDebugPrivileges()
	{ // Artikash 5/19/2018: Is it just me or is this function 100% superfluous?
		HANDLE processToken;
		TOKEN_PRIVILEGES Privileges = {1, {0x14, 0, SE_PRIVILEGE_ENABLED}};

		OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &processToken);
		AdjustTokenPrivileges(processToken, FALSE, &Privileges, 0, nullptr, nullptr);
		CloseHandle(processToken);
	}

} // unnamed namespace

void CreateNewPipe();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID unused)
{
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hinstDLL);
		GetDebugPrivileges();
		// jichi 12/20/2013: Since I already have a GUI, I don't have to InitCommonControls()
		// Used by timers.
		// InitCommonControls();
		// jichi 8/24/2013: Create hidden window so that ITH can access timer and events
		dummyWindow = CreateWindowW(L"Button", L"InternalWindow", 0, 0, 0, 0, 0, 0, 0, hinstDLL, 0);
		break;
	case DLL_PROCESS_DETACH:
		if (::running)
			CloseHost();
		DestroyWindow(dummyWindow);
		break;
	default:
		break;
	}
	return true;
}

DLLEXPORT bool StartHost()
{
	preventDuplicationMutex = CreateMutexW(nullptr, TRUE, ITH_SERVER_MUTEX);
	if (GetLastError() == ERROR_ALREADY_EXISTS || ::running)
	{
		GROWL_WARN(L"I am sorry that this game is attached by some other VNR ><\nPlease restart the game and try again!");
		return false;
	}
	else
	{
		LoadExtensions();
		::running = true;
		::man = new HookManager;
		return true;
	}
}

DLLEXPORT void OpenHost()
{
	CreateNewPipe();
}

DLLEXPORT void CloseHost()
{
	if (::running)
	{
		::running = false;
		delete man;
		CloseHandle(preventDuplicationMutex);
	}
}

DLLEXPORT bool InjectProcessById(DWORD processId, DWORD timeout)
{
	if (processId == GetCurrentProcessId())
	{
		return false;
	}

	CloseHandle(CreateMutexW(nullptr, FALSE, (ITH_HOOKMAN_MUTEX_ + std::to_wstring(processId)).c_str()));
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		man->AddConsoleOutput(L"already locked");
		return false;
	}

	HMODULE textHooker = LoadLibraryExW(ITH_DLL, nullptr, DONT_RESOLVE_DLL_REFERENCES);
	wchar_t textHookerPath[MAX_PATH];
	unsigned int textHookerPathSize = GetModuleFileNameW(textHooker, textHookerPath, MAX_PATH) * 2 + 2;
	FreeLibrary(textHooker);

	if (HANDLE processHandle = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId))
		if (LPVOID remoteData = VirtualAllocEx(processHandle, nullptr, textHookerPathSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE))
			if (WriteProcessMemory(processHandle, remoteData, textHookerPath, textHookerPathSize, nullptr))
				if (HANDLE thread = CreateRemoteThread(processHandle, nullptr, 0, (LPTHREAD_START_ROUTINE)LoadLibraryW, remoteData, 0, nullptr))
				{
					WaitForSingleObject(thread, timeout);
					CloseHandle(thread);
					VirtualFreeEx(processHandle, remoteData, textHookerPathSize, MEM_RELEASE);
					CloseHandle(processHandle);
					return true;
				}
	
	man->AddConsoleOutput(L"couldn't inject dll");
	return false;
}

DLLEXPORT bool DetachProcessById(DWORD processId)
{
	DWORD command = HOST_COMMAND_DETACH;
	DWORD unused;
	return WriteFile(man->GetHostPipe(processId), &command, sizeof(command), &unused, nullptr);
}

DLLEXPORT void GetHostHookManager(HookManager** hookman)
{
	if (::running)
	{
		*hookman = man;
	}
}

DLLEXPORT DWORD InsertHook(DWORD pid, const HookParam *hp, std::string name)
{
  HANDLE commandPipe = man->GetHostPipe(pid);
  if (commandPipe == nullptr)
    return -1;

  BYTE buffer[PIPE_BUFFER_SIZE] = {};
  *(DWORD*)buffer = HOST_COMMAND_NEW_HOOK;
  *(HookParam*)(buffer + sizeof(DWORD)) = *hp;
  if (name.size()) strcpy((char*)buffer + sizeof(DWORD) + sizeof(HookParam), name.c_str());

  DWORD unused;
  WriteFile(commandPipe, buffer, sizeof(DWORD) + sizeof(HookParam) + name.size(), &unused, nullptr);
  return 0;
}

DLLEXPORT DWORD RemoveHook(DWORD pid, DWORD addr)
{
	HANDLE commandPipe = man->GetHostPipe(pid);
	if (commandPipe == nullptr)
		return -1;
    
	HANDLE hookRemovalEvent = CreateEventW(nullptr, TRUE, FALSE, ITH_REMOVEHOOK_EVENT);
	BYTE buffer[sizeof(DWORD) * 2] = {};
	*(DWORD*)buffer = HOST_COMMAND_REMOVE_HOOK;
	*(DWORD*)(buffer + sizeof(DWORD)) = addr;
  
	DWORD unused;
  WriteFile(commandPipe, buffer, sizeof(DWORD) * 2, &unused, nullptr);
  WaitForSingleObject(hookRemovalEvent, 1000);
  CloseHandle(hookRemovalEvent);
  man->RemoveSingleHook(pid, addr);
  return 0;
}

// EOF
