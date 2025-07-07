/* 
  ===============================================
    File : mod_player_engine.cpp
    Author : svh03ra
    Created : 6-Jul-2025 (10:46:44 PM)
	// Program /* Alpha 1 /*
  ===============================================
            This is an alpha version.
      It may be unstable as it still contains
       several limitations and some issues!

              Use at your own risk.

          (C) 2025, All rights reserved
  ===============================================
*/

// Standard includes
#include "mod_player_engine.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <algorithm>

// Default audio settings
#define DEFAULT_BUFFER_SAMPLES 1024
#define DEFAULT_BUFFER_COUNT 4
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_OUTPUT_CHANNELS 2

// Callback function for waveOut — called when a buffer finishes
static void CALLBACK waveCallback(HWAVEOUT, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR, DWORD_PTR) {
    if (uMsg == WOM_DONE) {
        ModEngine* engine = reinterpret_cast<ModEngine*>(dwInstance);
        engine->onBufferDone();  // Notify engine buffer is ready for reuse
    }
}

// Constructor: initialize defaults and allocate buffer
ModEngine::ModEngine() {
    bufferSamples = DEFAULT_BUFFER_SAMPLES;
    bufferCount = DEFAULT_BUFFER_COUNT;
    sampleRate = DEFAULT_SAMPLE_RATE;
    outputChannels = DEFAULT_OUTPUT_CHANNELS;
    resizeMixBuffer();  // Allocate internal mix buffer
}

// Resize mix buffer based on current settings
void ModEngine::resizeMixBuffer() {
    mixBuffer.resize(bufferSamples * outputChannels * bufferCount);
}

// Load MOD file and initialize pattern/tempo state
bool ModEngine::load(MODFile* file) {
    mod = file;
    patternOrderPos = 0;
    currentRow = 0;
    currentPattern = mod->patternOrder[patternOrderPos];
    speed = 6;
    tempo = 125;
    tickCounter = 0;
    tickSampleCounter = 0.0f;
    tickSamples = (sampleRate * 2.5f) / tempo;  // Ticks based on tempo
    return true;
}

// Start playback: initialize buffers and submit them
void ModEngine::start() {
    if (!mod) return;
    setupWaveOut();  // Open wave output device
    isPlaying = true;
    currentBuffer = 0;
    for (int i = 0; i < bufferCount; ++i)
        submitBuffer(i);  // Pre-fill all buffers
}

// Stop playback and release audio device
void ModEngine::stop() {
    isPlaying = false;
    if (hWaveOut) {
        waveOutReset(hWaveOut);  // Stop playback
        waveOutClose(hWaveOut);  // Close device
        hWaveOut = nullptr;
    }
}

// Initialize Windows waveOut API and prepare audio buffers
void ModEngine::setupWaveOut() {
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = outputChannels;
    fmt.nSamplesPerSec = sampleRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = outputChannels * sizeof(int16_t);
    fmt.nAvgBytesPerSec = sampleRate * fmt.nBlockAlign;

    waveOutOpen(&hWaveOut, WAVE_MAPPER, &fmt, (DWORD_PTR)waveCallback, (DWORD_PTR)this, CALLBACK_FUNCTION);

    headers.resize(bufferCount);
    for (int i = 0; i < bufferCount; ++i) {
        WAVEHDR& hdr = headers[i];
        std::memset(&hdr, 0, sizeof(WAVEHDR));
        hdr.lpData = (LPSTR)&mixBuffer[i * bufferSamples * outputChannels];
        hdr.dwBufferLength = bufferSamples * outputChannels * sizeof(int16_t);
        waveOutPrepareHeader(hWaveOut, &hdr, sizeof(WAVEHDR));
    }
}

// Mix audio for a buffer and queue it to waveOut
void ModEngine::submitBuffer(int index) {
    if (!isPlaying) return;
    mixAudio((int16_t*)&mixBuffer[index * bufferSamples * outputChannels], bufferSamples);
    waveOutWrite(hWaveOut, &headers[index], sizeof(WAVEHDR));
}

// Called by waveCallback when a buffer completes
void ModEngine::onBufferDone() {
    submitBuffer(currentBuffer);  // Refill buffer
    currentBuffer = (currentBuffer + 1) % bufferCount;
}

// Convert MOD period value to actual frequency
float ModEngine::periodToFreq(uint16_t period) {
    if (period == 0) return 0;
    return 7093789.2f / period;
}

// Advance tick state; handles row changes and per-tick effects
void ModEngine::tick() {
    if (tickCounter == 0)
        handleRow();  // New row: decode notes
    else
        processTickEffects();  // Mid-row: effects like vibrato

    tickCounter++;
    if (tickCounter >= speed) {
        tickCounter = 0;
        currentRow++;
        if (currentRow >= 64) {
            currentRow = 0;
            patternOrderPos = (patternOrderPos + 1) % mod->songLength;
            currentPattern = mod->patternOrder[patternOrderPos];
        }
    }
}

// Decode and apply note/sample data for current row
void ModEngine::handleRow() {
    MODPattern& pat = mod->patterns[currentPattern];
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        MODNote& note = pat.notes[currentRow][ch];
        if (note.period > 0) {
            int sampleId = note.sampleNumber;
            if (sampleId >= 0 && sampleId < mod->samples.size()) {
                MODSample& s = mod->samples[sampleId];
                ChannelState& c = channels[ch];
                c.sampleData = s.data.data();
                c.sampleLength = s.length;
                c.loopStart = s.loopStart;
                c.loopLength = s.loopLength;
                c.sampleIndex = sampleId;
                c.samplePos = 0.0f;
                c.playing = true;
                c.volume = s.volume / 64.0f;
                c.freq = periodToFreq(note.period);
                c.currentPeriod = note.period;
                c.targetPeriod = note.period;
            }
        }

        // Store effects for processing on later ticks
        channels[ch].effectCmd = note.effectType;
        channels[ch].effectParam = note.effectParam;

        // Handle tempo/speed change (0x0F)
        if (note.effectType == 0x0F) {
            if (note.effectParam <= 0x1F && note.effectParam > 0) {
                speed = note.effectParam;
            } else if (note.effectParam >= 0x20) {
                tempo = note.effectParam;
                tickSamples = (sampleRate * 2.5f) / tempo;
            }
        }
    }
}

// Apply effects (arpeggio, portamento, vibrato, volume) on current tick
void ModEngine::processTickEffects() {
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        ChannelState& c = channels[ch];
        switch (c.effectCmd) {
            case 0x0: {  // Arpeggio
                int n = tickCounter % 3;
                int semitone = 0;
                if (n == 1) semitone = (c.effectParam >> 4);
                else if (n == 2) semitone = (c.effectParam & 0x0F);
                float newPeriod = c.currentPeriod * std::pow(2.0f, -semitone / 12.0f);
                c.freq = periodToFreq((uint16_t)newPeriod);
                break;
            }
            case 0x3: {  // Portamento
                int slide = c.effectParam;
                if (c.currentPeriod > c.targetPeriod)
                    c.currentPeriod -= slide;
                else if (c.currentPeriod < c.targetPeriod)
                    c.currentPeriod += slide;
                c.freq = periodToFreq((uint16_t)c.currentPeriod);
                break;
            }
            case 0x4: {  // Vibrato
                float depth = (c.effectParam & 0x0F) * 2;
                float rate = (c.effectParam >> 4) * 0.2f;
                float vib = std::sin(c.vibratoPhase) * depth;
                c.freq = periodToFreq((uint16_t)(c.currentPeriod + vib));
                c.vibratoPhase += rate;
                break;
            }
            case 0xC: {  // Volume set
                c.volume = (c.effectParam & 0x3F) / 64.0f;
                break;
            }
        }
    }
}

// Audio mixing function — fills buffer with mixed output of all channels
void ModEngine::mixAudio(int16_t* buffer, int samples) {
    std::memset(buffer, 0, samples * outputChannels * sizeof(int16_t));
    for (int i = 0; i < samples; ++i) {
        tickSampleCounter += 1.0f;
        if (tickSampleCounter >= tickSamples) {
            tickSampleCounter -= tickSamples;
            tick();  // Advance tick (and possibly row)
        }

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            ChannelState& c = channels[ch];
            if (!c.playing || !c.sampleData) continue;

            float effectiveFreq = c.freq;

            // Inline re-evaluation of arpeggio/vibrato during mixing
            switch (c.effectCmd) {
                case 0x0: {
                    int n = tickCounter % 3;
                    int semitone = (n == 1) ? (c.effectParam >> 4) :
                                   (n == 2) ? (c.effectParam & 0x0F) : 0;
                    float newPeriod = c.currentPeriod * std::pow(2.0f, -semitone / 12.0f);
                    effectiveFreq = periodToFreq((uint16_t)newPeriod);
                    break;
                }
                case 0x4: {
                    float depth = (c.effectParam & 0x0F) * 2;
                    float rate = (c.effectParam >> 4) * 0.2f;
                    float vib = std::sin(c.vibratoPhase) * depth;
                    effectiveFreq = periodToFreq((uint16_t)(c.currentPeriod + vib));
                    c.vibratoPhase += rate;
                    break;
                }
            }

            int idx = static_cast<int>(c.samplePos);
            if (idx >= c.sampleLength) {
                if (c.loopLength > 2) {
                    c.samplePos = static_cast<float>(c.loopStart);
                    idx = c.loopStart;
                } else {
                    c.playing = false;
                    continue;
                }
            }

            int8_t s = c.sampleData[idx];  // 8-bit PCM sample
            int16_t val = static_cast<int16_t>(s * c.volume * 512);  // Convert to 16-bit PCM

            // Mix into output buffer (mono or stereo)
            for (int chn = 0; chn < outputChannels; ++chn) {
                int mixIdx = i * outputChannels + chn;
                int mixed = buffer[mixIdx] + val;
                buffer[mixIdx] = std::min(std::max(mixed, -32768), 32767);  // Clipping
            }

            // Advance sample position
            c.samplePos += effectiveFreq / sampleRate;
        }
    }
}

// Accessor functions and configuration setters
int ModEngine::getChannels() const {
    return MAX_CHANNELS;
}

int ModEngine::getOutputChannels() const {
    return outputChannels;
}

void ModEngine::setOutputChannels(int ch) {
    outputChannels = std::clamp(ch, 1, 2);
    resizeMixBuffer();
}

void ModEngine::setBufferSize(int size) {
    bufferSamples = std::clamp(size, 64, 8192);
    resizeMixBuffer();
}

void ModEngine::setBufferCount(int count) {
    bufferCount = std::clamp(count, 1, 8);
    resizeMixBuffer();
}

void ModEngine::setSampleRate(int rate) {
    sampleRate = std::clamp(rate, 8000, 96000);
    tickSamples = (sampleRate * 2.5f) / tempo;
}