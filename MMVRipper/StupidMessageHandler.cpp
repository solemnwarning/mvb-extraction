#include <stdio.h>

#include "StupidMessageHandler.hpp"

void StupidMessageHandler::run()
{
	HWND stupid_dialog = NULL;
	EnumWindows(&find_the_stupid_dialog, (LPARAM)(&stupid_dialog));

	if(stupid_dialog != NULL)
	{
		//SendMessage(stupid_dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);
		SendMessage(stupid_dialog, WM_COMMAND, MAKEWPARAM(IDCANCEL, BN_CLICKED), 0);
	}
}

BOOL CALLBACK StupidMessageHandler::find_the_stupid_dialog(HWND hWnd, LPARAM lParam)
{
	char window_text[128];
	if(GetWindowText(hWnd, window_text, sizeof(window_text)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	if(strcmp(window_text, "Navigator") == 0)
	{
		HWND stupid_msg = NULL;
		EnumChildWindows(hWnd, &find_the_stupid_message, (LPARAM)(&stupid_msg));

		if(stupid_msg != NULL)
		{
			HWND *dest_hwnd = (HWND*)(lParam);
			*dest_hwnd = hWnd;
			return FALSE;
		}
	}

	/* Keep searching. */
	return TRUE;
}

BOOL CALLBACK StupidMessageHandler::find_the_stupid_message(HWND hWnd, LPARAM lParam)
{
	TCHAR class_name[128];
	if(GetClassName(hWnd, class_name, sizeof(class_name)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	char window_text[128];
	if(GetWindowText(hWnd, window_text, sizeof(window_text)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	size_t len = strlen(window_text);

	if(strcmp(class_name, "Static") == 0
		&& len >= 22
		&& strncmp(window_text, "The ", 4) == 0
		&& strcmp((window_text + len - 22), " argument is not valid") == 0)
	{
		HWND *dest_hwnd = (HWND*)(lParam);
		*dest_hwnd = hWnd;
		return FALSE;
	}

	/* Keep searching. */
	return TRUE;
}
