/* 
  ===============================================
    File : mod_loader.h
    Author : svh03ra
    Created : 13-Jul-2025 (‏‎‏‎06:41:31 PM)
	// Program /* Beta 1 /*
  ===============================================
             This is an beta version.
         You might experience instability!

              Use at your own risk.

          (C) 2025, All rights reserved
  ===============================================
*/

#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <array>

// Represents one sample (instrument data) in a MOD file
struct MODSample {
    std::string name;         // Sample name (22 bytes in MOD)
    uint16_t length = 0;      // Sample length in bytes (stored as words × 2)
    int8_t finetune = 0;      // Finetune: -8 to +7 (from 4-bit unsigned nibble)
    uint8_t volume = 64;      // Volume (0–64)
    uint16_t loopStart = 0;   // Loop start position (bytes)
    uint16_t loopLength = 0;  // Loop length (bytes)
    std::vector<int8_t> data; // Sample audio data (8-bit signed PCM)
};

// Represents a single note entry in the pattern grid
struct MODNote {
    uint16_t period = 0;        // Pitch/frequency information (12-bit)
    uint8_t sampleNumber = 0;   // Sample number (5–8 bits across 2 packed bytes)
    uint8_t effectType = 0;     // Effect command type (e.g., vibrato, arpeggio)
    uint8_t effectParam = 0;    // Effect parameter (depends on effect)
};

// Represents one pattern: 64 rows × 4 channels
struct MODPattern {
    std::array<std::array<MODNote, 4>, 64> notes; // [row][channel]
};

// Complete parsed MOD file representation
struct MODFile {
    std::string title;                      // Song title (20 bytes)
    std::vector<MODSample> samples;         // 31 samples (ProTracker standard)
    std::vector<MODPattern> patterns;       // All pattern data
    std::vector<uint8_t> patternOrder;      // Sequence of pattern indices (128 bytes)
    uint8_t songLength = 0;                 // Song length (number of positions in patternOrder)
};

// Loads a MOD file from disk and returns a parsed MODFile object
MODFile loadMODFile(const std::string& filename);
