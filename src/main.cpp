/* 
  ===============================================
    File : main.cpp
    Author : svh03ra
    Created : 13-Jul-2025 (‏‎11:44:00 PM)
	// Program /* Beta 1 /*
  ===============================================
             This is an beta version.
         You might experience instability!

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
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")  // Link with Common Controls library

// Include custom headers for MOD loading and engine
#include "mod_loader.h"
#include "mod_player_engine.h"

#include "api_helper.h"

// Define title and GUI control IDs
#define TITLE_WINDOW "MOD Player Beta"
#define VERSION_TEXT "// MOD Player /* Beta 1"

// Window size
#define WINDOW_WIDTH 770
#define WINDOW_HEIGHT 570

#define ID_BTN_BROWSE        1001
#define ID_BTN_RESET         1002
#define ID_BTN_STOP          1003
#define ID_BTN_TOGGLE_SETTINGS 1009
#define ID_EDIT_BUFFER_SIZE  2001
#define ID_EDIT_BUFFER_COUNT 2002
#define ID_EDIT_SAMPLE_RATE  2003
#define ID_EDIT_CHANNELS     2004
#define ID_BTN_RESTORE       2005
#define ID_BTN_APPLY         2006

// Playback silder
#define ID_BTN_PLAY          3001
#define ID_BTN_PAUSE         3002
#define ID_BTN_SEEK_BACK     3003
#define ID_BTN_SEEK_FORWARD  3004
#define ID_SLIDER_SEEK       3005

#define ID_BTN_MUTE_CH1   4001
#define ID_BTN_MUTE_CH2   4002
#define ID_BTN_MUTE_CH3   4003
#define ID_BTN_MUTE_CH4   4004

// Global variables for engine, mod file and playback state
MODFile modFile;
ModEngine engine;
bool running = true;
bool isPlaying = false;
bool isChannelMuted[4] = { false, false, false, false };
char statusText[128] = "Audio Stopped!";  // Display status at bottom of GUI
char loadedFileName[MAX_PATH] = "";       // Used for window title after loading MOD

extern HFONT hSegoeUIFont;

// GUI component handles
HWND hwndBrowse, hwndReset, hwndStop;
HWND hwndBufSize, hwndBufCount, hwndSampleRate, hwndChannels;
HWND hwndLabelBufSize, hwndLabelBufCount, hwndLabelSampleRate, hwndLabelChannels;
HWND hwndRestore, hwndApply;
HWND hwndToggleSettings;


HWND hwndChannelLabel[4];
HWND hwndChannelLight[4];
HWND hwndChannelMute[4];
HWND hwndLabelAdjustChannels;
HWND hwndBtnSeekBack = nullptr;
HWND hwndBtnPause = nullptr;
HWND hwndBtnSeekForward = nullptr;

// Settings visibility state
bool settingsVisible = false;

// For calculating frame rate (FPS)
DWORD frameTimes[60] = {};
int frameIndex = 0;

// Enum to represent playback button state
enum PlaybackButtonState {
	STATE_PLAYING,
	STATE_PAUSED
};

// Variable to track current playback state
PlaybackButtonState playbackButtonState = STATE_PLAYING;

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

    exit(1);
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

// Updates playback-related GUI buttons
void updatePlaybackButtons(HWND hwnd, bool enabled) {
    EnableWindow(hwndReset, enabled);
    EnableWindow(hwndStop, enabled);
    EnableWindow(hwndBtnSeekBack, enabled);
    EnableWindow(hwndBtnPause, enabled);
    EnableWindow(hwndBtnSeekForward, enabled);
	//EnableWindow(hSlider, TRUE);
    EnableWindow(GetDlgItem(hwnd, ID_SLIDER_SEEK), enabled);
	
	debug(enabled ? "Playback buttons enabled" : "Playback buttons disabled");
}

// Toggles playback buttons from string mode
void updatePlaybackButtonsFromString(HWND hwnd, const char* mode) {
    if (_stricmp(mode, "enabled") == 0) {
        updatePlaybackButtons(hwnd, true);
    } else if (_stricmp(mode, "disabled") == 0) {
        updatePlaybackButtons(hwnd, false);
    } else {
        debugf("Invalid mode in updatePlaybackButtonsFromString: %s", mode);
    }
}

// Update the playback slider position to match the current row
void updatePlaybackSlider(HWND hwnd) {
    HWND hSlider = GetDlgItem(hwnd, ID_SLIDER_SEEK);
    SendMessage(hSlider, TBM_SETPOS, TRUE, engine.currentRow);
}

// GUI drawing logic to render current pattern and playback stats
void renderPattern(HWND hwnd, int row, int pattern) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HFONT oldFont = (HFONT)SelectObject(hdc, hSegoeUIFont);

    RECT rect;
    GetClientRect(hwnd, &rect);
    FillRect(hdc, &rect, (HBRUSH)(COLOR_WINDOW + 1));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    char buf[128];
    wsprintf(buf, "Pattern ID: %02d | Row: %02d", pattern, row);
    TextOut(hdc, 10, 10, buf, lstrlen(buf));

    for (int ch = 0; ch < engine.getChannels(); ++ch) {
        ChannelState& state = engine.channels[ch];
        sprintf(buf, "CH%d: Sample %d Freq %.1f Vol %.2f", ch, state.sampleIndex, state.freq, state.volume);
        TextOutA(hdc, 10, 40 + ch * 20, buf, strlen(buf));
    }

    int yOffset = 60 + engine.getChannels() * 20 + 10;
    int latencyMs = static_cast<int>((1000.0 * engine.getBufferSize() * engine.getBufferCount()) / engine.getSampleRate());
    sprintf(buf, "Audio Latency: %d ms", latencyMs);
    TextOutA(hdc, 10, yOffset, buf, strlen(buf));
    sprintf(buf, "Audio Buffer: %d", engine.getCurrentBuffer());
    TextOutA(hdc, 10, yOffset + 20, buf, strlen(buf));
    sprintf(buf, "Audio FPS: %d fps", calculateFPS());
    TextOutA(hdc, 10, yOffset + 40, buf, strlen(buf));

	// Draw EQ visualizer
	if (isPlaying) {
		int barX = 10;
		int barY = rect.bottom - 270;
		int barWidth = 12;
		int barSpacing = 6;
		int maxBarHeight = 80;
		int segments = 16;
		int segmentHeight = maxBarHeight / segments;

		bool anySolo = engine.anyChannelSoloed();

		for (int ch = 0; ch < engine.getChannels(); ++ch) {
			// Skip channel if muted or not in solo group (when any channel is soloed)
			if (engine.isChannelMuted(ch)) continue;
			if (anySolo && !engine.isChannelSoloed(ch)) continue;

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

    RECT footer;
    footer.left = 0;
    footer.top = rect.bottom - 110;
    footer.right = rect.right;
    footer.bottom = rect.bottom;
    FillRect(hdc, &footer, (HBRUSH)(COLOR_WINDOW + 1));

    // Bold red text
    HFONT hBoldRed = CreateFont(
        -18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, TEXT("Segoe UI")
    );
    HFONT oldRed = (HFONT)SelectObject(hdc, hBoldRed);
    SetTextColor(hdc, RGB(255, 0, 0));
    TextOutA(hdc, 10, rect.bottom - 100, "PLEASE NOTE:", 12);
    TextOutA(hdc, 10, rect.bottom - 80, "This is a beta version and may experience instability!", 54);
    SelectObject(hdc, oldRed);
    DeleteObject(hBoldRed);

    SetTextColor(hdc, RGB(0, 0, 0));
    TextOutA(hdc, 10, rect.bottom - 30, statusText, strlen(statusText));

    SIZE size;
    GetTextExtentPoint32A(hdc, VERSION_TEXT, strlen(VERSION_TEXT), &size);
    TextOutA(hdc, rect.right - size.cx - 10, rect.bottom - size.cy - 10, VERSION_TEXT, strlen(VERSION_TEXT));

    SelectObject(hdc, oldFont);
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
    debugf("Set Buffer Size: %d", size);

    GetWindowTextA(hwndBufCount, buf, sizeof(buf));
    int count = atoi(buf);
    debugf("Set Buffer Count: %d", count);

    GetWindowTextA(hwndSampleRate, buf, sizeof(buf));
    int rate = atoi(buf);
    debugf("Set Sample Rate: %d", rate);

    GetWindowTextA(hwndChannels, buf, sizeof(buf));
    int channels = atoi(buf);
    debugf("Set Output Channels: %d", channels);

    // Apply settings to engine
    debug("Stopping engine to apply new settings");
    engine.stop();
    debug("Setting engine parameters");
    engine.setBufferSize(size);
    engine.setBufferCount(count);
    engine.setSampleRate(rate);
    engine.setOutputChannels(channels);

    // Reload and start engine
	if (modFile.patterns.empty()) {
		debug("No MOD file loaded - skipping engine start");
		strcpy(statusText, "No MOD file loaded.");
	} else {
		debug("Loading MOD file to engine");
		if (engine.load(&modFile)) {
			debug("Starting engine");
			engine.start();
			isPlaying = true;
			strcpy(statusText, "Audio Playing...");
		} else {
			debug("Engine failed to load MOD after apply.");
			strcpy(statusText, "Failed to apply settings.");
		}
	}
    setGUIEnabled(true);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    debug("Settings applied successfully");
}

// File dialog to browse and load a MOD file
void browseAndLoad(HWND hwnd) {
    debug("Browse button clicked");
    setGUIEnabled(false);
    SetCursor(LoadCursor(NULL, IDC_WAIT));

    static DWORD lastLoadTime = 0;
    DWORD now = GetTickCount();
    if (now - lastLoadTime < 500) {
        debug("Ignored load: User clicked too fast!");
        setGUIEnabled(true);
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return;
    }
    lastLoadTime = now;

    char filename[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "MOD Files (*.mod)\0*.mod\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrTitle = "Select a MOD file";

    if (GetOpenFileNameA(&ofn)) {
        char debugMsg[MAX_PATH + 32];
        sprintf(debugMsg, "Opened file: %s", filename);
        debug(debugMsg);

        debug("Stopping and resetting engine before loading new file");
        engine.stop();
        engine.reset();
        modFile = {};  // Clear old MOD data
        isPlaying = false;
		updatePlaybackButtons(hwnd, false);

        debug("Preparing to load MOD file");
        MODFile newMod = loadMODFile(filename);

        if (!newMod.patterns.empty()) {
            modFile = std::move(newMod);  // Efficient and safe replacement

            if (engine.load(&modFile)) {
                engine.start();
                isPlaying = true;
                strcpy(statusText, "Audio Playing...");

                // Set window title
                strncpy(loadedFileName, filename, MAX_PATH);
                const char* justName = strrchr(loadedFileName, '\\');
                justName = justName ? justName + 1 : loadedFileName;

                char newTitle[256];
                sprintf(newTitle, TITLE_WINDOW " - %s", justName);
                SetWindowTextA(hwnd, newTitle);
				updatePlaybackButtons(hwnd, true);

				// Set seek slider range
				HWND hSlider = GetDlgItem(hwnd, ID_SLIDER_SEEK);
				int totalRows = static_cast<int>(modFile.patterns[engine.currentPattern].notes.size());

				SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, totalRows - 1));
				SendMessage(hSlider, TBM_SETPOS, TRUE, 0);
				updatePlaybackButtons(hwnd, true);


                debug("MOD file loaded and playback started");
            } else {
                debug("Engine failed to load MOD file.");
                strcpy(statusText, "Failed to load MOD.");
            }
        } else {
            debug("Failed to parse/load MOD file.");
            strcpy(statusText, "Invalid MOD file.");
        }
    }

    setGUIEnabled(true);
    SetCursor(LoadCursor(NULL, IDC_ARROW));
}

void SetFontRecursive(HWND parent, HFONT font) {
    HWND child = GetWindow(parent, GW_CHILD);
    while (child) {
        SendMessage(child, WM_SETFONT, (WPARAM)font, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

HFONT hSegoeUIFont = nullptr;  // Global or static variable

// Main window message handler
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    // Layout constants
    const int LABEL_X = 360;
    const int LABEL_WIDTH = 200;
    const int EDIT_X = LABEL_X + LABEL_WIDTH + 10;
    const int EDIT_WIDTH = 100;
    const int HEIGHT = 20;
    int y = 80;
    const int GAP = 30;

    // Top buttons
    int topButtonY = 10;
    int topButtonW = 120;
    int topButtonH = 25;
    int topButtonGap = 10;
    int topStartX = 360;

    // Bottom buttons
    int bottomButtonY = 295;
    int bottomButtonW = 150;
    int bottomButtonH = 25;
    int bottomGap = 20;
    int bottomStartX = 360;

	// Playback buttons
	int playbackButtonY = 380;
	int playbackButtonW = 40;
	int playbackButtonH = 25;
	int playbackButtonSpacing = 10;
	int playbackButtonStartX = 120;

    // Toggle settings buttons
    int toggleBtnWidth = 150;
    int totalTopWidth = (3 * topButtonW) + (2 * topButtonGap);
    int topGroupCenter = topStartX + (totalTopWidth / 2);
    int toggleBtnX = topGroupCenter - (toggleBtnWidth / 2);
    int toggleBtnY = topButtonY + topButtonH + 10;

    switch (msg) {

    case WM_DRAWITEM: {
        LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;

        // Check if it's one of the channel light boxes
        for (int i = 0; i < 4; ++i) {
            if ((HWND)lpdis->hwndItem == hwndChannelLight[i]) {
                HBRUSH brush;
                if (isChannelMuted[i])
                    brush = CreateSolidBrush(RGB(255, 0, 0)); // Red if muted
                else
                    brush = CreateSolidBrush(RGB(0, 255, 0)); // Green if active

                FillRect(lpdis->hDC, &lpdis->rcItem, brush);
                DeleteObject(brush);

                // Optional: draw a border
                FrameRect(lpdis->hDC, &lpdis->rcItem, (HBRUSH)GetStockObject(BLACK_BRUSH));
                return TRUE;
            }
        }
        break;
    }
		
    case WM_CREATE:
        // Create all GUI elements
        debug("Initializing GUI controls");

	// Important buttons
	hwndBrowse = CreateWindow("BUTTON", "Browse", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		topStartX, topButtonY, topButtonW, topButtonH, hwnd, (HMENU)ID_BTN_BROWSE, NULL, NULL);

	hwndStop = CreateWindow("BUTTON", "Stop", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		topStartX + (topButtonW + topButtonGap), topButtonY, topButtonW, topButtonH,
		hwnd, (HMENU)ID_BTN_STOP, NULL, NULL);

	hwndReset = CreateWindow("BUTTON", "Start / Reset", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		topStartX + 2 * (topButtonW + topButtonGap), topButtonY, topButtonW, topButtonH,
		hwnd, (HMENU)ID_BTN_RESET, NULL, NULL);
	
	// Playback control buttons	
	hwndBtnSeekBack = CreateWindow("BUTTON", "<<", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		playbackButtonStartX, playbackButtonY, playbackButtonW, playbackButtonH,
		hwnd, (HMENU)ID_BTN_SEEK_BACK, NULL, NULL);

	hwndBtnPause = CreateWindow("BUTTON", "| |", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		playbackButtonStartX + (playbackButtonW + playbackButtonSpacing), playbackButtonY,
		playbackButtonW, playbackButtonH,
		hwnd, (HMENU)ID_BTN_PAUSE, NULL, NULL);

	hwndBtnSeekForward = CreateWindow("BUTTON", ">>", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		playbackButtonStartX + 2 * (playbackButtonW + playbackButtonSpacing), playbackButtonY,
		playbackButtonW, playbackButtonH,
		hwnd, (HMENU)ID_BTN_SEEK_FORWARD, NULL, NULL);		

        // Disable until MOD is loaded
		updatePlaybackButtons(hwnd, false);
		//RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
		debug("Some buttons disabled on startup");

        // Settings button
		hwndToggleSettings = CreateWindow("BUTTON", ">> Show Settings", WS_CHILD | WS_VISIBLE,
			toggleBtnX, toggleBtnY, toggleBtnWidth, topButtonH, hwnd, (HMENU)ID_BTN_TOGGLE_SETTINGS, NULL, NULL);

		// Buffer Size
		hwndLabelBufSize = CreateWindow("STATIC", "Audio Buffer Size:", WS_CHILD,
			LABEL_X, y, LABEL_WIDTH, HEIGHT, hwnd, NULL, NULL, NULL);
		hwndBufSize = CreateWindow("EDIT", "1024", WS_CHILD | WS_BORDER | ES_NUMBER,
			EDIT_X, y, EDIT_WIDTH, HEIGHT, hwnd, (HMENU)ID_EDIT_BUFFER_SIZE, NULL, NULL);
		y += GAP;

		// Buffer Count
		hwndLabelBufCount = CreateWindow("STATIC", "Audio Buffer Count:", WS_CHILD,
			LABEL_X, y, LABEL_WIDTH, HEIGHT, hwnd, NULL, NULL, NULL);
		hwndBufCount = CreateWindow("EDIT", "4", WS_CHILD | WS_BORDER | ES_NUMBER,
			EDIT_X, y, EDIT_WIDTH, HEIGHT, hwnd, (HMENU)ID_EDIT_BUFFER_COUNT, NULL, NULL);
		y += GAP;

		// Sample Rate
		hwndLabelSampleRate = CreateWindow("STATIC", "Audio Sample Rate (Hz):", WS_CHILD,
			LABEL_X, y, LABEL_WIDTH, HEIGHT, hwnd, NULL, NULL, NULL);
		hwndSampleRate = CreateWindow("EDIT", "48000", WS_CHILD | WS_BORDER | ES_NUMBER,
			EDIT_X, y, EDIT_WIDTH, HEIGHT, hwnd, (HMENU)ID_EDIT_SAMPLE_RATE, NULL, NULL);
		y += GAP;

		// Channels
		/*hwndLabelChannels = CreateWindow("STATIC", "Audio Output Channels:", WS_CHILD,
			LABEL_X, y, LABEL_WIDTH, HEIGHT, hwnd, NULL, NULL, NULL);
		hwndChannels = CreateWindow("EDIT", "4", WS_CHILD | WS_BORDER | ES_NUMBER,
			EDIT_X, y, EDIT_WIDTH, HEIGHT, hwnd, (HMENU)ID_EDIT_CHANNELS, NULL, NULL);
		y += GAP;*/

		// Adjust Channels label (initially hidden)
		hwndLabelAdjustChannels = CreateWindow("STATIC", "Adjust Channels:", WS_CHILD,
			LABEL_X, y, 200, 20, hwnd, NULL, NULL, NULL);
		y += 30;


		for (int i = 0; i < 4; ++i) {
			int x = LABEL_X + i * 70;

			char label[4];
			sprintf(label, "%d", i + 1);

			hwndChannelLabel[i] = CreateWindow("STATIC", label, WS_CHILD | SS_CENTER,
				x + 10, y, 30, 20, hwnd, NULL, NULL, NULL);

			hwndChannelLight[i] = CreateWindow("STATIC", "", WS_CHILD | SS_OWNERDRAW,
				x + 10, y + 20, 30, 30, hwnd, NULL, NULL, NULL);

			hwndChannelMute[i] = CreateWindow("BUTTON", "Solo", WS_CHILD | BS_PUSHBUTTON,
				x, y + 55, 50, 25, hwnd, (HMENU)(uintptr_t)(ID_BTN_MUTE_CH1 + i), NULL, NULL);
		}
	
	hwndRestore = CreateWindow("BUTTON", "Restore Changes", WS_CHILD | BS_PUSHBUTTON,
		bottomStartX, bottomButtonY, bottomButtonW, bottomButtonH, hwnd, (HMENU)ID_BTN_RESTORE, NULL, NULL);

	hwndApply = CreateWindow("BUTTON", "Apply Settings", WS_CHILD | BS_PUSHBUTTON,
		bottomStartX + bottomButtonW + bottomGap, bottomButtonY, bottomButtonW, bottomButtonH,
		hwnd, (HMENU)ID_BTN_APPLY, NULL, NULL);

		// Seek slider
		CreateWindow(TRACKBAR_CLASS, "", WS_CHILD | WS_VISIBLE | TBS_HORZ,
			10, 360, 350, 20, hwnd, (HMENU)ID_SLIDER_SEEK, NULL, NULL);		
        debug("GUI controls created");
		SetFontRecursive(hwnd, hSegoeUIFont);
        break;

    case WM_COMMAND:
        // Handle button actions
        switch (LOWORD(wParam)) {
        case ID_BTN_BROWSE:
           browseAndLoad(hwnd);
           // Enable controls if load was successful
           if (isPlaying) {
				updatePlaybackButtons(hwnd, true);
           }
           break;
        
        case ID_BTN_RESET:
            debug("Reset button clicked");

            // Stop and reset the engine to clear any prior data
            engine.stop();
            engine.reset();

            // Check if a valid MOD file is loaded
            if (modFile.patterns.empty()) {
                debug("No MOD file loaded - cannot reset/start playback");
                strcpy(statusText, "No MOD file loaded.");
                isPlaying = false;
				updatePlaybackButtons(hwnd, false);
            } else {
                // Fully reload the engine with the existing MOD file
                if (engine.load(&modFile)) {
                    debug("MOD file loaded into engine: Starting playback");
                    engine.start();  // <-- FIXED: removed if-condition

                    isPlaying = true;
                    strcpy(statusText, "Audio Playing...");

					updatePlaybackButtons(hwnd, true);
					debug("Some buttons enabled after engine start");
                } else {
                    debug("Engine failed to load MOD file.");
                    strcpy(statusText, "Failed to load MOD.");
                    isPlaying = false;
					updatePlaybackButtons(hwnd, false);
                }
            }
            break;

		case WM_HSCROLL: {
			HWND hSlider = GetDlgItem(hwnd, ID_SLIDER_SEEK);
			if ((HWND)lParam == hSlider) {
				int scrollCode = LOWORD(wParam);

				// Seek only when dragging or finished dragging
				if (scrollCode == TB_THUMBTRACK || scrollCode == SB_ENDSCROLL || scrollCode == SB_THUMBPOSITION) {
					int pos = (int)SendMessage(hSlider, TBM_GETPOS, 0, 0);
					engine.seekToRow(pos, engine.currentPattern);
					debugf("Slider seeked to row %d", pos);
				}
				return 0;
			}
			break;
		}
	
		case ID_BTN_MUTE_CH1:
		case ID_BTN_MUTE_CH2:
		case ID_BTN_MUTE_CH3:
		case ID_BTN_MUTE_CH4:
		{
			int channel = LOWORD(wParam) - ID_BTN_MUTE_CH1;
			isChannelMuted[channel] = !isChannelMuted[channel];

			// Update button text
			SetWindowText(hwndChannelMute[channel], isChannelMuted[channel] ? "Mute" : "Solo");

			// Inform the engine (if it supports muting)
			engine.MuteChannel(channel, isChannelMuted[channel]);
			// Debug print based on state
			if (isChannelMuted[channel]) {
				debugf("Muted Channel: %d", channel + 1);
			} else {
				debugf("Solo Channel: %d", channel + 1);
			}
			break;
		}

		case ID_BTN_TOGGLE_SETTINGS:
			settingsVisible = !settingsVisible;

			ShowWindow(hwndLabelBufSize, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndBufSize, settingsVisible ? SW_SHOW : SW_HIDE);        
			ShowWindow(hwndLabelBufCount, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndBufCount, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndLabelSampleRate, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndSampleRate, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndLabelChannels, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndChannels, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndRestore, settingsVisible ? SW_SHOW : SW_HIDE);
			ShowWindow(hwndApply, settingsVisible ? SW_SHOW : SW_HIDE);

			// NEW: toggle "Adjust Channels" visibility
			ShowWindow(hwndLabelAdjustChannels, settingsVisible ? SW_SHOW : SW_HIDE);
			for (int i = 0; i < 4; ++i) {
				ShowWindow(hwndChannelLabel[i], settingsVisible ? SW_SHOW : SW_HIDE);
				ShowWindow(hwndChannelLight[i], settingsVisible ? SW_SHOW : SW_HIDE);
				ShowWindow(hwndChannelMute[i], settingsVisible ? SW_SHOW : SW_HIDE);
			}

			SetWindowText(hwndToggleSettings, settingsVisible ? "<< Hide Settings" : ">> Show Settings");
			break;

        case ID_BTN_RESTORE:
            debug("Restore button clicked");
            SetWindowTextA(hwndBufSize, "1024");
            SetWindowTextA(hwndBufCount, "4");
            SetWindowTextA(hwndSampleRate, "48000");
            SetWindowTextA(hwndChannels, "4");
            break;
        
		case ID_BTN_APPLY: applyAudioSettings(); break;
        
		case ID_BTN_STOP:
            debug("Stop button clicked");
            engine.stop();
            isPlaying = false;
            strcpy(statusText, "Audio Stopped!");
			updatePlaybackButtons(hwnd, false);
            break;
        
		case ID_BTN_PLAY:
            debug("Play button clicked");
            if (!isPlaying && !modFile.patterns.empty()) {
                engine.load(&modFile);
                engine.start();
                isPlaying = true;
                strcpy(statusText, "Audio Playing...");
            }
            break;

        case ID_BTN_PAUSE:
            debug("Pause button clicked");

            if (playbackButtonState == STATE_PLAYING) {
                engine.pause();  // Pause
                isPlaying = false;
                playbackButtonState = STATE_PAUSED;

                SetWindowText(GetDlgItem(hwnd, ID_BTN_PAUSE), ">");  // Change to play symbol
                strcpy(statusText, "Audio Paused.");
            } else {
                engine.resume();  // Resume
                isPlaying = true;
                playbackButtonState = STATE_PLAYING;

                SetWindowText(GetDlgItem(hwnd, ID_BTN_PAUSE), "| |");  // Change back to pause symbol
                strcpy(statusText, "Playing...");
            }
            break;

        case ID_BTN_SEEK_BACK:
            debug("Seek back clicked");
            engine.seekBackward();
            break;

        case ID_BTN_SEEK_FORWARD:
            debug("Seek forward clicked");
            engine.seekForward();
            break;			
        }
        break;

	case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        renderPattern(hwnd, engine.currentRow, engine.currentPattern);
        break;

    case WM_TIMER:
        InvalidateRect(hwnd, NULL, TRUE);
		SendMessage(GetDlgItem(hwnd, ID_SLIDER_SEEK), TBM_SETPOS, TRUE, engine.currentRow);
		updatePlaybackSlider(hwnd);		
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
	ensureConsole();
	INITCOMMONCONTROLSEX icex;
	icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icex.dwICC = ICC_BAR_CLASSES;  // Enables trackbars, progress bars, etc.
	InitCommonControlsEx(&icex);

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
    
	// Create a Segoe UI font (used globally)
	hSegoeUIFont = CreateFontW(
		-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Emoji"
	);

	
    // Create main window
    HWND hwnd = CreateWindowEx(0, "MODWindow", TITLE_WINDOW, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, WINDOW_WIDTH, WINDOW_HEIGHT, NULL, NULL, hInstance, NULL);

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
    
	// Clean up font
    if (hSegoeUIFont) {
        DeleteObject(hSegoeUIFont);
    }	
    return 0;
}
