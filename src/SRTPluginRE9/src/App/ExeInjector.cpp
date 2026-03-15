#pragma once

#include "ExeInjector.h"

#include <iostream>
#include <tlhelp32.h>

DWORD GetProcessIdByName(const TCHAR *name)
{
	DWORD pid = 0;

	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 process;
	process.dwSize = sizeof(process);
	if (Process32First(snapshot, &process))
	{
		do
		{
			if (!TSTRICMP(process.szExeFile, name))
			{
				pid = process.th32ProcessID;
				break;
			}
		} while (Process32Next(snapshot, &process));
	}
	CloseHandle(snapshot);

	return pid;
}

size_t GetStringSizeA(const std::string &string)
{
	return string.length() * sizeof(char) + sizeof(char);
}

size_t GetStringSizeW(const std::wstring &string)
{
	return string.length() * sizeof(wchar_t) + sizeof(wchar_t);
}

int wmain()
{
	std::wcout << L"Searching for running process... ";
	DWORD pid = GetProcessIdByName(L"re9.exe");
	if (pid == 0)
	{
		std::wcout << L"not found!" << std::endl;
		std::wcout << L"Failure, exiting..." << std::endl;
		return 1;
	}
	std::wcout << L"found! PID: " << pid << std::endl;

	HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD, FALSE, pid);
	{
		const std::wstring basePath = std::filesystem::current_path().native();
		const std::wstring dllPaths[] = {basePath + L"\\SRTPluginRE9.dll"};
		constexpr size_t dllPathsLength = std::size(dllPaths);

		std::wcout << L"Injecting DLLs..." << std::endl;
		for (size_t i = 0; i < dllPathsLength; ++i)
		{
			std::wcout << L"\tDLL: " << dllPaths[i] << " ";
			const size_t dllPathSize = GetStringSize(dllPaths[i]);
			LPVOID pDllPath = VirtualAllocEx(handle, nullptr, dllPathSize, MEM_COMMIT, PAGE_READWRITE);
			WriteProcessMemory(handle, pDllPath, dllPaths[i].c_str(), dllPathSize, nullptr);
			HANDLE hDllThread = CreateRemoteThread(handle, nullptr, 0, reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandle(L"Kernel32.dll"), "LoadLibraryW")), pDllPath, 0, 0);
			WaitForSingleObject(hDllThread, INFINITE);
			VirtualFreeEx(handle, pDllPath, 0, MEM_RELEASE);
			CloseHandle(hDllThread);
			std::wcout << L"complete." << std::endl;
		}
		std::wcout << L"DLL injection completed." << std::endl;
	}
	CloseHandle(handle);

	std::wcout << L"Success, exiting..." << std::endl;
	return 0;
}
