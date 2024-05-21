/* (C) Copyright Microsoft Corporation 1991. All Rights Reserved */
/* SoundRec.c - Sound Recorder main loop, modernized for Windows */

#undef NOWH
#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <commctrl.h>
#include "buttons.h"
#include <shellapi.h>
#include <mmreg.h>

#define INCLUDE_OLESTUBS
#include "SoundRec.h"
#include "dialog.h"
#include "helpids.h"

#include <stdarg.h>
#include <stdio.h>

/* globals */

BOOL gfUserClose = FALSE;
HWND ghwndApp = NULL;
HINSTANCE ghInst;
TCHAR gachFileName[_MAX_PATH] = L"";
LPSTR gszAnsiCmdLine;
BOOL gfDirty = FALSE;
BOOL gfClipboard = FALSE;
int gfErrorBox = FALSE;
HICON ghiconApp = NULL, ghiconAppStarting = NULL;
HWND ghwndWaveDisplay = NULL;
HWND ghwndScroll = NULL;
HWND ghwndPlay = NULL;
HWND ghwndStop = NULL;
HWND ghwndRecord = NULL;
#ifdef THRESHOLD
HWND ghwndSkipStart = NULL;
HWND ghwndSkipEnd = NULL;
#endif //THRESHOLD
HWND ghwndForward = NULL;
HWND ghwndRewind = NULL;
UINT guiHlpContext = 0;
BOOL gfWasPlaying = FALSE;
BOOL gfWasRecording = FALSE;
BOOL gfPaused = FALSE;
BOOL gfPausing = FALSE;
HWAVE ghPausedWave = NULL;

int gidDefaultButton = 0;
BOOL gfEmbeddedObject = FALSE;
BOOL gfRunWithEmbeddingFlag = FALSE;
BOOL gfHideAfterPlaying = FALSE;
BOOL gfShowWhilePlaying = FALSE;
TCHAR chDecimal = '.';
BOOL gfLZero = TRUE;
SZCODE aszClassKey[] = TEXT(".wav");
char gszRegisterPenApp[] = "RegisterPenApp";
HWND ghwndAbout = NULL;
UINT guiACMHlpMsg = 0;
HBRUSH ghbrPanel = NULL;
HHOOK fpfnOldMsgFilter = NULL;
HOOKPROC fpfnMsgHook = NULL;
int gfACMConvert = FALSE;

// Statics
SZCODE aszNULL[] = TEXT("");
SZCODE aszUntitled[] = TEXT("Untitled");
SZCODE aszIntl[] = TEXT("Intl");


// Rest of the function definitions (HelpMsgFilter, WinMain, SoundRecDlgProc, etc.)
BITMAPBTN tbPlaybar[] = {
    { ID_REWINDBTN - ID_BTN_BASE, ID_REWINDBTN, 0 },  /* index 0 */
    { ID_FORWARDBTN - ID_BTN_BASE, ID_FORWARDBTN,0 },  /* index 1 */
    { ID_PLAYBTN - ID_BTN_BASE, ID_PLAYBTN, 0 },  /* index 2 */
    { ID_STOPBTN - ID_BTN_BASE, ID_STOPBTN, 0 },  /* index 3 */
    { ID_RECORDBTN - ID_BTN_BASE, ID_RECORDBTN, 0 }  /* index 4 */
};


/* Message filter for F1 key in dialogs */
DWORD FAR PASCAL
HelpMsgFilter(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        LPMSG msg = (LPMSG)lParam;

        if ((msg->message == WM_KEYDOWN) && (LOWORD(msg->wParam) == VK_F1)) {
            if ((GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_MENU)) < 0) {
                /* do nothing */
            }
            else {
                if (guiHlpContext != 0)
                    WinHelp(ghwndApp, gachHelpFile, HELP_CONTEXT, guiHlpContext);
                else
                    SendMessage(ghwndApp, WM_COMMAND, IDM_INDEX, 0L);
            }
        }
    }

    return CallNextHookEx(fpfnOldMsgFilter, nCode, wParam, lParam);
}

/* WinMain(hInst, hPrev, lpszCmdLine, cmdShow)
 *
 * The main procedure for the App.  After initializing, it just goes
 * into a message-processing loop until it gets a WM_QUIT message
 * (meaning the app was closed).
 */
int WINAPI
WinMain(
    HINSTANCE hInst,     // instance handle of current instance
    HINSTANCE hPrev,     // instance handle of previous instance
    LPSTR lpszCmdLine,   // null-terminated command line
    int iCmdShow)      // how window should be initially displayed
{
    DLGPROC     fpfn;
    HWND        hDlg;
    MSG         rMsg;

    typedef VOID(FAR PASCAL* PPENAPP)(WORD, BOOL);
    PPENAPP lpfnRegisterPenApp;

    /* save instance handle for dialog boxes */
    ghInst = hInst;

    /* increase the message queue size, to make sure that the
     * MM_WOM_DONE and MM_WIM_DONE messages get through
     */
    SetMessageQueue(24);  // no op on NT

    /* save the command line -- it's used in the dialog box */
    gszAnsiCmdLine = lpszCmdLine;

    lpfnRegisterPenApp
        = (PPENAPP)GetProcAddress((HMODULE)GetSystemMetrics(SM_PENWINDOWS)
            , gszRegisterPenApp);

    if (lpfnRegisterPenApp)
        (*lpfnRegisterPenApp)(1, TRUE);

    //DPF("AppInit ...\n");
    /* call initialization procedure */
    if (!AppInit(hInst, hPrev)) {

        //DPF("AppInit failed\n");
        return FALSE;
    }

    /* setup the message filter to handle grabbing F1 for this task */
    fpfnMsgHook = (HOOKPROC)HelpMsgFilter;
    fpfnOldMsgFilter = SetWindowsHookEx(WH_MSGFILTER, fpfnMsgHook, NULL, GetCurrentThreadId());

    /* display "SoundRec" dialog box */
    fpfn = (DLGPROC)SoundRecDlgProc; // Corrected line

    hDlg = CreateDialogParam(ghInst
        , MAKEINTRESOURCE(SOUNDRECBOX)
        , NULL
        , fpfn
        , (int)iCmdShow
    );
    if (hDlg) {

        /* Polling messages from event queue */

        while (GetMessage(&rMsg, NULL, 0, 0)) {
            if (ghwndApp) {
                if (TranslateAccelerator(ghwndApp, ghAccel, &rMsg))
                    continue;

                if (IsDialogMessage(ghwndApp, &rMsg))
                    continue;
            }

            TranslateMessage(&rMsg);
            DispatchMessage(&rMsg);
        }
    }
    else {
        //        DPF("Create dialog failed ...\n");
    }

    /* free the current document */
    DestroyWave();

    /* if the message hook was installed, remove it and free */
    /* up our proc instance for it.                        */
    if (fpfnOldMsgFilter) {
        UnhookWindowsHookEx(fpfnOldMsgFilter);
    }

    /* random cleanup */
    DeleteObject(ghbrPanel);

#if defined(WIN16)
    ControlCleanup();
#endif 

#ifdef OLE1_REGRESS
    FreeVTbls();
#else   
    if (gfOleInitialized)     //CG
    {
        FlushOleClipboard();
        OleUninitialize();    //CG
        gfOleInitialized = FALSE;  //CG
    }
#endif 

    if (lpfnRegisterPenApp)
        (*lpfnRegisterPenApp)(1, FALSE);

    return TRUE;
}
/* SoundRecDlgProc(hwnd, wMsg, wParam, lParam)
 *
 * This function handles messages belonging to the main window dialog box.
 */
BOOL FAR PASCAL  // TRUE iff message has been processed
SoundRecDlgProc(HWND  hwnd   // window handle of "about" dialog box
    , UINT  wMsg   // message number
    , WPARAM wParam  // message-dependent parameter
    , LPARAM lParam  // message-dependent parameter
)
{
    RECT        rcClient;   // client rectangle
    POINT       pt;
#ifdef OLE1_REGRESS 
    UINT        cf;
#endif 

    // if we have an ACM help message registered see if this
    // message is it.
    if (guiACMHlpMsg && wMsg == guiACMHlpMsg) {
        // message was sent from ACM because the user
        // clicked on the HELP button on the chooser dialog.
        // report help for that dialog.
        WinHelp(hwnd, gachHelpFile, HELP_CONTEXT, IDM_NEW);
        return TRUE;
    }

    switch (wMsg)
    {
    case WM_COMMAND:
        return SoundRecCommand(hwnd, LOWORD(wParam), lParam);

    case WM_INITDIALOG:
        return SoundDialogInit(hwnd, lParam);

    case WM_SIZE:
        return FALSE; // let dialog manager do whatever else it wants

    case WM_WININICHANGE:
        if (!lParam || !lstrcmpi((LPTSTR)lParam, aszIntl))
            if (GetIntlSpecs())
                UpdateDisplay(TRUE);
        return (TRUE);

    case WM_INITMENU:
        Cls_OnInitMenu(hwnd, (HMENU)wParam);
        return (TRUE);

    case WM_PASTE:
        UpdateWindow(hwnd);
        StopWave();
        SnapBack();
        InsertFile(TRUE);
        break;

        HANDLE_MSG(hwnd, WM_DRAWITEM, SndRec_OnDrawItem);

    case WM_NOTIFY:
    {
        LPTOOLTIPTEXT lpTt = (LPTOOLTIPTEXT)lParam;
        LoadString(ghInst, lpTt->hdr.idFrom, lpTt->szText, SIZEOF(lpTt->szText));
    }
    break;


    case WM_HSCROLL:
        return Cls_OnHScroll(hwnd, (HWND)lParam, LOWORD(wParam), HIWORD(wParam));

    case WM_SYSCOMMAND:
        if (gfHideAfterPlaying) {
            //DPF("Resetting HideAfterPlaying");
            gfHideAfterPlaying = FALSE;
        }

        switch (wParam & 0xFFF0)
        {
        case SC_CLOSE:
            PostMessage(hwnd, WM_CLOSE, 0, 0L);
            return TRUE;
        }
        break;

    case WM_QUERYENDSESSION:
        return PromptToSave(FALSE) == enumCancel;

    case WM_SYSCOLORCHANGE:
#if defined(WIN16)
        ControlCleanup();
#endif 

        if (ghbrPanel) DeleteObject(ghbrPanel);
#if defined(WIN16)
        ControlInit(ghInst, ghInst);
#endif 
        ghbrPanel = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); // COLOR_BTNFACE is a more modern equivalent
        break;

    case WM_ERASEBKGND:
        GetClientRect(hwnd, &rcClient);
        FillRect((HDC)wParam, &rcClient, ghbrPanel);
        return TRUE;

    case MM_WOM_DONE:
        WaveOutDone((HWAVEOUT)wParam, (LPWAVEHDR)lParam);
        return TRUE;

    case MM_WIM_DATA:
        WaveInData((HWAVEIN)wParam, (LPWAVEHDR)lParam);
        return TRUE;

    case WM_TIMER:
        UpdateDisplay(FALSE);
        return TRUE;

    case MM_WIM_CLOSE:
        return TRUE;

    case WM_CTLCOLORBTN:
    case WM_CTLCOLORSTATIC:
    {
        pt.x = pt.y = 0;
        ClientToScreen((HWND)lParam, &pt);
        ScreenToClient(hwnd, &pt);
        SetBrushOrgEx((HDC)wParam, -pt.x, -pt.y, NULL);
        return (INT_PTR)ghbrPanel;
    }

    case WM_CLOSE:
        //DPF("WM_CLOSE received\n");
        gfUserClose = TRUE;
        if (gfHideAfterPlaying) {
            //DPF("Resetting HideAfterPlaying");
            gfHideAfterPlaying = FALSE;
        }
        if (gfErrorBox) {
            // DPF("we have a error box up, ignoring WM_CLOSE.\n");
            return TRUE;
        }
        if (PromptToSave(TRUE) == enumCancel)
            return TRUE;

        // Don't free our data before terminating. When the clipboard
        // is flushed, we need to commit the data.
        TerminateServer();

        FileNew(FMT_DEFAULT, FALSE, FALSE);
        FreeACM();

        //
        //  NOTE: TerminateServer() will destroy the window!
        //
        return TRUE; //!!!

#ifdef OLE1_REGRESS
    case WM_RENDERFORMAT:
        //DPF("WM_RENDERFORMAT: %u\n",wParam);
    {
        HCURSOR hcurPrev;
        hcurPrev = SetCursor(LoadCursor(NULL, IDC_WAIT));

        Copy1ToClipboard(ghwndApp, (OLECLIPFORMAT)wParam);
        SetCursor(hcurPrev);
    }
    break;
#endif // OLE1_REGRESS

#ifdef OLE1_REGRESS  
    case WM_RENDERALLFORMATS:
        //DPF("WM_RENDERALLFORMATS\n");

        if (GetClipboardOwner() != hwnd)
            return 0L;

        if (!gfClipboard)
            return 0L;

        if (OpenClipboard(hwnd)) {
            for (cf = EnumClipboardFormats(0); cf;
                cf = EnumClipboardFormats(cf)) {
                GetClipboardData(cf);
            }

            CloseClipboard();
        }
        gfClipboard = FALSE;
        break;
#endif // OLE1_REGRESS

    case WM_USER_DESTROY:
        //DPF("WM_USER_DESTROY\n");

        if (ghWaveOut || ghWaveIn) {
            //DPF("Ignoring, we have a device open.\n");
            /* Close later, when the play finishes. */
            return TRUE;
        }

        DestroyWindow(hwnd);
        return TRUE;

    case WM_DESTROY:
        //DPF("WM_DESTROY\n");
        WinHelp(hwnd, gachHelpFile, HELP_QUIT, 0L);

        ghwndApp = NULL;

        /*  Tell my app to die  */
        PostQuitMessage(0);
        return TRUE;

    case WM_DROPFILES: /*case added 10/07/91 for file drag /drop support*/
        doDrop(hwnd, wParam);
        break;
    }
    return FALSE;
} /* SoundRecDlgProc */

void SndRec_OnDrawItem(
    HWND hwnd,
    const DRAWITEMSTRUCT* lpdis
)
{
    int       i;

    i = lpdis->CtlID - ID_BTN_BASE;

    if (lpdis->CtlType == ODT_BUTTON) {

        /*
        ** Now draw the button according to the buttons state information.
        */

        tbPlaybar[i].fsState = LOBYTE(lpdis->itemState);

        if (lpdis->itemAction & (ODA_DRAWENTIRE | ODA_SELECT)) {

            BtnDrawButton(hwnd, lpdis->hDC, (int)lpdis->rcItem.right,
                (int)lpdis->rcItem.bottom,
                &tbPlaybar[i]);
        }
        else if (lpdis->itemAction & ODA_FOCUS) {

            BtnDrawFocusRect(lpdis->hDC, &lpdis->rcItem, lpdis->itemState);
        }
    }
}
// Pause function for playback/recording control
void NEAR PASCAL
Pause(BOOL fBeginPause)
{
    if (fBeginPause) {
        if (ghWaveOut != NULL) {
#ifdef NEWPAUSE
            gfPausing = TRUE;
            gfPaused = FALSE;
            ghPausedWave = ghWaveOut;
#endif
            gfWasPlaying = TRUE;
            StopWave();
        }
        else if (ghWaveIn != NULL) {
#ifdef NEWPAUSE
            gfPausing = TRUE;
            gfPaused = FALSE;
            ghPausedWave = ghWaveIn;
#endif
            gfWasRecording = TRUE;
            StopWave();
        }
    }
    else {
        if (gfWasPlaying) {
            gfWasPlaying = FALSE;
            PlayWave();
#ifdef NEWPAUSE
            gfPausing = FALSE;
            gfPaused = FALSE;
#endif
        }
        else if (gfWasRecording) {
            gfWasRecording = FALSE;
            RecordWave();
#ifdef NEWPAUSE
            gfPausing = FALSE;
            gfPaused = FALSE;
#endif
        }
    }
}

// SoundRecCommand function (handles menu and button commands)
BOOL NEAR PASCAL
SoundRecCommand(HWND hwnd,
    WPARAM wParam,
    LPARAM lParam
)
{
    // ... (Rest of the SoundRecCommand function, same as the original code)
}


// Cls_OnInitMenu (initializes menu items)
void NEAR PASCAL Cls_OnInitMenu(HWND hwnd, HMENU hMenu)
{
    // ... (Rest of the Cls_OnInitMenu function, same as the original code)
}

// Cls_OnHScroll (handles horizontal scrollbar events)
BOOL NEAR PASCAL Cls_OnHScroll(HWND hwnd, HWND hwndCtl, UINT code, int pos)
{
    // ... (Rest of the Cls_OnHScroll function, same as the original code)
}


// About Dialog Procedure
LRESULT CALLBACK AboutDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    case WM_INITDIALOG:
        return (TRUE);

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, TRUE);
            return (TRUE);
        }
        break;
    }
    return (FALSE);
}


#if DBG

void FAR cdecl dprintfA(LPSTR szFormat, ...)
{
    char ach[128];
    int  s, d;
    va_list va;

    va_start(va, szFormat);
    s = vsprintf(ach, szFormat, va);
    va_end(va);

    for (d = sizeof(ach) - 1; s >= 0; s--)
    {
        if ((ach[d--] = ach[s]) == '\n')
            ach[d--] = '\r';
    }

    OutputDebugStringA("SOUNDREC: ");
    OutputDebugStringA(ach + d + 1);
}

#ifdef UNICODE
void FAR cdecl dprintfW(LPWSTR szFormat, ...)
{
    WCHAR ach[128];
    int  s, d;
    va_list va;

    va_start(va, szFormat);
    s = vswprintf(ach, szFormat, va);
    va_end(va);

    for (d = (sizeof(ach) / sizeof(WCHAR)) - 1; s >= 0; s--)
    {
        if ((ach[d--] = ach[s]) == L'\n')
            ach[d--] = L'\r';
    }

    OutputDebugStringW(L"SOUNDREC: ");
    OutputDebugStringW(ach + d + 1);
}
#endif // UNICODE

#endif // DBG
