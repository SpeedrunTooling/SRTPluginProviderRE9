#include "DeferredWndProc.h"
#include <imgui_impl_win32.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

void DeferredWndProc::Enqueue(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Queue.push_back({hWnd, msg, wParam, lParam});
}

void DeferredWndProc::ProcessQueue()
{
	std::vector<DeferredWndMsg> local;
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		local.swap(m_Queue);
	}
	for (const auto &m : local)
	{
		ImGui_ImplWin32_WndProcHandler(m.hWnd, m.msg, m.wParam, m.lParam);
	}
}
