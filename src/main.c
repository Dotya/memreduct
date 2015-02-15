﻿// Mem Reduct
// Copyright © 2011-2013, 2015 Henry++

#include <windows.h>

#include "main.h"

#include "resource.h"
#include "routine.h"

NOTIFYICONDATA _r_nid = {0};

HDC _r_dc = NULL, _r_cdc = NULL;
HBITMAP _r_bitmap, _r_bitmap_mask = NULL;
RECT _r_rc = {0};
HFONT _r_font = NULL;
VOID *_r_bits = NULL;

// Show tray balloon tip
BOOL ShowBalloonTip(DWORD flags, LPCWSTR title, LPCWSTR text, BOOL)
{
	// Check interval
	//if(bLimit && cfg.dwLastBalloon && (GetTickCount() - cfg.dwLastBalloon) < ((UINT)ini.read(APP_NAME_SHORT, L"BalloonInterval", 10)) * 1000)
	//	return FALSE;

	// Configure structure
	_r_nid.uFlags = NIF_INFO;
	_r_nid.dwInfoFlags = NIIF_RESPECT_QUIET_TIME | flags;

	// Set text
	StringCchCopy(_r_nid.szInfoTitle, _countof(_r_nid.szInfoTitle), title);
	StringCchCopy(_r_nid.szInfo, _countof(_r_nid.szInfo), text);

	// Show balloon
	Shell_NotifyIcon(NIM_MODIFY, &_r_nid);

	// Clear for Prevent reshow
	_r_nid.szInfo[0] = 0;
	_r_nid.szInfoTitle[0] = 0;

	return TRUE;
}

DWORD _Reduct_MemoryStatus(_R_MEMORYSTATUS* m)
{
	MEMORYSTATUSEX msex = {0};

	msex.dwLength = sizeof(msex);

	if(GlobalMemoryStatusEx(&msex) && m) // WARNING!!! don't tounch "m"!
	{
		m->percent_phys = msex.dwMemoryLoad;

		m->free_phys = msex.ullAvailPhys;
		m->total_phys = msex.ullTotalPhys;

		m->percent_page = DWORD((msex.ullTotalPageFile - msex.ullAvailPageFile) / (msex.ullTotalPageFile / 100));

		m->free_page = msex.ullAvailPageFile;
		m->total_page = msex.ullTotalPageFile;
	}

	SYSTEM_CACHE_INFORMATION sci = {0};

	if(m && NtQuerySystemInformation(SystemFileCacheInformation, &sci, sizeof(sci), NULL) >= 0)
	{
		m->percent_ws = DWORD(sci.CurrentSize / (sci.PeakSize / 100));

		m->free_ws = (sci.PeakSize - sci.CurrentSize);
		m->total_ws = sci.PeakSize;
	}

	return msex.dwMemoryLoad;
}

HICON _Reduct_CreateIcon(DWORD percent)
{
	if(!percent)
	{
		percent = _Reduct_MemoryStatus(NULL);
	}

	HBITMAP old_bitmap = (HBITMAP)SelectObject(_r_cdc, _r_bitmap);

	COLORREF clrOld = SetBkColor(_r_cdc, COLOR_TRAY_TRANSPARENT_BG);
	ExtTextOut(_r_cdc, 0, 0, ETO_OPAQUE, &_r_rc, NULL, 0, NULL);
	SetBkColor(_r_cdc, clrOld);

	// Draw
	SetTextColor(_r_cdc, 0);
	SetBkMode(_r_cdc, TRANSPARENT);

	CString buffer = _r_helper_format(L"%d\0", percent);

	DrawTextEx(_r_cdc, buffer.GetBuffer(), buffer.GetLength(), &_r_rc, DT_VCENTER | DT_CENTER | DT_SINGLELINE | DT_NOCLIP, NULL);

	SelectObject(_r_dc, old_bitmap);

	#define ARGB(a,r,g,b) ((b) + (g << 8) + (r << 16) + (a << 24))

	if(1)
	{
		DWORD *lpdwPixel = (DWORD*)_r_bits;


		for(INT i = ((_r_rc.right * _r_rc.right) - 1); i >= 0; i--)
		{
			*lpdwPixel &= COLOR_TRAY_TRANSPARENT_BG;

			if(*lpdwPixel == COLOR_TRAY_TRANSPARENT_BG)
			{
				*lpdwPixel |= 0x00000000;
			}
			else
			{
				*lpdwPixel |= ARGB(255, 255, 255, 255);
			}

			lpdwPixel++;
		}
	}

	ICONINFO ii = {TRUE, 0, 0, _r_bitmap_mask, _r_bitmap};

	return CreateIconIndirect(&ii);
}

BOOL _Reduct_Start(HWND)
{
	// if user has no rights
	if(_r_system_uacstate())
	{
		ShowBalloonTip(NIIF_ERROR, APP_NAME, _r_locale(IDS_BALLOON_WARNING), FALSE);
		return FALSE;
	}

	_R_MEMORYSTATUS m = {0};

	_Reduct_MemoryStatus(&m);

	BOOL is_vista = _r_system_validversion(6, 0);
	SYSTEM_MEMORY_LIST_COMMAND smlc;

	// System working set
	if(_r_cfg_read(L"ReductSystemWorkingSet", 1))
	{
		SYSTEM_CACHE_INFORMATION cache = {0};

		cache.MinimumWorkingSet = (ULONG)-1;
		cache.MaximumWorkingSet = (ULONG)-1;

		NtSetSystemInformation(SystemFileCacheInformation, &cache, sizeof(cache));
	}

	// Working set
	if(is_vista && _r_cfg_read(L"ReductWorkingSet", 1))
	{
		smlc = MemoryEmptyWorkingSets;
		NtSetSystemInformation(SystemMemoryListInformation, &smlc, sizeof(smlc));
	}
						
	// Modified pagelists
	if(is_vista && _r_cfg_read(L"ReductModifiedList", 0))
	{
		smlc = MemoryFlushModifiedList;
		NtSetSystemInformation(SystemMemoryListInformation, &smlc, sizeof(smlc));
	}
	
	// Standby pagelists
	if(is_vista && _r_cfg_read(L"ReductStandbyList", 0))
	{
		smlc = MemoryPurgeStandbyList;
		NtSetSystemInformation(SystemMemoryListInformation, &smlc, sizeof(smlc));
	}

	// Standby priority-0 pagelists
	if(is_vista && _r_cfg_read(L"ReductStandbyPriority0List", 1))
	{
		smlc = MemoryPurgeLowPriorityStandbyList;
		NtSetSystemInformation(SystemMemoryListInformation, &smlc, sizeof(smlc));
	}

	DWORD current = _Reduct_MemoryStatus(NULL);

	ShowBalloonTip(NIIF_INFO, APP_NAME, _r_helper_format(_r_locale(IDS_BALLOON_REDUCT), current, _Reduct_MemoryStatus(NULL) - current), FALSE);

	return TRUE;
}

VOID CALLBACK _Reduct_MonitorCallback(HWND hwnd, UINT, UINT_PTR, DWORD)
{
	_R_MEMORYSTATUS m = {0};

	_Reduct_MemoryStatus(&m);

	if(_r_nid.hIcon)
	{
		DestroyIcon(_r_nid.hIcon);
	}

	// Refresh tray info
	_r_nid.uFlags = NIF_ICON | NIF_TIP;

	StringCchPrintf(_r_nid.szTip, _countof(_r_nid.szTip), _r_locale(IDS_TRAY_TOOLTIP), m.percent_phys, m.percent_page, m.percent_ws);
	_r_nid.hIcon = _Reduct_CreateIcon(NULL);

	Shell_NotifyIcon(NIM_MODIFY, &_r_nid);

	// Autoreduct
	if(
		(m.percent_phys >= _r_cfg_read(L"DangerLevel", 90) && _r_cfg_read(L"AutoreductDanger", 1)) ||
		(m.percent_phys >= _r_cfg_read(L"WarningLevel", 60) && _r_cfg_read(L"AutoreductWarning", 0))
	)
	{
		_Reduct_Start(hwnd);
	}

	if(IsWindowVisible(hwnd))
	{
		// Physical memory
		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_format(L"%d%%", m.percent_phys), 0, 1, -1, -1, m.percent_phys);

		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_formatsize64(m.free_phys), 1, 1, -1, -1, m.percent_phys);
		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_formatsize64(m.total_phys), 2, 1, -1, -1, m.percent_phys);
		
		// Pagefile memory
		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_format(L"%d%%", m.percent_page), 3, 1, -1, -1, m.percent_page);

		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_formatsize64(m.free_page), 4, 1, -1, -1, m.percent_page);
		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_formatsize64(m.total_page), 5, 1, -1, -1, m.percent_page);

		// System working set
		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_format(L"%d%%", m.percent_ws), 6, 1, -1, -1, m.percent_ws);

		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_formatsize64(m.free_ws), 7, 1, -1, -1, m.percent_ws);
		_r_listview_additem(hwnd, IDC_LISTVIEW, _r_helper_formatsize64(m.total_ws), 8, 1, -1, -1, m.percent_ws);
	}

}

VOID _Reduct_Unitialize()
{
	if(_r_nid.hIcon)
	{
		DestroyIcon(_r_nid.hIcon);
	}

	UnregisterHotKey(_r_hwnd, UID);
	KillTimer(_r_hwnd, UID);

	DeleteObject(_r_font);
	DeleteDC(_r_cdc);
	DeleteDC(_r_dc);
	DeleteObject(_r_bitmap);
	DeleteObject(_r_bitmap_mask);
}

VOID _Reduct_Initialize()
{
	_Reduct_Unitialize();

	_r_rc.right = GetSystemMetrics(SM_CXSMICON);
	_r_rc.bottom = GetSystemMetrics(SM_CYSMICON);

	BITMAPINFO bmi = {0};

    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = _r_rc.right;
    bmi.bmiHeader.biHeight = _r_rc.bottom;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biSizeImage = 0;

	_r_dc = GetDC(NULL);

	_r_bitmap = CreateDIBSection(_r_dc, &bmi, DIB_RGB_COLORS, (void**)&_r_bits, NULL, 0);
	_r_bitmap_mask = CreateBitmap(_r_rc.right, _r_rc.bottom, 1, 1, NULL);;

	_r_cdc = CreateCompatibleDC(_r_dc);

	ReleaseDC(NULL, _r_dc);

	LOGFONT lf ={0};

	lf.lfQuality = ANTIALIASED_QUALITY;
	//lf.lfWeight = FW_SEMIBOLD;
	lf.lfHeight = -MulDiv(8, GetDeviceCaps(_r_cdc, LOGPIXELSY), 72);

	StringCchCopy(lf.lfFaceName, _countof(lf.lfFaceName), L"Tahoma");

	_r_font = CreateFontIndirect(&lf);

	SelectObject(_r_cdc, _r_font);

	UINT hk = _r_cfg_read(L"Hotkey", 0);

	if(hk)
	{
		RegisterHotKey(_r_hwnd, UID, (HIBYTE(hk) & 2) | ((HIBYTE(hk) & 4) >> 2) | ((HIBYTE(hk) & 1) << 2), LOBYTE(hk));
	}

	_r_windowtotop(_r_hwnd, _r_cfg_read(L"AlwaysOnTop", 0));

	SetTimer(_r_hwnd, UID, 500, _Reduct_MonitorCallback);
}

INT_PTR WINAPI PagesDlgProc(HWND hwnd, UINT msg, WPARAM, LPARAM lparam)
{
	switch(msg)
	{
		case WM_INITDIALOG:
		{
			SetProp(hwnd, L"id", (HANDLE)lparam);

			if((INT)lparam == IDD_SETTINGS_1)
			{
				CheckDlgButton(hwnd, IDC_ALWAYSONTOP_CHK, _r_cfg_read(L"AlwaysOnTop", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_STARTMINIMIZED_CHK, _r_cfg_read(L"StartMinimized", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_LOADONSTARTUP_CHK, _r_autorun_is_present(APP_NAME) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_CHECKUPDATES_CHK, _r_cfg_read(L"CheckUpdates", 1) ? BST_CHECKED : BST_UNCHECKED);

				SendDlgItemMessage(hwnd, IDC_LANGUAGE, CB_INSERTSTRING, 0, (LPARAM)L"System default");
				SendDlgItemMessage(hwnd, IDC_LANGUAGE, CB_SETCURSEL, 0, NULL);

				EnumResourceLanguages(NULL, RT_STRING, MAKEINTRESOURCE(63), _r_locale_enum, (LONG_PTR)GetDlgItem(hwnd, IDC_LANGUAGE));
			}
			else if((INT)lparam == IDD_SETTINGS_2)
			{
				CheckDlgButton(hwnd, IDC_SYSTEMWORKINGSET_CHK, _r_cfg_read(L"ReductSystemWorkingSet", 1) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_WORKINGSET_CHK, _r_cfg_read(L"ReductWorkingSet", 1) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_MODIFIEDLIST_CHK, _r_cfg_read(L"ReductModifiedList", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_STANDBYLIST_CHK, _r_cfg_read(L"ReductStandbyList", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_STANDBYLISTPRIORITY0_CHK, _r_cfg_read(L"ReductStandbyPriority0List", 1) ? BST_CHECKED : BST_UNCHECKED);

				if(!_r_system_validversion(6, 0))
				{
					EnableWindow(GetDlgItem(hwnd, IDC_WORKINGSET_CHK), FALSE);
					EnableWindow(GetDlgItem(hwnd, IDC_MODIFIEDLIST_CHK), FALSE);
					EnableWindow(GetDlgItem(hwnd, IDC_STANDBYLIST_CHK), FALSE);
					EnableWindow(GetDlgItem(hwnd, IDC_STANDBYLISTPRIORITY0_CHK), FALSE);
				}

				SendDlgItemMessage(hwnd, IDC_WARNING_LEVEL, UDM_SETRANGE32, 1, 99);
				SendDlgItemMessage(hwnd, IDC_WARNING_LEVEL, UDM_SETPOS32, 0, _r_cfg_read(L"WarningLevel", 60));

				SendDlgItemMessage(hwnd, IDC_DANGER_LEVEL, UDM_SETRANGE32, 1, 99);
				SendDlgItemMessage(hwnd, IDC_DANGER_LEVEL, UDM_SETPOS32, 0, _r_cfg_read(L"DangerLevel", 90));

				SendDlgItemMessage(hwnd, IDC_HOTKEY, HKM_SETHOTKEY, _r_cfg_read(L"Hotkey", 0), NULL);
			}
			else if((INT)lparam == IDD_SETTINGS_3)
			{
				CheckDlgButton(hwnd, IDC_AUTOREDUCTWARNING_CHK, _r_cfg_read(L"AutoreductWarning", 0) ? BST_CHECKED : BST_UNCHECKED);
				CheckDlgButton(hwnd, IDC_AUTOREDUCTDANGER_CHK, _r_cfg_read(L"AutoreductDanger", 1) ? BST_CHECKED : BST_UNCHECKED);
			}
			else if((INT)lparam == IDD_SETTINGS_4)
			{

			}

			break;
		}
			
		case WM_DESTROY:
		{
			if(GetProp(GetParent(hwnd), L"is_save"))
			{
				if(INT(GetProp(hwnd, L"id")) == IDD_SETTINGS_1)
				{
					// general
					_r_cfg_write(L"AlwaysOnTop", INT((IsDlgButtonChecked(hwnd, IDC_ALWAYSONTOP_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_cfg_write(L"StartMinimized", INT((IsDlgButtonChecked(hwnd, IDC_STARTMINIMIZED_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_autorun_cancer(APP_NAME, IsDlgButtonChecked(hwnd, IDC_LOADONSTARTUP_CHK) == BST_UNCHECKED);
					_r_cfg_write(L"CheckUpdates", INT((IsDlgButtonChecked(hwnd, IDC_CHECKUPDATES_CHK) == BST_CHECKED) ? TRUE : FALSE));

					// language
					LCID lang = (LCID)SendDlgItemMessage(hwnd, IDC_LANGUAGE, CB_GETITEMDATA, SendDlgItemMessage(hwnd, IDC_LANGUAGE, CB_GETCURSEL, 0, NULL), NULL);

					if(lang <= 0)
					{
						lang = NULL;
					}

					SetProp(_r_hwnd, L"is_restart", (HANDLE)((lang != _r_lcid) ? TRUE : FALSE));

					_r_locale_set(lang);
				}
				else if(INT(GetProp(hwnd, L"id")) == IDD_SETTINGS_2)
				{
					_r_cfg_write(L"ReductSystemWorkingSet", INT((IsDlgButtonChecked(hwnd, IDC_SYSTEMWORKINGSET_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_cfg_write(L"ReductWorkingSet", INT((IsDlgButtonChecked(hwnd, IDC_WORKINGSET_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_cfg_write(L"ReductModifiedList", INT((IsDlgButtonChecked(hwnd, IDC_MODIFIEDLIST_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_cfg_write(L"ReductStandbyList", INT((IsDlgButtonChecked(hwnd, IDC_STANDBYLIST_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_cfg_write(L"ReductStandbyPriority0List", INT((IsDlgButtonChecked(hwnd, IDC_STANDBYLISTPRIORITY0_CHK) == BST_CHECKED) ? TRUE : FALSE));

					_r_cfg_write(L"WarningLevel", (DWORD)SendDlgItemMessage(hwnd, IDC_WARNING_LEVEL, UDM_GETPOS32, 0, NULL));
					_r_cfg_write(L"DangerLevel", (DWORD)SendDlgItemMessage(hwnd, IDC_DANGER_LEVEL, UDM_GETPOS32, 0, NULL));

					_r_cfg_write(L"Hotkey", (DWORD)SendDlgItemMessage(hwnd, IDC_HOTKEY, HKM_GETHOTKEY, 0, NULL));
				}
				else if(INT(GetProp(hwnd, L"id")) == IDD_SETTINGS_3)
				{
					_r_cfg_write(L"AutoreductWarning", INT((IsDlgButtonChecked(hwnd, IDC_AUTOREDUCTWARNING_CHK) == BST_CHECKED) ? TRUE : FALSE));
					_r_cfg_write(L"AutoreductDanger", INT((IsDlgButtonChecked(hwnd, IDC_AUTOREDUCTDANGER_CHK) == BST_CHECKED) ? TRUE : FALSE));
				}
				else if(INT(GetProp(hwnd, L"id")) == IDD_SETTINGS_4)
				{

				}

				_Reduct_Initialize();
			}

			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK SettingsDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch(msg)
	{
		case WM_INITDIALOG:
		{
			_r_treeview_setstyle(hwnd, IDC_NAV, TVS_EX_DOUBLEBUFFER, GetSystemMetrics(SM_CYSMICON));

			for(INT i = 0; i < APP_SETTINGS_COUNT; i++)
			{
				_r_treeview_additem(hwnd, IDC_NAV, _r_locale(IDS_SETTINGS_1 + i), -1, (LPARAM)CreateDialogParam(NULL, MAKEINTRESOURCE(IDD_SETTINGS_1 + i), hwnd, PagesDlgProc, IDD_SETTINGS_1 + i));
			}

			SendDlgItemMessage(hwnd, IDC_NAV, TVM_SELECTITEM, TVGN_CARET, SendDlgItemMessage(hwnd, IDC_NAV, TVM_GETNEXTITEM, TVGN_FIRSTVISIBLE, NULL));

			break;
		}

		case WM_NOTIFY:
		{
			LPNMHDR lphdr = (LPNMHDR)lparam;

			switch(lphdr->code)
			{
				case TVN_SELCHANGED:
				{
					if(wparam == IDC_NAV)
					{
						LPNMTREEVIEW pnmtv = (LPNMTREEVIEW)lparam;

						ShowWindow((HWND)GetProp(hwnd, L"hwnd"), SW_HIDE);

						SetProp(hwnd, L"hwnd", (HANDLE)pnmtv->itemNew.lParam);

						ShowWindow((HWND)pnmtv->itemNew.lParam, SW_SHOW);
					}

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch(LOWORD(wparam))
			{
				case IDOK: // process Enter key
				case IDC_OK:
				{
					SetProp(hwnd, L"is_save", (HANDLE)TRUE); // save settings indicator

					// without break;
				}

				case IDCANCEL: // process Esc key
				case IDC_CANCEL:
				{
					EndDialog(hwnd, 0);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT_PTR CALLBACK ReductDlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM)
{
	switch(msg)
	{
		case WM_INITDIALOG:
		{
			break;
		}

		case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			HDC dc = BeginPaint(hwnd, &ps);
			RECT rc = {0};

			GetClientRect(hwnd, &rc);
			rc.top = rc.bottom - GetSystemMetrics(SM_CYSIZE) * 2;

			COLORREF clrOld = SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
			ExtTextOut(dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
			SetBkColor(dc, clrOld);

			for(INT i = 0; i < rc.right; i++)
			{
				SetPixel(dc, i, rc.top, RGB(223, 223, 223));
			}

			EndPaint(hwnd, &ps);

			break;
		}
		
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORDLG:
		{
			return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
		}

		case WM_COMMAND:
		{
			switch(LOWORD(wparam))
			{
				case IDC_OK:
				{
					_Reduct_Start(hwnd);
					break;
				}

				case IDCANCEL: // process Esc key
				case IDC_CANCEL:
				{
					EndDialog(hwnd, 0);
					break;
				}
			}

			break;
		}

	

}

	return FALSE;
}

LRESULT CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch(msg)
	{
		case WM_INITDIALOG:
		{
			_r_hwnd = hwnd;

			_Reduct_Initialize();
/*
 //  ------------------------------------------------------
    //  Initialize COM.
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    //  Set general COM security levels.
    hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, 0, NULL);

    //  ------------------------------------------------------
    //  Create a name for the task.
    LPCWSTR wszTaskName = L"task name";

    //  ------------------------------------------------------
    //  Create an instance of the Task Service. 
    ITaskService *pService = NULL;
    hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService );
    if (FAILED(hr))

    //  Connect to the task service.
    hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());

    //  ------------------------------------------------------
    //  Get the pointer to the root task folder.  This folder will hold the new task that is registered.
    ITaskFolder *pRootFolder = NULL;
    hr = pService->GetFolder(_bstr_t( L"\\") , &pRootFolder);

    //  If the same task exists, remove it.
    pRootFolder->DeleteTask(_bstr_t(wszTaskName), 0);

    //  Create the task builder object to create the task.
    ITaskDefinition *pTask = NULL;
    hr = pService->NewTask(0, &pTask);

    pService->Release();  // COM clean up.  Pointer is no longer used.

    //  ------------------------------------------------------
    //  Get the registration info for setting the identification.
    IRegistrationInfo *pRegInfo= NULL;
    hr = pTask->get_RegistrationInfo(&pRegInfo);

    hr = pRegInfo->put_Author(L"author");
    pRegInfo->Release();  

    //  ------------------------------------------------------
    //  Create the settings for the task
    ITaskSettings *pSettings = NULL;
    hr = pTask->get_Settings(&pSettings);

    //  Set setting values for the task. 
    hr = pSettings->put_StartWhenAvailable(VARIANT_BOOL(true));
    pSettings->Release();

    //  ------------------------------------------------------
    //  Get the trigger collection to insert the logon trigger.
    ITriggerCollection *pTriggerCollection = NULL;
    hr = pTask->get_Triggers(&pTriggerCollection);

    //  Add the logon trigger to the task.
    ITrigger *pTrigger = NULL;
    hr = pTriggerCollection->Create(TASK_TRIGGER_LOGON, &pTrigger);
    pTriggerCollection->Release();

    ILogonTrigger *pLogonTrigger = NULL;       
    hr = pTrigger->QueryInterface(IID_ILogonTrigger, (void**)&pLogonTrigger);
    pTrigger->Release();

    hr = pLogonTrigger->put_Id(_bstr_t(L"Trigger1"));  

    //  Define the user.  The task will execute when the user logs on.
    //  The specified user must be a user on this computer.  
    hr = pLogonTrigger->put_UserId( NULL );  
    pLogonTrigger->Release();

    //  ------------------------------------------------------
    //  Add an Action to the task. This task will execute notepad.exe.     
    IActionCollection *pActionCollection = NULL;

    //  Get the task action collection pointer.
    hr = pTask->get_Actions( &pActionCollection );

    //  Create the action, specifying that it is an executable action.
    IAction *pAction = NULL;
    hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
    pActionCollection->Release();

    IExecAction *pExecAction = NULL;
    //  QI for the executable task pointer.
    hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
    pAction->Release();

    //  Set the path of the executable to notepad.exe.
    hr = pExecAction->put_Path( _bstr_t(L"notepad.exe") );
    pExecAction->Release();

    //  ------------------------------------------------------
    //  Save the task in the root folder.
    IRegisteredTask *pRegisteredTask = NULL;

    hr = pRootFolder->RegisterTaskDefinition(_bstr_t(wszTaskName), pTask, TASK_CREATE_OR_UPDATE, _variant_t(""), 
        _variant_t(""), TASK_LOGON_NONE, _variant_t(L""), &pRegisteredTask);


    // Clean up
    pRootFolder->Release();
    pTask->Release();
    pRegisteredTask->Release();

    CoUninitialize();

	*/

			if(_r_system_adminstate())
			{
				HANDLE token = NULL;

				if(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
				{
					_r_system_setprivilege(token, SE_INCREASE_QUOTA_NAME, TRUE);
					_r_system_setprivilege(token, SE_PROF_SINGLE_PROCESS_NAME, TRUE);
				}

				if(token)
				{
					CloseHandle(token);
				}
			}

			_r_listview_setstyle(hwnd, IDC_LISTVIEW, LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP, TRUE);

			_r_listview_addcolumn(hwnd, IDC_LISTVIEW, NULL, 50, 1, LVCFMT_RIGHT);
			_r_listview_addcolumn(hwnd, IDC_LISTVIEW, NULL, 50, 2, LVCFMT_LEFT);

			for(INT i = 0; i < 3; i++)
			{
				_r_listview_addgroup(hwnd, IDC_LISTVIEW, i, _r_locale(IDS_GROUP_1 + i), 0, 0);
			}

			for(INT i = 0, j = 0; i < 3; i++)
			{
				_r_listview_additem(hwnd, IDC_LISTVIEW, _r_locale(IDS_ITEM_1), j++, 0, -1, i);
				_r_listview_additem(hwnd, IDC_LISTVIEW, _r_locale(IDS_ITEM_2), j++, 0, -1, i);
				_r_listview_additem(hwnd, IDC_LISTVIEW, _r_locale(IDS_ITEM_3), j++, 0, -1, i);
			}

			// Tray icon
			_r_nid.cbSize = _r_system_validversion(6, 0) ? sizeof(_r_nid) : NOTIFYICONDATA_V3_SIZE;
			_r_nid.hWnd = hwnd;
			_r_nid.uID = UID;
			_r_nid.uFlags = NIF_MESSAGE | NIF_ICON;
			_r_nid.uCallbackMessage = WM_TRAYICON;
			_r_nid.hIcon = _Reduct_CreateIcon(NULL);

			Shell_NotifyIcon(NIM_ADD, &_r_nid);

/*
			for(int i = 0; i <= 100; i++)
			{
				DestroyIcon(_r_nid.hIcon);

				_r_nid.uFlags = NIF_ICON;
				_r_nid.hIcon = _Reduct_CreateIcon(i, 1);

				Shell_NotifyIcon(NIM_MODIFY, &_r_nid);

				SleepEx(15, FALSE);
			}
*/
			if(!_r_cfg_read(L"StartMinimized", 0))
			{
				ShowWindow(hwnd, SW_SHOW);
			}

			//_r_msg(0, L"%i", _r_pixeldpi((14 + 8 + 8) * 2));
			//Beep( 750, 300 );
			//MessageBeep(MB_ICONINFORMATION);
			//PlaySound(L"SystemHand", NULL, SND_ALIAS | SND_ASYNC);

			return TRUE;;
		}

		case WM_DESTROY:
		{
			_Reduct_Unitialize();

			if(_r_nid.uID)
			{
				Shell_NotifyIcon(NIM_DELETE, &_r_nid);
			}

			PostQuitMessage(0);

			break;
		}

		case WM_QUERYENDSESSION:
		{
			if(lparam == ENDSESSION_CLOSEAPP)
			{
				return TRUE;
			}

			break;
		}
			
		case WM_PAINT:
		{
			PAINTSTRUCT ps = {0};
			HDC dc = BeginPaint(hwnd, &ps);
			RECT rc = {0};

			GetClientRect(hwnd, &rc);
			rc.top = rc.bottom - GetSystemMetrics(SM_CYSIZE) * 2;

			COLORREF clrOld = SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
			ExtTextOut(dc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
			SetBkColor(dc, clrOld);

			for(INT i = 0; i < rc.right; i++)
			{
				SetPixel(dc, i, rc.top, RGB(223, 223, 223));
			}

			EndPaint(hwnd, &ps);

			break;
		}

		case WM_HOTKEY:
		{
			if(wparam == UID)
			{
				_Reduct_Start(hwnd);
			}

			break;
		}
		
		case WM_CTLCOLORSTATIC:
		case WM_CTLCOLORDLG:
		{
			return (INT_PTR)GetSysColorBrush(COLOR_WINDOW);
		}

		case WM_NOTIFY:
		{
			LPNMHDR nmlp = (LPNMHDR)lparam;

			switch(nmlp->code)
			{
				case NM_CUSTOMDRAW:
				{
					LONG result = CDRF_DODEFAULT;
					LPNMLVCUSTOMDRAW lpnmlv = (LPNMLVCUSTOMDRAW)lparam;

					switch(lpnmlv->nmcd.dwDrawStage)
					{
						case CDDS_PREPAINT:
						{
							result = (CDRF_NOTIFYPOSTPAINT | CDRF_NOTIFYITEMDRAW);
							break;
						}

						case CDDS_ITEMPREPAINT:
						{
							if(lpnmlv->nmcd.hdr.idFrom == IDC_LISTVIEW)
							{
								if((UINT)lpnmlv->nmcd.lItemlParam >= _r_cfg_read(L"DangerLevel", 90))
								{
									lpnmlv->clrText = COLOR_LEVEL_DANGER;
									//lpnmlv->clrTextBk = COLOR_LEVEL_DANGER;
								}
								else if((UINT)lpnmlv->nmcd.lItemlParam >= _r_cfg_read(L"WarningLevel", 60))
								{
									lpnmlv->clrText = COLOR_LEVEL_WARNING;
									//lpnmlv->clrTextBk = COLOR_LEVEL_DANGER;
								}

								result = (CDRF_NOTIFYPOSTPAINT | CDRF_NEWFONT);
								
							}

							break;
						}
					}

					SetWindowLongPtr(hwnd, 0 /*DWL_MSGRESULT*/, result);
					return TRUE;
				}
			}

			break;
		}

		case WM_SIZE:
		{
			if(wparam == SIZE_MINIMIZED)
			{
				_r_windowtoggle(hwnd, FALSE);
			}

			break;
		}

		case WM_SYSCOMMAND:
		{
			if(wparam == SC_CLOSE)
			{
				_r_windowtoggle(hwnd, FALSE);
				return TRUE;
			}

			break;
		}

		case WM_TRAYICON:
		{
			switch(LOWORD(lparam))
			{
				case WM_LBUTTONDBLCLK:
				{
					SendMessage(hwnd, WM_COMMAND, MAKELPARAM(IDM_TRAY_SHOW, 0), NULL);
					break;
				}

				case WM_RBUTTONUP:
				case WM_CONTEXTMENU:
				{
					HMENU menu = LoadMenu(NULL, MAKEINTRESOURCE(IDM_TRAY)), submenu = GetSubMenu(menu, 0);

					SetForegroundWindow(hwnd);

					POINT pt = {0};
					GetCursorPos(&pt);

					if(IsWindowVisible(hwnd))
					{
						MENUITEMINFO mii = {0};

						mii.cbSize = sizeof(mii);
						mii.fMask = MIIM_STRING;

						CString buffer = _r_locale(IDS_TRAY_HIDE);

						mii.dwTypeData = buffer.GetBuffer();
						mii.cch = buffer.GetLength();

						SetMenuItemInfo(submenu, IDM_TRAY_SHOW, FALSE, &mii);
					}

					TrackPopupMenuEx(submenu, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_LEFTBUTTON | TPM_NOANIMATION, pt.x, pt.y, hwnd, NULL);

					DestroyMenu(menu);
					DestroyMenu(submenu);

					break;
				}
			}

			break;
		}

		case WM_COMMAND:
		{
			switch(LOWORD(wparam))
			{
				case IDM_SETTINGS:
				case IDM_TRAY_SETTINGS:
				{
					DialogBox(NULL, MAKEINTRESOURCE(IDD_SETTINGS), hwnd, SettingsDlgProc);

					if(GetProp(hwnd, L"is_restart"))
					{
						_r_uninitialize(TRUE);
					}

					break;
				}

				case IDM_EXIT:
				case IDM_TRAY_EXIT:
				{
					DestroyWindow(hwnd);
					break;
				}

				case IDCANCEL: // process Esc key
				case IDM_TRAY_SHOW:
				{
					_r_windowtoggle(hwnd, FALSE);
					break;
				}

				case IDC_REDUCT:
				case IDM_TRAY_REDUCT:
				{
					DialogBox(NULL, MAKEINTRESOURCE(IDD_REDUCT), hwnd, ReductDlgProc);
/*
					if(cfg.bUnderUAC)
					{
						GetModuleFileName(NULL, buffer.GetBuffer(MAX_PATH), MAX_PATH);
						buffer.ReleaseBuffer();

						if(RunElevated(hwndDlg, buffer, L"/reduct"))
							DestroyWindow(hwndDlg);

						else
							ShowBalloonTip(NIIF_ERROR, APP_NAME, ls(cfg.hLocale, IDS_UAC_WARNING));

						return FALSE;
					}
*/
					break;
				}

				case IDM_WEBSITE:
				case IDM_TRAY_WEBSITE:
				{
					ShellExecute(hwnd, NULL, APP_WEBSITE L"/product/" APP_NAME_SHORT, NULL, NULL, 0);
					break;
				}

				case IDM_CHECKUPDATES:
				{
					_r_updatecheck(FALSE);
					break;
				}

				case IDM_ABOUT:
				case IDM_TRAY_ABOUT:
				{
					_r_aboutbox(hwnd);
					break;
				}
			}

			break;
		}
	}

	return FALSE;
}

INT APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, INT)
{
	if(_r_initialize((DLGPROC)DlgProc))
	{
		MSG msg = {0};

		while(GetMessage(&msg, NULL, 0, 0))
		{
			if(!IsDialogMessage(_r_hwnd, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
	}

	_r_uninitialize(FALSE);

	return ERROR_SUCCESS;
}