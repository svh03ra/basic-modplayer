/* 
  ===============================================
    File : mod_player_engine.cpp
    Author : svh03ra
    Created : 13-Jul-2025 (‏‎11:46:16 PM)
	// Program /* Beta 1 /*
  ===============================================
             This is an beta version.
         You might experience instability!

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
#include <cstdio> // For debug()

#include "api_helper.h"

// Debug helper
#define debug(fmt, ...) printf("[DEBUG]: MODEngine: " fmt "\n", ##__VA_ARGS__)

// Default audio settings
#define DEFAULT_BUFFER_SAMPLES 1024
#define DEFAULT_BUFFER_COUNT 4
#define DEFAULT_SAMPLE_RATE 48000
#define DEFAULT_OUTPUT_CHANNELS 2

// Callback function for waveOut - called when a buffer finishes
static void CALLBACK waveCallback(HWAVEOUT, UINT uMsg, DWORD_PTR dwInstance, DWORD_PTR, DWORD_PTR) {
    if (uMsg == WOM_DONE) {
        ModEngine* engine = reinterpret_cast<ModEngine*>(dwInstance);
        engine->onBufferDone();  // Notify engine buffer is ready for reuse
    }
}

// Constructor: initialize defaults and allocate buffer
ModEngine::ModEngine() {
	ensureConsole();
    debug("Constructor");
    bufferSamples = DEFAULT_BUFFER_SAMPLES;
    bufferCount = DEFAULT_BUFFER_COUNT;
    sampleRate = DEFAULT_SAMPLE_RATE;
    outputChannels = DEFAULT_OUTPUT_CHANNELS;
    isPaused = false;
    resizeMixBuffer();  // Allocate internal mix buffer

    // Default init per channel
    for (int i = 0; i < MAX_CHANNELS; ++i)
        channels[i] = ChannelState();

    // Init solo/mute state
    std::fill(std::begin(soloChannel), std::end(soloChannel), false);
    std::fill(std::begin(channelMuteState), std::end(channelMuteState), false);
}

// Resize mix buffer based on current settings
void ModEngine::resizeMixBuffer() {
    debug("Resizing mix buffer");
    mixBuffer.resize(bufferSamples * outputChannels * bufferCount);
}

// Load MOD file and initialize pattern/tempo state
bool ModEngine::load(MODFile* file) {
    debug("Loading MOD file");
    if (!file || file->patternOrder.empty() || file->patterns.empty()) return false;
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
    debug("Starting playback");
    if (!mod) return;

    // Reset channel states cleanly
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        channels[ch] = ChannelState();  // Resets volume, freq, samplePos, etc.
    }

    resizeMixBuffer();  // Reallocate based on current bufferCount/sampleRate
    setupWaveOut();     // Fill new headers + WAVEHDRs

    isPlaying = true;
    isPaused = false;
    currentBuffer = 0;

    for (int i = 0; i < bufferCount; ++i)
        submitBuffer(i);
}

// Pause playback
void ModEngine::pause() {
    debug("Pausing playback");
    isPaused = true;
}

// Resume playback
void ModEngine::resume() {
    if (isPaused) {
        isPaused = false;
        isPlaying = true;

        debug("Resuming playback");

        // Re-submit all buffers so audio resumes
        for (int i = 0; i < bufferCount; ++i) {
			debug("Submitting buffer: %d", i);
            submitBuffer(i);
        }
    }
}


bool ModEngine::isPausedState() const {
    return isPaused;
}

bool ModEngine::seekToRow(int row, int pattern) {
    debug("Seek to pattern %d, row %d", pattern, row);
    if (!mod) return false;

    if (pattern >= 0 && pattern < mod->patterns.size() && row >= 0 && row < 64) {
        currentPattern = pattern;
        currentRow = row;
        tickCounter = 0;
        tickSampleCounter = 0.0f;

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            channels[ch].effectCmd = 0;
            channels[ch].effectParam = 0;
            channels[ch].vibratoPhase = 0;
        }

        return true;
    }

    return false;
}

void ModEngine::seekBackward() {
    if (!mod) return;

    int row = currentRow - 10;
    int pat = currentPattern;
    int pos = patternOrderPos;

    if (row > 0) {
        row--;
    } else if (pos > 0) {
        pos--;
        pat = mod->patternOrder[pos];
        row = 63;
    }

    seekToRow(row, pat);
    patternOrderPos = pos;
}

void ModEngine::seekForward() {
    if (!mod) return;

    int row = currentRow + 10;
    int pos = patternOrderPos;
    int pat = currentPattern;

    if (row >= 64) {
        row = 0;
        pos = (pos + 1) % mod->songLength;
        pat = mod->patternOrder[pos];
    }

    seekToRow(row, pat);
    patternOrderPos = pos;
}

void ModEngine::reset() {
    stop();

    for (WAVEHDR& hdr : headers) {
        hdr.lpData = nullptr;
    }
    headers.clear();
    mixBuffer.clear();

    isPlaying = false;
    isPaused = false;
    mod = nullptr;
    currentBuffer = 0;
    tickCounter = 0;
    tickSampleCounter = 0.0;
}

void ModEngine::stop() {
    isPlaying = false;
    isPaused = false;
    if (hWaveOut) {
        waveOutReset(hWaveOut);
        waveOutClose(hWaveOut);
        hWaveOut = nullptr;
    }
}

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

void ModEngine::submitBuffer(int index) {
    if (!isPlaying || isPaused) return;
    mixAudio((int16_t*)&mixBuffer[index * bufferSamples * outputChannels], bufferSamples);
    waveOutWrite(hWaveOut, &headers[index], sizeof(WAVEHDR));
}

void ModEngine::onBufferDone() {
    submitBuffer(currentBuffer);
    currentBuffer = (currentBuffer + 1) % bufferCount;
}

float ModEngine::periodToFreq(uint16_t period, int finetune) {
    if (period == 0) return 0.0f;

    static const float periodTable[16] = {
        8363.0f, 8413.0f, 8463.0f, 8529.0f,
        8581.0f, 8651.0f, 8723.0f, 8757.0f,
        7895.0f, 7941.0f, 7985.0f, 8046.0f,
        8107.0f, 8169.0f, 8232.0f, 8280.0f
    };

    float baseFreq = periodTable[finetune & 0x0F];
    return (baseFreq * 428.0f) / period;
}

void ModEngine::MuteChannel(int channel, bool mute) {
    if (channel >= 0 && channel < MAX_CHANNELS) {
        channelMuteState[channel] = mute;
    }
}

// New function: set solo state of a channel
void ModEngine::setChannelSolo(int ch, bool state) {
    if (ch >= 0 && ch < MAX_CHANNELS) {
        soloChannel[ch] = state;
    }
}

void ModEngine::tick() {
    if (tickCounter == 0)
        handleRow();
    else
        processTickEffects();

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

void ModEngine::handleRow() {
    MODPattern& pat = mod->patterns[currentPattern];
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        MODNote& note = pat.notes[currentRow][ch];
        ChannelState& c = channels[ch];

        // Store effect for tick processing
        c.effectCmd = note.effectType;
        c.effectParam = note.effectParam;

        bool hasNewNote = note.period > 0;
        bool hasNewSample = note.sampleNumber > 0;

        // Get sample index (if provided)
        int sampleId = note.sampleNumber - 1;  // NOTE: Always subtract 1 to prevent playback failure from invalid sample indices
        if (sampleId >= 0 && sampleId < mod->samples.size()) {
            if (hasNewSample) {
                c.sampleIndex = sampleId;
                MODSample& s = mod->samples[sampleId];
                c.sampleData = s.data.data();
                c.sampleLength = s.length;
                c.loopStart = s.loopStart;
                c.loopLength = s.loopLength;
                c.volume = s.volume / 64.0f;
                c.finetune = s.finetune;
            }
        }

        // If there's a new note
        if (hasNewNote) {
            // Tone portamento (3xx) means don't retrigger sample
            if (note.effectType == 0x03) {
                c.targetPeriod = note.period;
                // Don't reset samplePos or restart
            } else {
                // Normal note trigger
                c.samplePos = 0.0f;
                c.playing = true;
                c.currentPeriod = note.period;
                c.targetPeriod = note.period;
                c.freq = periodToFreq(note.period, c.finetune);  // With finetune
            }
        }

        // Handle Fxx: Speed / Tempo
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

// TODO: Reference for info - https://wiki.openmpt.org/Manual:_Effect_Reference#Effect_Column
void ModEngine::processTickEffects() {
    for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
        ChannelState& c = channels[ch];
        switch (c.effectCmd) {
            case 0x0: { // 0xy: Arpeggio
                int n = tickCounter % 3;
                int semitone = 0;
                if (n == 1) semitone = (c.effectParam >> 4);
                else if (n == 2) semitone = (c.effectParam & 0x0F);
                float newPeriod = c.currentPeriod * std::pow(2.0f, -semitone / 12.0f);
                c.freq = periodToFreq((uint16_t)newPeriod, c.finetune);
                break;
            }
            case 0x1: { // 1xx: Portamento Up
                c.currentPeriod = std::max(113.0f, c.currentPeriod - c.effectParam);
                c.freq = periodToFreq((uint16_t)c.currentPeriod, c.finetune);
                break;
            }
            case 0x2: { // 2xx: Portamento Down
                c.currentPeriod = std::min(856.0f, c.currentPeriod + c.effectParam);
                c.freq = periodToFreq((uint16_t)c.currentPeriod, c.finetune);
                break;
            }
            case 0x3: { // 3xx: Tone Portamento
                int slide = c.effectParam;
                if (c.currentPeriod > c.targetPeriod)
                    c.currentPeriod = std::max(c.currentPeriod - slide, (float)c.targetPeriod);
                else if (c.currentPeriod < c.targetPeriod)
                    c.currentPeriod = std::min(c.currentPeriod + slide, (float)c.targetPeriod);
                c.freq = periodToFreq((uint16_t)c.currentPeriod, c.finetune);
                break;
            }
            case 0x4: { // 4xy: Vibrato
                float depth = (c.effectParam & 0x0F) * 2;
                float rate = (c.effectParam >> 4) * 0.2f;
                float vib = std::sin(c.vibratoPhase) * depth;
                c.freq = periodToFreq((uint16_t)(c.currentPeriod + vib), c.finetune);
                c.vibratoPhase += rate;
                break;
            }
            case 0xC: { // Cxx: Set Volume
                c.volume = (c.effectParam & 0x3F) / 64.0f;
                break;
            }
			case 0xD: { // Dxx: Pattern Break
				// Convert BCD to row number (D34 -> row 34)
				int nextRow = ((c.effectParam >> 4) * 10) + (c.effectParam & 0x0F);

				// Move to next pattern in order list
				patternOrderPos = (patternOrderPos + 1) % mod->songLength;
				currentPattern = mod->patternOrder[patternOrderPos];

				// Set target row for next tick
				currentRow = nextRow;

				// Force advance on next tick
				tickCounter = speed;

				break;
			}
            case 0xE: { // ECx: Note Cut
                uint8_t subCmd = (c.effectParam >> 4);
                uint8_t subVal = (c.effectParam & 0x0F);
                if (subCmd == 0xC && tickCounter == subVal) {
                    c.volume = 0;
                }
                break;
            }
            case 0xF: { // Fxx: Set Speed/Tempo
                if (tickCounter == 0 && c.effectParam > 0) {
                    if (c.effectParam <= 0x1F) {
                        speed = c.effectParam;
                    } else {
                        tempo = c.effectParam;
                        tickSamples = (sampleRate * 2.5f) / tempo;
                    }
                }
                break;
            }
        }
    }
		//debug("Playing pattern %d, row %d", currentPattern, currentRow);
}

// Audio mixing function - fills buffer with mixed output of all channels
void ModEngine::mixAudio(int16_t* buffer, int samples) {
    // Clear output buffer
    std::memset(buffer, 0, samples * outputChannels * sizeof(int16_t));

    // Pause handling output silence and skip processing
    if (isPaused) {
        for (int i = 0; i < samples; ++i) {
            tickSampleCounter += 1.0f;
            if (tickSampleCounter >= tickSamples) {
                tickSampleCounter -= tickSamples;
                // Do NOT call tick(); just keep time sync
            }
        }
        return;
    }

    // Check if any solo is active
    bool anySolo = std::any_of(std::begin(soloChannel), std::end(soloChannel), [](bool s) { return s; });

    for (int i = 0; i < samples; ++i) {
        // Advance tick if needed
        while (tickSampleCounter >= tickSamples) {
            tickSampleCounter -= tickSamples;
            tick();
        }
        tickSampleCounter += 1.0f;

        for (int ch = 0; ch < MAX_CHANNELS; ++ch) {
            // Skip muted or non-soloed channels
            if (channelMuteState[ch]) continue;
            if (anySolo && !soloChannel[ch]) continue;

            ChannelState& c = channels[ch];
            if (!c.playing || !c.sampleData) continue;

            float effectiveFreq = c.freq;

            // Handle per-tick effect commands
            switch (c.effectCmd) {
                case 0x0: { // Arpeggio: alternate between base, +semitone1, +semitone2
                    int n = tickCounter % 3;
                    int semitone = (n == 1) ? (c.effectParam >> 4) :
                                   (n == 2) ? (c.effectParam & 0x0F) : 0;
                    float newPeriod = c.currentPeriod * std::pow(2.0f, -semitone / 12.0f);
                    effectiveFreq = periodToFreq((uint16_t)newPeriod, c.finetune);
                    break;
                }
                case 0x1: { // Portamento Up
                    c.currentPeriod = std::max(113.0f, c.currentPeriod - c.effectParam);
                    effectiveFreq = periodToFreq((uint16_t)c.currentPeriod, c.finetune);
                    break;
                }
                case 0x2: { // Portamento Down
                    c.currentPeriod = std::min(856.0f, c.currentPeriod + c.effectParam);
                    effectiveFreq = periodToFreq((uint16_t)c.currentPeriod, c.finetune);
                    break;
                }
                case 0x3: { // Tone Portamento toward targetPeriod
                    int slide = c.effectParam;
                    if (c.currentPeriod > c.targetPeriod)
                        c.currentPeriod = std::max(c.currentPeriod - slide, (float)c.targetPeriod);
                    else if (c.currentPeriod < c.targetPeriod)
                        c.currentPeriod = std::min(c.currentPeriod + slide, (float)c.targetPeriod);
                    effectiveFreq = periodToFreq((uint16_t)c.currentPeriod, c.finetune);
                    break;
                }
                case 0x4: { // Vibrato: LFO modulates pitch
                    float depth = (c.effectParam & 0x0F) * 2;
                    float rate = (c.effectParam >> 4) * 0.2f;
                    float vib = std::sin(c.vibratoPhase) * depth;
                    effectiveFreq = periodToFreq((uint16_t)(c.currentPeriod + vib), c.finetune);
                    c.vibratoPhase += rate;
                    break;
                }
                case 0xC: { // Set Volume
                    c.volume = (c.effectParam & 0x3F) / 64.0f;
                    break;
                }
                case 0xE: { // ECx: Note Cut after N ticks
                    uint8_t subCmd = (c.effectParam >> 4);
                    uint8_t subVal = (c.effectParam & 0x0F);
                    if (subCmd == 0xC && tickCounter == subVal) {
                        c.volume = 0;
                    }
                    break;
                }
                case 0xF: { // Fxx: Speed/Tempo handled in tick(), (unused!)
                    break;
                }
                default:
                    break;
            }

            // Sample position and looping
            int idx = static_cast<int>(c.samplePos);
            if (idx < 0 || idx >= c.sampleLength) {
                if (c.loopLength > 2) {
                    c.samplePos = static_cast<float>(c.loopStart);
                    idx = c.loopStart;
                } else {
                    c.playing = false;
                    continue;
                }
            }

            // Fetch and scale sample value
            int8_t s = c.sampleData[idx];
            constexpr float mixVolume = 0.125f; // Global gain scaler
            int16_t val = static_cast<int16_t>(s * c.volume * 512 * mixVolume);

            // Mix to output buffer (mono/stereo)
            for (int chn = 0; chn < outputChannels; ++chn) {
                int mixIdx = i * outputChannels + chn;
                int mixed = buffer[mixIdx] + val;
                buffer[mixIdx] = std::clamp(mixed, -32768, 32767);
            }

            // Advance sample position based on frequency
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
