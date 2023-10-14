#include <assert.h>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#include <winsock2.h>
#include <windows.h>

#include "DummyPrinter.hpp"
#include "StupidMessageHandler.hpp"

#define DUMP_INDEX_SCREENSHOTS
#define DUMP_PAGE_TEXT
// #define DUMP_PAGE_PRINTOUT

static BOOL CALLBACK find_popnav(HWND hWnd, LPARAM lParam);
static BOOL CALLBACK find_listbox(HWND hWnd, LPARAM lParam);
static BOOL CALLBACK find_windoc(HWND hWnd, LPARAM lParam);
static std::vector<DWORD> get_listbox_items_data(HWND listbox);
static void reset_listbox_state(HWND listbox);
static void walk_listbox(HWND listbox, std::set<DWORD> *seen_items, int depth);
static void capture_rect(HWND hWnd, RECT rect, const char *filename);
static void capture_cleanup();

static int find_menu_item_id(HWND hWnd, const char *name)
{
	HMENU windoc_menu = GetMenu(hWnd);

	int i, j;

	for(i = 0; i < 10; ++i)
	{
		HMENU submenu = GetSubMenu(windoc_menu, i);

		if(submenu != NULL)
		{
			int num_menu_items = GetMenuItemCount(submenu);

			for(j = 0; j < num_menu_items; ++j)
			{
				char item_name[32];
				int in_len = GetMenuString(submenu, j, item_name, sizeof(item_name), MF_BYPOSITION);

				if(in_len > 0 && strcmp(item_name, name) == 0)
				{
					return GetMenuItemID(submenu, j);
				}
			}
		}
	}

	return -1;
}

DummyPrinter *print_server;

HWND windoc = NULL;

int main(int argc, char **argv)
{
	EnumWindows(&find_windoc, (LPARAM)(&windoc));

	if(windoc == NULL)
	{
		abort();
	}

	print_server = new DummyPrinter(9100);

	HWND popnav = NULL;
	EnumWindows(&find_popnav, (LPARAM)(&popnav));

	if(popnav == NULL)
	{
		fprintf(stderr, "Unable to find PopNav (Index) window\n");
		return 1;
	}

	HWND listbox = NULL;
	EnumChildWindows(popnav, &find_listbox, (LPARAM)(&listbox));

	if(listbox == NULL)
	{
		fprintf(stderr, "Unable to find ListBox in PopNav (Index) window\n");
		return 1;
	}

	/* Bring PopNav to the front so it actually draws and we can capture it. */
	BringWindowToTop(popnav);

	DWORD pid;
	GetWindowThreadProcessId(listbox, &pid);

	reset_listbox_state(listbox);

	std::set<DWORD> seen_items;
	walk_listbox(listbox, &seen_items, 0);

	delete print_server;
	
	capture_cleanup();

	return 0;
}

static BOOL CALLBACK find_popnav(HWND hWnd, LPARAM lParam)
{
	TCHAR class_name[128];
	if(GetClassName(hWnd, class_name, sizeof(class_name)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	if(strcmp(class_name, "PopNav") == 0)
	{
		HWND *dest_hwnd = (HWND*)(lParam);
		*dest_hwnd = hWnd;
		return FALSE;
	}

	/* Keep searching. */
	return TRUE;
}

static BOOL CALLBACK find_listbox(HWND hWnd, LPARAM lParam)
{
	TCHAR class_name[128];
	if(GetClassName(hWnd, class_name, sizeof(class_name)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	if(strcmp(class_name, "ListBox") == 0)
	{
		HWND *dest_hwnd = (HWND*)(lParam);
		*dest_hwnd = hWnd;
		return FALSE;
	}

	/* Keep searching. */
	return TRUE;
}

static BOOL CALLBACK find_windoc(HWND hWnd, LPARAM lParam)
{
	TCHAR class_name[128];
	if(GetClassName(hWnd, class_name, sizeof(class_name)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	if(strcmp(class_name, "MS_WINDOC") == 0)
	{
		HWND *dest_hwnd = (HWND*)(lParam);
		*dest_hwnd = hWnd;
		return FALSE;
	}

	/* Keep searching. */
	return TRUE;
}

static BOOL CALLBACK find_copy_dialog(HWND hWnd, LPARAM lParam)
{
	char window_text[128];
	if(GetWindowText(hWnd, window_text, sizeof(window_text)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	HWND parent = GetParent(hWnd);

	if(strcmp(window_text, "Copy") == 0 && parent == windoc)
	{
		HWND *dest_hwnd = (HWND*)(lParam);
		*dest_hwnd = hWnd;
		return FALSE;
	}

	/* Keep searching. */
	return TRUE;
}

static BOOL CALLBACK find_copy_edit(HWND hWnd, LPARAM lParam)
{
	TCHAR class_name[128];
	if(GetClassName(hWnd, class_name, sizeof(class_name)) == 0)
	{
		/* Keep searching. */
		return TRUE;
	}

	if(strcmp(class_name, "Edit") == 0)
	{
		HWND *dest_hwnd = (HWND*)(lParam);
		*dest_hwnd = hWnd;
		return FALSE;
	}

	/* Keep searching. */
	return TRUE;
}

static std::vector<DWORD> get_listbox_items_data(HWND listbox)
{
	DWORD lb_count = SendMessage(listbox, LB_GETCOUNT, 0, 0);
	
	std::vector<DWORD> items_data;
	items_data.reserve(lb_count);

	for(DWORD i = 0; i < lb_count; ++i)
	{
		DWORD data = SendMessage(listbox, LB_GETITEMDATA, i, 0);
		items_data.push_back(data);
	}

	return items_data;
}

static void reset_listbox_state(HWND listbox)
{
	SendMessage(listbox, WM_KEYDOWN, VK_END, 0);

	std::vector<DWORD> prev_list_items = get_listbox_items_data(listbox);
	DWORD prev_list_sel = SendMessage(listbox, LB_GETCURSEL, 0, 0);

	while(true)
	{
		/* Try collapsing a group. */

		SendMessage(listbox, WM_KEYDOWN, VK_LEFT, 0);

		std::vector<DWORD> new_list_items = get_listbox_items_data(listbox);
		DWORD new_list_sel = SendMessage(listbox, LB_GETCURSEL, 0, 0);

		if(new_list_items != prev_list_items || new_list_sel != prev_list_sel)
		{
			/* We collapsed a group. */

			prev_list_items = new_list_items;
			prev_list_sel = new_list_sel;

			continue;
		}
		
		/* Doesn't look like we are in a group, try moving up. */

		SendMessage(listbox, WM_KEYDOWN, VK_UP, 0);

		new_list_items = get_listbox_items_data(listbox);
		new_list_sel = SendMessage(listbox, LB_GETCURSEL, 0, 0);

		if(new_list_items != prev_list_items || new_list_sel != prev_list_sel)
		{
			/* Moved up. */

			prev_list_items = new_list_items;
			prev_list_sel = new_list_sel;

			continue;
		}

		/* Looks like everything is now collapsed and we are at the top. */
		break;
	}
}

static void walk_listbox(HWND listbox, std::set<DWORD> *seen_items, int depth)
{
	DWORD sel_idx = SendMessage(listbox, LB_GETCURSEL, 0, 0);
	DWORD sel_item = SendMessage(listbox, LB_GETITEMDATA, sel_idx, 0);

	std::vector<DWORD> new_items;

	while(seen_items->find(sel_item) == seen_items->end())
	{
		seen_items->insert(sel_item);
		new_items.push_back(sel_item);

		SendMessage(listbox, WM_KEYDOWN, VK_DOWN, 0);

		sel_idx = SendMessage(listbox, LB_GETCURSEL, 0, 0);
		sel_item = SendMessage(listbox, LB_GETITEMDATA, sel_idx, 0);
	}

	while(sel_item != new_items[0])
	{
		SendMessage(listbox, WM_KEYDOWN, VK_UP, 0);

		sel_idx = SendMessage(listbox, LB_GETCURSEL, 0, 0);
		sel_item = SendMessage(listbox, LB_GETITEMDATA, sel_idx, 0);
	}

	size_t i;

	char indent[128] = {'\0'};
	for(i = 0; i < depth; ++i)
	{
		strcat(indent, "  ");
	}

	for(i = 0; i < new_items.size(); ++i)
	{
		printf("%s%u\n", indent, new_items[i]);

#ifdef DUMP_INDEX_SCREENSHOTS
		RECT r;
		SendMessage(listbox, LB_GETITEMRECT, sel_idx, (LPARAM)(&r));

		char name[32];
		sprintf(name, "%u.bmp", new_items[i]);

		capture_rect(listbox, r, name);
#endif

		std::vector<DWORD> prev_list_items = get_listbox_items_data(listbox);

		SendMessage(listbox, WM_KEYDOWN, VK_RIGHT, 0);

		std::vector<DWORD> new_list_items = get_listbox_items_data(listbox);

		if(new_list_items != prev_list_items)
		{
			walk_listbox(listbox, seen_items, depth + 1);
			SendMessage(listbox, WM_KEYDOWN, VK_LEFT, 0);
		}
		else{
#ifdef DUMP_PAGE_TEXT
			char txt_name[32];
			sprintf(txt_name, "%u.txt", new_items[i]);

			DWORD txt_attribs = GetFileAttributes(txt_name);

			bool do_txt = txt_attribs == -1;
#else
			bool do_txt = false;
#endif

#ifdef DUMP_PAGE_PRINTOUT
			char ps_name[32];
			sprintf(ps_name, "%u.ps", new_items[i]);

			DWORD ps_attribs = GetFileAttributes(ps_name);

			bool do_ps = ps_attribs == -1;
#else
			bool do_ps = false;
#endif
			
			/* This solitary page seems to crash the viewer when you load it. */
			if(new_items[i] == 3941378)
			{
				do_txt = false;
				do_ps = false;
			}

			if(do_txt || do_ps)
			{
				/* Pressing enter on the listbox navigates the main window
				 * to the selected topic.
				*/
				SendMessage(listbox, WM_CHAR, '\r', 0);
			}

#ifdef DUMP_PAGE_TEXT
			if(do_txt)
			{
				BOOL x = OpenClipboard(NULL);
				assert(x);

				EmptyClipboard();
				CloseClipboard();

				/* The "Copy" commad from the "Edit" menu... */
				PostMessage(windoc, WM_COMMAND, 1203, 0);

				HWND copy_dialog = NULL;
				while(copy_dialog == NULL)
				{
					StupidMessageHandler::run();
					EnumWindows(&find_copy_dialog, (LPARAM)(&copy_dialog));
				}

				assert(copy_dialog != NULL);

				HWND edit_ctl = NULL;
				EnumChildWindows(copy_dialog, &find_copy_edit, (LPARAM)(&edit_ctl));

				SendMessage(edit_ctl, EM_SETSEL, 0, -1);

				SendMessage(copy_dialog, WM_COMMAND, MAKEWPARAM(IDOK, BN_CLICKED), 0);

				x = OpenClipboard(NULL);

				HANDLE clipboard_data = GetClipboardData(CF_OEMTEXT);
				assert(clipboard_data != NULL);

				const char *clipboard_text = (const char*)(GlobalLock(clipboard_data));

				FILE *f = fopen(txt_name, "wb");
				assert(f != NULL);
				fwrite(clipboard_text, strlen(clipboard_text), 1, f);
				fclose(f);

				GlobalUnlock(clipboard_data);
				CloseClipboard();
			}
#endif

#ifdef DUMP_PAGE_PRINTOUT
			if(do_ps)
			{
				/* Command 1103 appears to be a fixed internal command for printing
				 * the current topic in MMV. There is a dynamically generated ID
				 * for the "Print Topic" menu command, however this acts as a proxy
				 * and seems to randomly fail with an unhelpful error if you use it
				 * enough.
				*/
				
				PostMessage(windoc, WM_COMMAND, 1103, 0);

				bool got_print = false;
				while(!got_print)
				{
					got_print = print_server->wait_for_print(1);
					StupidMessageHandler::run();
				}

				/* Give the viewer a chance to catch up to the fact printing has
				 * finished, or else it can break.
				*/
				Sleep(500);

				if(got_print)
				{
					std::vector<unsigned char> print_data = print_server->get_next_print();

					FILE *f = fopen(ps_name, "wb");
					assert(f != NULL);
					fwrite(&(print_data[0]), print_data.size(), 1, f);
					fclose(f);

					printf("Printed page %u\n", new_items[i]);
				}
			}
#endif
		}

		SendMessage(listbox, WM_KEYDOWN, VK_DOWN, 0);
		sel_idx = SendMessage(listbox, LB_GETCURSEL, 0, 0);
	}

	sel_idx = SendMessage(listbox, LB_GETCURSEL, 0, 0);
	sel_item = SendMessage(listbox, LB_GETITEMDATA, sel_idx, 0);

	while(sel_item != new_items[0])
	{
		SendMessage(listbox, WM_KEYDOWN, VK_UP, 0);

		sel_idx = SendMessage(listbox, LB_GETCURSEL, 0, 0);
		sel_item = SendMessage(listbox, LB_GETITEMDATA, sel_idx, 0);
	}
}

static HDC hdcMemDC = NULL;
static HBITMAP hbmScreen = NULL;

static void capture_rect(HWND hWnd, RECT rect, const char *filename)
{
    HDC hdcScreen;
    BITMAP bmpScreen;
    DWORD dwBytesWritten = 0;
    DWORD dwSizeofDIB = 0;
    HANDLE hFile = NULL;
    char* lpbitmap = NULL;
    HANDLE hDIB = NULL;
    DWORD dwBmpSize = 0;

    hdcScreen = GetDC(hWnd);

	if(hdcMemDC == NULL)
	{
		// Create a compatible DC, which is used in a BitBlt from the window DC.
		hdcMemDC = CreateCompatibleDC(hdcScreen);

		if (!hdcMemDC)
		{
			MessageBox(hWnd, "CreateCompatibleDC has failed", "Failed", MB_OK);
			goto done;
		}
	}

	if(hbmScreen == NULL)
	{
		// Create a compatible bitmap from the Window DC.
		hbmScreen = CreateCompatibleBitmap(hdcScreen, rect.right - rect.left, rect.bottom - rect.top);

		if (!hbmScreen)
		{
			MessageBox(hWnd, "CreateCompatibleBitmap Failed", "Failed", MB_OK);
			goto done;
		}
	}

    // Select the compatible bitmap into the compatible memory DC.
    SelectObject(hdcMemDC, hbmScreen);

    // Bit block transfer into our compatible memory DC.
    if (!BitBlt(hdcMemDC,
        0, 0,
        rect.right - rect.left, rect.bottom - rect.top,
        hdcScreen,
        rect.left, rect.top,
        SRCCOPY))
    {
        MessageBox(hWnd, "BitBlt has failed", "Failed", MB_OK);
        goto done;
    }

    // Get the BITMAP from the HBITMAP.
    GetObject(hbmScreen, sizeof(BITMAP), &bmpScreen);

    BITMAPFILEHEADER   bmfHeader;
    BITMAPINFOHEADER   bi;

    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpScreen.bmWidth;
    bi.biHeight = bmpScreen.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    dwBmpSize = ((bmpScreen.bmWidth * bi.biBitCount + 31) / 32) * 4 * bmpScreen.bmHeight;

    // Starting with 32-bit Windows, GlobalAlloc and LocalAlloc are implemented as wrapper functions that 
    // call HeapAlloc using a handle to the process's default heap. Therefore, GlobalAlloc and LocalAlloc 
    // have greater overhead than HeapAlloc.
    hDIB = GlobalAlloc(GHND, dwBmpSize);
    lpbitmap = (char*)GlobalLock(hDIB);

    // Gets the "bits" from the bitmap, and copies them into a buffer 
    // that's pointed to by lpbitmap.
    GetDIBits(hdcScreen, hbmScreen, 0,
        (UINT)bmpScreen.bmHeight,
        lpbitmap,
        (BITMAPINFO*)&bi, DIB_RGB_COLORS);

    // A file is created, this is where we will save the screen capture.
    hFile = CreateFile(filename,
        GENERIC_WRITE,
        0,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);

    // Add the size of the headers to the size of the bitmap to get the total file size.
    dwSizeofDIB = dwBmpSize + sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	bmfHeader.bfReserved1 = 0;
	bmfHeader.bfReserved2 = 0;

    // Offset to where the actual bitmap bits start.
    bmfHeader.bfOffBits = (DWORD)sizeof(BITMAPFILEHEADER) + (DWORD)sizeof(BITMAPINFOHEADER);

    // Size of the file.
    bmfHeader.bfSize = dwSizeofDIB;

    // bfType must always be BM for Bitmaps.
    bmfHeader.bfType = 0x4D42; // BM.

    WriteFile(hFile, (LPSTR)&bmfHeader, sizeof(BITMAPFILEHEADER), &dwBytesWritten, NULL);
    WriteFile(hFile, (LPSTR)&bi, sizeof(BITMAPINFOHEADER), &dwBytesWritten, NULL);
    WriteFile(hFile, (LPSTR)lpbitmap, dwBmpSize, &dwBytesWritten, NULL);

    // Unlock and Free the DIB from the heap.
    GlobalUnlock(hDIB);
    GlobalFree(hDIB);

    // Close the handle for the file that was created.
    CloseHandle(hFile);

    // Clean up.
done:
    ReleaseDC(hWnd, hdcScreen);
}

static void capture_cleanup()
{
	DeleteObject(hbmScreen);
	DeleteObject(hdcMemDC);
}
