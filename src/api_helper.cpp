/* 
  ===============================================
    File : api_helper.cpp
    Author : svh03ra
    Created : 13-Jul-2025 (‏‎‏‎11:47:56 PM)
	// Program /* Beta 1 /*
  ===============================================
             This is an beta version.
         You might experience instability!

              Use at your own risk.

          (C) 2025, All rights reserved
  ===============================================
*/

#include <windows.h>
#include "api_helper.h"
#include <cstdio>
#include <cstdarg>

static bool consoleInitialized = false;

// Allocates a console and redirects stdout/stderr for debug output
void ensureConsole() {
    if (!consoleInitialized) {
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        SetConsoleTitleA("MOD Player Debug Window");
        consoleInitialized = true;
    }
}

// Logs plain message
void debug(const char* msg) {
	ensureConsole();
    char buffer[512];
    sprintf(buffer, "[DEBUG]: %s\n", msg);
    OutputDebugStringA(buffer);  // Sends to debugger (e.g., Output pane in IDE)
    printf("%s", buffer);        // Sends to stdout (your debug console)
    fflush(stdout);              // Ensures it's flushed immediately
}

// Logs formatted message (like printf)
void debugf(const char* fmt, ...) {
	ensureConsole();
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    char full[576];
    snprintf(full, sizeof(full), "[DEBUG]: %s\n", buffer);
    OutputDebugStringA(full);
    printf("%s", full);
    fflush(stdout);
}
