/* 
  ===============================================
    File : mod_player_engine.h
    Author : svh03ra
    Created : 13-Jul-2025 (‏‎‏‎11:46:24 PM)
	// Program /* Beta 1 /*
  ===============================================
             This is an beta version.
         You might experience instability!

              Use at your own risk.

          (C) 2025, All rights reserved
  ===============================================
*/

#pragma once

#include "mod_loader.h"
#include <windows.h>
#include <cstdint>
#include <vector>
#include <atomic>
#include <algorithm>  // for std::clamp

// Default sample rate and max MOD channels
#define SAMPLE_RATE    48000
#define MAX_CHANNELS   4  // MOD files typically support 4 channels (Amiga / ProTracker)

// State of a single audio channel
struct ChannelState {
    int sampleIndex = -1;           // Index of current sample
    const int8_t* sampleData = nullptr; // Pointer to sample PCM data
    int sampleLength = 0;           // Total length of the sample
    int loopStart = 0;              // Loop start position
    int loopLength = 0;             // Loop length
    float volume = 1.0f;            // Current volume (0.0 to 1.0)
    float freq = 8363.0f;           // Playback frequency in Hz
    float samplePos = 0.0f;         // Floating point sample position
    bool playing = false;           // Is sample currently active?

    // Effects (set by row, updated per tick)
    uint8_t effectCmd = 0;          // Effect type (e.g., 0x0 for arpeggio)
    uint8_t effectParam = 0;        // Effect parameter (8-bit)
    float targetPeriod = 0.0f;      // Target period for portamento
    float currentPeriod = 0.0f;     // Current period value
    float vibratoPhase = 0.0f;      // Phase accumulator for vibrato LFO
	int finetune = 0;
};

// Main MOD playback engine
class ModEngine {
public:
    ModEngine();

    // Load MOD file and control playback
    bool load(MODFile* file);       // Load parsed MOD structure
    void start();                   // Begin playback
    void reset();                   // comment placeholder
    void stop();                    // Stop playback and release audio
    void pause();                   // Pause playback
    void resume();                  // Resume playback
    bool isPausedState() const;     // Return paused status
    bool seekToRow(int row, int pattern); // Jump to position
	void seekBackward();  			// Jump one row back
	void seekForward();  	    // Jump one row forward
    void tick();                    // Process one MOD tick
    void onBufferDone();            // Called when audio buffer finishes
    void mixAudio(int16_t* buffer, int samples); // Mix samples into output buffer
	void MuteChannel(int channel, bool mute);

    // Runtime audio configuration
    void setBufferSize(int size);   // Set samples per buffer (64 - 8192)
    void setBufferCount(int count); // Set number of audio buffers
    void setSampleRate(int rate);   // Set sample rate (Hz)
    void setOutputChannels(int ch); // Set mono (1) or stereo (2)

    // Get current audio configuration
    int getBufferSize()     const { return bufferSamples; }
    int getBufferCount()    const { return bufferCount; }
    int getSampleRate()     const { return sampleRate; }
    int getOutputChannels() const; // Implemented in .cpp
    int getCurrentBuffer()  const { return currentBuffer; }
    int getChannels()       const; // Return number of MOD channels (4)

    // Playback position (exposed for GUI/status display)
    int currentRow     = 0;         // Current row in pattern (0–63)
    int currentPattern = 0;         // Current pattern ID
    int speed          = 6;         // Speed: ticks per row
    int tempo          = 125;       // BPM tempo

    // Array of channel states
    ChannelState channels[MAX_CHANNELS];

    // Solo toggle array
    void setChannelSolo(int ch, bool state);    // Toggle solo state per channel
	
	bool isChannelMuted(int ch) const {
        return ch >= 0 && ch < MAX_CHANNELS ? channelMuteState[ch] : true;
    }

    bool isChannelSoloed(int ch) const {
        return ch >= 0 && ch < MAX_CHANNELS ? soloChannel[ch] : false;
    }

    bool anyChannelSoloed() const {
        for (int i = 0; i < MAX_CHANNELS; ++i)
            if (soloChannel[i]) return true;
        return false;
    }

private:
    // MOD file data (loaded externally)
    MODFile* mod         = nullptr;

    // Playback state
    int tickCounter      = 0;       // Current tick in row
    int patternOrderPos  = 0;       // Current position in pattern order table
    int bufferSamples    = 1024;    // Samples per buffer
    int bufferCount      = 2;       // Number of active buffers
    int sampleRate       = SAMPLE_RATE;
    int outputChannels   = 2;       // Mono = 1, Stereo = 2
    int currentBuffer    = 0;       // Index of active buffer

    // Windows audio
    HWAVEOUT hWaveOut    = nullptr; // WaveOut handle
    std::vector<int16_t> mixBuffer; // Output buffer (interleaved)
    std::vector<WAVEHDR> headers;   // WaveOut headers for buffers
    std::atomic<bool> isPlaying = false;
    std::atomic<bool> isPaused = false;

    // Tick timing control
    double tickSamples        = 0.0; // Samples per tick (recomputed with tempo)
    double tickSampleCounter  = 0.0; // Accumulator for tick advancement

    // Internal engine logic
    void setupWaveOut();            // Initialize WaveOut and buffers
    void resizeMixBuffer();         // Resize mix buffer when settings change
    void submitBuffer(int index);   // Send buffer to WaveOut
    float periodToFreq(uint16_t period, int finetune); // Convert MOD period to Hz
    void processTickEffects();      // Apply per-tick effects
    void handleRow();               // Decode note data on new row

    // Mute and solo state
    bool channelMuteState[MAX_CHANNELS] = { false };
    bool soloChannel[MAX_CHANNELS] = { false };
};
