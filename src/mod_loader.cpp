/* 
  ===============================================
    File : mod_loader.cpp
    Author : svh03ra
    Created : 6-Jul-2025 (‏‎10:51:38 PM)
	// Program /* Alpha 1 /*
  ===============================================
            This is an alpha version.
      It may be unstable as it still contains
       several limitations and some issues!

              Use at your own risk.

          (C) 2025, All rights reserved
  ===============================================
*/

#include "mod_loader.h"
#include <fstream>
#include <cstring>
#include <stdexcept>

// Read a big-endian 16-bit value from byte array
static uint16_t readBE16(const uint8_t* data) {
    return (data[0] << 8) | data[1];
}

// Load a MOD file into a structured MODFile object
MODFile loadMODFile(const std::string& filename) {
    MODFile mod;

    // Open file in binary mode
    std::ifstream file(filename, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open MOD file");

    // Read 1084-byte header (includes title, samples, pattern order, etc.)
    uint8_t header[1084];
    file.read((char*)header, 1084);

    // Extract module title (first 20 bytes)
    mod.title = std::string((char*)header, 20);

    // Read sample metadata (31 samples, 30 bytes each)
    for (int i = 0; i < 31; ++i) {
        MODSample sample;
        sample.name       = std::string((char*)&header[20 + i * 30], 22);
        sample.length     = readBE16(&header[20 + i * 30 + 22]) * 2;   // Stored in words
        sample.volume     = header[20 + i * 30 + 25];
        sample.loopStart  = readBE16(&header[20 + i * 30 + 26]) * 2;
        sample.loopLength = readBE16(&header[20 + i * 30 + 28]) * 2;
        mod.samples.push_back(sample);
    }

    // Song length (1 byte at offset 950)
    mod.songLength = header[950];

    // Pattern order table (128 bytes starting at offset 952)
    mod.patternOrder.assign(&header[952], &header[952 + 128]);

    // Determine number of patterns based on the highest pattern index used
    int numPatterns = 0;
    for (int i = 0; i < 128; ++i)
        if (mod.patternOrder[i] > numPatterns)
            numPatterns = mod.patternOrder[i];
    numPatterns++;  // Include that pattern index itself

    // Read pattern data (each pattern has 64 rows × 4 channels × 4 bytes per note)
    for (int p = 0; p < numPatterns; ++p) {
        MODPattern pattern;
        for (int row = 0; row < 64; ++row) {
            for (int ch = 0; ch < 4; ++ch) {
                uint8_t note[4];
                file.read((char*)note, 4);

                MODNote n;
                n.period       = ((note[0] & 0x0F) << 8) | note[1];     // 12-bit period
                n.sampleNumber = (note[0] & 0xF0) | (note[2] >> 4);    // Sample number (split across 2 bytes)
                n.effectType   = note[2] & 0x0F;                      // Effect type (lower 4 bits)
                n.effectParam  = note[3];                            // Effect parameter
                pattern.notes[row][ch] = n;
            }
        }
        mod.patterns.push_back(pattern);
    }

    // Read sample audio data (8-bit signed PCM)
    for (int i = 0; i < 31; ++i) {
        auto& sample = mod.samples[i];
        sample.data.resize(sample.length);  // Allocate buffer
        file.read((char*)sample.data.data(), sample.length);
    }

    return mod;
}
