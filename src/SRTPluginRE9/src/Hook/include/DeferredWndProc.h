#ifndef SRTPLUGINRE9_DEFERREDWNDPROC_H
#define SRTPLUGINRE9_DEFERREDWNDPROC_H

#include <Windows.h>
#include <mutex>
#include <vector>

struct DeferredWndMsg
{
	HWND hWnd;
	UINT msg;
	WPARAM wParam;
	LPARAM lParam;
};

class DeferredWndProc
{
public:
	void Enqueue(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void ProcessQueue();

private:
	std::mutex m_Mutex;
	std::vector<DeferredWndMsg> m_Queue;
};

#endif
