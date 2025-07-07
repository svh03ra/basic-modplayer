/* 
  ===============================================
    File : main.cpp
    Author : svh03ra
    Created : 7-Jul-2025 (‏‎12:56:39 AM)
	// Program /* Alpha 1 /*
  ===============================================
            This is an alpha version.
      It may be unstable as it still contains
       several limitations and some issues!

              Use at your own risk.

          (C) 2025, All rights reserved
  ===============================================
*/

// Include standard headers
#include <windows.h>
#include <excpt.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <csignal>

// Include custom headers for MOD loading and engine
#include "mod_loader.h"
#include "mod_player_engine.h"

// Define title and GUI control IDs
#define TITLE_WINDOW "MOD Player Alpha"
#define VERSION_TEXT "// MOD Player /* Alpha 1"
#define ID_BTN_BROWSE        1001
#define ID_BTN_RESET         1002
#define ID_BTN_STOP          1003
#define ID_EDIT_BUFFER_SIZE  2001
#define ID_EDIT_BUFFER_COUNT 2002
#define ID_EDIT_SAMPLE_RATE  2003
#define ID_EDIT_CHANNELS     2004
#define ID_BTN_RESTORE       2005
#define ID_BTN_APPLY         2006

// Global variables for engine, mod file and playback state
MODFile modFile;
ModEngine engine;
bool running = true;
bool isPlaying = false;
char statusText[128] = "Audio Stopped!";  // Display status at bottom of GUI
char loadedFileName[MAX_PATH] = "";       // Used for window title after loading MOD

// GUI component handles
HWND hwndBrowse, hwndReset;
HWND hwndBufSize, hwndBufCount, hwndSampleRate, hwndChannels;
HWND hwndRestore, hwndApply, hwndStop;

// For calculating frame rate (FPS)
DWORD frameTimes[60] = {};
int frameIndex = 0;

// Debug log output (both to debug console and stdout)
void debug(const char* msg) {
    char buffer[256];
    sprintf(buffer, "[DEBUG]: %s\n", msg);
    OutputDebugStringA(buffer);
    printf("%s", buffer);
    fflush(stdout);
}

// FPS calculator using ring buffer of timestamps
int calculateFPS() {
    DWORD now = GetTickCount();
    DWORD prev = frameTimes[frameIndex];
    frameTimes[frameIndex] = now;
    frameIndex = (frameIndex + 1) % 60;
    if (prev == 0) return 0;
    DWORD delta = now - prev;
    return delta ? (1000 * 60 / delta) : 0;
}

// Signal handler to catch crashes (SIGSEGV, SIGABRT, ETC)
void signalHandler(int signal) {
    const char* signalName = "Unknown";
    const char* signalDesc = "No description available.";

    switch (signal) {
    case SIGSEGV:
        signalName = "SIGSEGV";
        signalDesc = "Segmentation Fault! (invalid memory access)";
        break;
    case SIGABRT:
        signalName = "SIGABRT";
        signalDesc = "Abort signal! from abort()";
        break;
    case SIGFPE:
        signalName = "SIGFPE";
        signalDesc = "Floating-point exception! (divide by zero?)";
        break;
    case SIGILL:
        signalName = "SIGILL";
        signalDesc = "Illegal instruction!";
        break;
    case SIGINT:
        signalName = "SIGINT";
        signalDesc = "Interrupt signal!";
        break;
    case SIGTERM:
        signalName = "SIGTERM";
        signalDesc = "Termination signal!";
        break;
    }

    char debugMsg[256];
    sprintf(debugMsg, "Program crashed! Signal: %s (%d) - %s", signalName, signal, signalDesc);
    debug(debugMsg);  // log to console and OutputDebugString

    char msgBoxText[512];
    sprintf(msgBoxText,
        "Oops! The program has crashed.\n"
        "Possible internal error or bug.\n\n"
        //"Signal: %s (%d)\n"
        "Reason: %s\n"
        "Please make sure to restart the application.",
        //signalName, signal,
		signalDesc);

    MessageBoxA(NULL, msgBoxText, "Crash Occurred!!!", MB_ICONERROR | MB_OK);

    exit(1);  // force-exit after crash
}

// Enable or disable all GUI controls
void setGUIEnabled(bool enabled) {
    debug(enabled ? "Unlocking GUI controls" : "Locking GUI controls");
    EnableWindow(hwndBrowse, enabled);
    EnableWindow(hwndReset, enabled);
    EnableWindow(hwndStop, enabled);
    EnableWindow(hwndBufSize, enabled);
    EnableWindow(hwndBufCount, enabled);
    EnableWindow(hwndSampleRate, enabled);
    EnableWindow(hwndChannels, enabled);
    EnableWindow(hwndRestore, enabled);
    EnableWindow(hwndApply, enabled);
}

// GUI drawing logic to render current pattern and playback stats
void renderPattern(HWND hwnd, int row, int pattern) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Clear background
    RECT rect;
    GetClientRect(hwnd, &rect);
    FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));

    // Set text properties
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    // Display pattern ID and row
    char buf[128];
    wsprintf(buf, "Pattern ID: %02d", pattern);
    TextOut(hdc, 10, 10, buf, lstrlen(buf));
    wsprintf(buf, "Row: %02d", row);
    TextOut(hdc, 10, 30, buf, lstrlen(buf));

    // Display per-channel state
    for (int ch = 0; ch < engine.getChannels(); ++ch) {
        ChannelState& state = engine.channels[ch];
        sprintf(buf, "CH%d: Sample %d Freq %.1f Vol %.2f", ch, state.sampleIndex, state.freq, state.volume);
        TextOutA(hdc, 10, 60 + ch * 20, buf, strlen(buf));
    }

    // Latency and buffer info
    int yOffset = 60 + engine.getChannels() * 20 + 10;
    int latencyMs = static_cast<int>((1000.0 * engine.getBufferSize() * engine.getBufferCount()) / engine.getSampleRate());
    sprintf(buf, "Audio Latency: %d ms", latencyMs);
    TextOutA(hdc, 10, yOffset, buf, strlen(buf));
    sprintf(buf, "Audio Buffer: %d", engine.getCurrentBuffer());
    TextOutA(hdc, 10, yOffset + 20, buf, strlen(buf));
    sprintf(buf, "Audio FPS: %d fps", calculateFPS());
    TextOutA(hdc, 10, yOffset + 40, buf, strlen(buf));

    // Draw EQ bars
    if (isPlaying) {
        int barX = 10;
        int barY = rect.bottom - 190;
        int barWidth = 12;
        int barSpacing = 6;
        int maxBarHeight = 80;
        int segments = 16;
        int segmentHeight = maxBarHeight / segments;

        for (int ch = 0; ch < engine.getChannels(); ++ch) {
            float vol = engine.channels[ch].volume;
            int activeSegments = static_cast<int>(vol * segments);

            for (int s = 0; s < activeSegments; ++s) {
                float t = (float)s / (segments - 1);
                int r = (t < 0.5f) ? static_cast<int>(t * 2 * 255) : 255;
                int g = (t < 0.5f) ? 255 : static_cast<int>((1.0f - (t - 0.5f) * 2) * 255);

                RECT seg = {
                    barX + ch * (barWidth + barSpacing),
                    barY + maxBarHeight - (s + 1) * segmentHeight,
                    barX + ch * (barWidth + barSpacing) + barWidth,
                    barY + maxBarHeight - s * segmentHeight
                };

                HBRUSH brush = CreateSolidBrush(RGB(r, g, 0));
                FillRect(hdc, &seg, brush);
                DeleteObject(brush);
            }
        }
    }

    // Clear footer area
    RECT footer;
    footer.left = 0;
    footer.top = rect.bottom - 110;
    footer.right = rect.right;
    footer.bottom = rect.bottom;
    FillRect(hdc, &footer, (HBRUSH)(COLOR_WINDOW + 1));

    // Draw warning message (in red)
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutA(hdc, 10, rect.bottom - 95, "WARNING!", 8);
    TextOutA(hdc, 10, rect.bottom - 80, "This is an alpha version and may have limitations", 49);
    TextOutA(hdc, 10, rect.bottom - 65, "still having some issues.", 25);

    // Draw status message (in black)
    SetTextColor(hdc, RGB(0, 0, 0));
    TextOutA(hdc, 10, rect.bottom - 45, statusText, strlen(statusText));

    // Draw version message
    SIZE size;
    GetTextExtentPoint32A(hdc, VERSION_TEXT, strlen(VERSION_TEXT), &size);
    TextOutA(hdc, rect.right - size.cx - 10, rect.bottom - size.cy - 10, VERSION_TEXT, strlen(VERSION_TEXT));

    EndPaint(hwnd, &ps);
}

// Apply user-defined audio settings and restart engine
void applyAudioSettings() {
    debug("Apply button clicked: Applying audio settings");
    setGUIEnabled(false);
    SetCursor(LoadCursor(NULL, IDC_WAIT));

    // Read settings from GUI
    char buf[32];
    GetWindowTextA(hwndBufSize, buf, sizeof(buf));
    int size = atoi(buf);
    GetWindowTextA(hwndBufCount, buf, sizeof(buf));
    int count = atoi(buf);
    GetWindowTextA(hwndSampleRate, buf, sizeof(buf));
    int rate = atoi(buf);
    GetWindowTextA(hwndChannels, buf, sizeof(buf));
    int channels = atoi(buf);

    // Apply settings to engine
    debug("Stopping engine to apply new settings");
    engine.stop();
    debug("Setting engine parameters");
    engine.setBufferSize(size);
    engine.setBufferCount(count);
    engine.setSampleRate(rate);
    engine.setOutputChannels(channels);

    // Reload and start engine
    debug("Loading MOD file to engine");
    engine.load(&modFile);
    debug("Starting engine");
    engine.start();

    isPlaying = true;
    strcpy(statusText, "Audio Playing...");

    setGUIEnabled(true);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    debug("Settings applied successfully");
}

// File dialog to browse and load a MOD file
void browseAndLoad(HWND hwnd) {
    debug("Browse button clicked");
    setGUIEnabled(false);
    SetCursor(LoadCursor(NULL, IDC_WAIT));

    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "MOD Files (*.mod)\0*.mod\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Select a MOD file";

    // If file selected, load into engine
    if (GetOpenFileNameA(&ofn)) {
        char debugMsg[MAX_PATH + 32];
        sprintf(debugMsg, "Opened file: %s", filename);
        debug(debugMsg);

        debug("Preparing to load MOD file");
        engine.stop();
        modFile = loadMODFile(filename);
        engine.load(&modFile);
        engine.start();
        isPlaying = true;
        strcpy(statusText, "Audio Playing...");

        // Update window title
        strncpy(loadedFileName, filename, MAX_PATH);
        const char* justName = strrchr(loadedFileName, '\\');
        if (!justName) justName = loadedFileName; else justName++;

        char newTitle[256];
        sprintf(newTitle, TITLE_WINDOW " - %s", justName);
        SetWindowTextA(hwnd, newTitle);

        debug("MOD file loaded and playback started");
    }

    setGUIEnabled(true);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

// Main window message handler
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // Create all GUI elements
        debug("Initializing GUI controls");
        hwndBrowse = CreateWindow("BUTTON", "Browse", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 10, 100, 25, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);
        hwndReset = CreateWindow("BUTTON", "Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 45, 100, 25, hwnd, (HMENU)ID_BTN_RESET, NULL, NULL);
        hwndStop = CreateWindow("BUTTON", "Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            470, 10, 100, 25, hwnd, (HMENU)ID_BTN_STOP, NULL, NULL);
        CreateWindow("STATIC", "Set to Audio Buffer Size:", WS_CHILD | WS_VISIBLE,
            360, 80, 160, 20, hwnd, NULL, NULL, NULL);
        hwndBufSize = CreateWindow("EDIT", "1024", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            360, 100, 100, 20, hwnd, (HMENU)ID_EDIT_BUFFER_SIZE, NULL, NULL);
        CreateWindow("STATIC", "Set to Audio Buffer Count:", WS_CHILD | WS_VISIBLE,
            360, 125, 160, 20, hwnd, NULL, NULL, NULL);
        hwndBufCount = CreateWindow("EDIT", "4", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            360, 145, 100, 20, hwnd, (HMENU)ID_EDIT_BUFFER_COUNT, NULL, NULL);
        CreateWindow("STATIC", "Set to Audio Sample Rate:", WS_CHILD | WS_VISIBLE,
            360, 170, 160, 20, hwnd, NULL, NULL, NULL);
        hwndSampleRate = CreateWindow("EDIT", "48000", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            360, 190, 100, 20, hwnd, (HMENU)ID_EDIT_SAMPLE_RATE, NULL, NULL);
        CreateWindow("STATIC", "Set to Audio Channels:", WS_CHILD | WS_VISIBLE,
            360, 215, 160, 20, hwnd, NULL, NULL, NULL);
        hwndChannels = CreateWindow("EDIT", "4", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER,
            360, 235, 100, 20, hwnd, (HMENU)ID_EDIT_CHANNELS, NULL, NULL);
        hwndRestore = CreateWindow("BUTTON", "Restore Changes", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 265, 120, 25, hwnd, (HMENU)ID_BTN_RESTORE, NULL, NULL);
        hwndApply = CreateWindow("BUTTON", "Apply Settings", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            360, 295, 120, 25, hwnd, (HMENU)ID_BTN_APPLY, NULL, NULL);
        debug("GUI controls created");
        break;

    case WM_COMMAND:
        // Handle button actions
        switch (LOWORD(wParam)) {
        case ID_BTN_BROWSE: browseAndLoad(hwnd); break;
        case ID_BTN_RESET:
            debug("Reset button clicked");
            engine.stop();
            engine.load(&modFile);
            engine.start();
            isPlaying = true;
            strcpy(statusText, "Audio Playing...");
            break;
        case ID_BTN_RESTORE:
            debug("Restore button clicked");
            SetWindowTextA(hwndBufSize, "1024");
            SetWindowTextA(hwndBufCount, "2");
            SetWindowTextA(hwndSampleRate, "44100");
            SetWindowTextA(hwndChannels, "4");
            break;
        case ID_BTN_APPLY: applyAudioSettings(); break;
        case ID_BTN_STOP:
            debug("Stop button clicked");
            engine.stop();
            isPlaying = false;
            strcpy(statusText, "Audio Stopped!");
            break;
        }
        break;

    case WM_PAINT:
        renderPattern(hwnd, engine.currentRow, engine.currentPattern);
        break;

    case WM_TIMER:
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case WM_DESTROY:
        debug("Exiting program and stopping engine");
        engine.stop();
        running = false;
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Program entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    AllocConsole();  // Enable debug console
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    SetConsoleTitle("MOD Player Debug Window");

    debug("Initializing MOD Player...");

	// Catch crash signals
	signal(SIGSEGV, signalHandler);
	signal(SIGABRT, signalHandler);
	signal(SIGFPE,  signalHandler);
	signal(SIGILL,  signalHandler);
	signal(SIGINT,  signalHandler);
	signal(SIGTERM, signalHandler);

    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "MODWindow";
    RegisterClass(&wc);

    // Create main window
    HWND hwnd = CreateWindowEx(0, "MODWindow", TITLE_WINDOW, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 450, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    debug("GUI initialized and event loop started");

    SetTimer(hwnd, 1, 1000 / 60, NULL);  // 60 FPS refresh timer

    // Run message loop
    MSG msg;
    while (running && GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    debug("MOD Player terminated");
    return 0;
}