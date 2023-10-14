#ifndef MMVRIPPER_STUPIDMESSAGEHANDLER_HPP
#define MMVRIPPER_STUPIDMESSAGEHANDLER_HPP

#include <winsock2.h>
#include <windows.h>

class StupidMessageHandler
{
public:
	static void run();

private:
	static BOOL CALLBACK find_the_stupid_dialog(HWND hWnd, LPARAM lParam);
	static BOOL CALLBACK find_the_stupid_message(HWND hWnd, LPARAM lParam);
};

#endif
