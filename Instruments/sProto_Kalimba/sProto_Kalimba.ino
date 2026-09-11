// sProto Kalimba - banded waveguide synthesis by multiple karplus strong modes source with potentiometer controls for pitch, release, and volume
// Harmonics: G3 196, E6 1318, A#7 3951, C#8 4435
// For Arduino IDE:
// - Select "ESP32-S3 Dev Module" as the board
// - Select the appropriate Port
// - Set, USB CDC on Boot: Enabled
// - Set, PSRAM: OPI PSRAM

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Gain.h"
#include "SVF2.h"
#include "Phys.h"

// Instantiate audio objects
constexpr int voiceCount = 2;
constexpr int modeCount = 3;
WaveTable noiseTable;
Osc noise[voiceCount];
Env envelopes[voiceCount];
SVF2 exciterFilters[voiceCount];
SVF2 modeFilters[voiceCount][modeCount];
Phys modes[voiceCount][modeCount];

// Global variables
float basePitch = 55; // root pitch
float ratio1 = 6.72; // overtone 1
float ratio2 = 20.16; // overtone 2
constexpr float feedback1 = 0.999f; 
constexpr float feedback2 = 0.99f;
constexpr float feedback3 = 0.98f;
int crossCoupling = 0; // 0-1024; cross-modal coupling strength
float exciterCutoff = 800; //Hz
const int padIntervals[4] = {0, 2, 4, 7}; // unison, maj2, maj3, P5
unsigned long controlTime, midiTime;
Gain outputGain;

enum VoiceOwner : uint8_t { VOICE_FREE, VOICE_PAD, VOICE_MIDI, VOICE_BUTTON };
VoiceOwner voiceOwner[voiceCount] = {VOICE_FREE, VOICE_FREE};
int8_t voicePitch[voiceCount] = {-1, -1};
int8_t voicePadIndex[voiceCount] = {-1, -1};
uint8_t nextRoundRobinVoice = 0;
int8_t padVoice[4] = {-1, -1, -1, -1};
int8_t padLastPitch[4] = {-1, -1, -1, -1};

// All live parameter changes, whether from a pot or MIDI, pass through these setters.
// Root-pitch changes from the pitch pot or MIDI CC pass through these setters.

// Tune the currently sounding voice without changing the root pitch.
void setVoicePitch(int voice, float pitch) {
  // Retune the resonator and its excitation filter together. Cap upper modes
  // at the useful range of SVF2 and the audio sample rate.
  float fundamental = mtof(pitch); //noise.getFreq();
  float frequency1 = min(10000.0f, fundamental);
  float frequency2 = min(10000.0f, fundamental * ratio1);
  float frequency3 = min(10000.0f, fundamental * ratio2);

  modeFilters[voice][0].setFreq((int)(frequency1 + 0.5f));
  modeFilters[voice][1].setFreq((int)(frequency2 + 0.5f));
  modeFilters[voice][2].setFreq((int)(frequency3 + 0.5f));

  modes[voice][0].setPluckFreq(frequency1);
  modes[voice][1].setPluckFreq(frequency2);
  modes[voice][2].setPluckFreq(frequency3);
}

void setKeytrackDampening(int voice, float pitch) {
  // keytrack dampening
  if (pitch < 55) {
    modes[voice][0].setPluckDampCutoff(0.6);
  } else if (pitch < 85) {
    modes[voice][0].setPluckDampCutoff(floatMap(pitch, 55, 84, 0.6f, 1.0f));
  } else {
    modes[voice][0].setPluckDampCutoff(1.0);
  }
}

void updatePitch(float pitch) {
  basePitch = pitch;
  for (int voice = 0; voice < voiceCount; voice++) {
    if (voiceOwner[voice] == VOICE_PAD && voicePadIndex[voice] >= 0) {
      float padPitch = basePitch + padIntervals[voicePadIndex[voice]];
      setVoicePitch(voice, padPitch);
      setKeytrackDampening(voice, padPitch);
      voicePitch[voice] = (int8_t)padPitch;
      padLastPitch[voicePadIndex[voice]] = (int8_t)padPitch;
    } else if (voiceOwner[voice] == VOICE_BUTTON) {
      setVoicePitch(voice, basePitch);
      setKeytrackDampening(voice, basePitch);
      voicePitch[voice] = (int8_t)basePitch;
    }
  }
}

void updateExciterDampening(float cutoff) {
  for (int voice = 0; voice < voiceCount; voice++) {
    exciterFilters[voice].setFreq(cutoff);
  }
  exciterCutoff = cutoff;
  Serial.println("exciter dampening: " + String(cutoff));
}

void updateVolume(int value) {
  // outputGain.setLevel(value);
  crossCoupling = value;
  Serial.println("volume: " + String(value));
}

void applyOutputGain(int32_t &left, int32_t &right) {
  left = outputGain.next(left);
  right = outputGain.next(right);
}

void playVoice(int voice, float pitch, VoiceOwner owner, float level = 1.0f);
void releaseVoice(int voice);
void restorePadVoiceIfHeld(int voice);

// Import associated files. midi.h defines midi/sendPadNoteOn; controls.h uses them, so MIDI comes first.
#include "midi.h"
#include "controls.h"

// --- setup ---

void setup() {
  Serial.begin(115200);
  delay(200);
  noiseTable.noiseGen(); // One managed table shared by four independent readers.
  for (int voice = 0; voice < voiceCount; voice++) {
    noise[voice].setTable(noiseTable);
    noise[voice].setNoise(true);
    setVoicePitch(voice, basePitch + padIntervals[voice]);
    setKeytrackDampening(voice, basePitch + padIntervals[voice]);
    envelopes[voice].setAttack(5);
    envelopes[voice].setDecay(50);
    envelopes[voice].setSustain(0);
    envelopes[voice].setRelease(50);
    exciterFilters[voice].setFreq(exciterCutoff);
    for (int mode = 0; mode < modeCount; mode++) {
      modeFilters[voice][mode].setRes(1.0);
    }
    modes[voice][1].setPluckDampCutoff(0.6); // 0.6
    modes[voice][2].setPluckDampCutoff(0.7); // 0.7
    // Allocate all twelve resonator buffers before the real-time audio task.
    modes[voice][0].pluck(0, feedback1);
    modes[voice][1].pluck(0, feedback2);
    modes[voice][2].pluck(0, feedback3);
  }
  for (int voice = 0; voice < voiceCount; voice++) {
    modes[voice][0].setPluckSilenceThreshold(16);
    modes[voice][1].setPluckSilenceThreshold(16);
    modes[voice][2].setPluckSilenceThreshold(16);
  }
  seti2sPins(38,39,40,41); // bck, ws, data_out, data_in 
  initTouchControls(); // Keep all pads untouched during startup calibration.
  setAudioPostProcessCallback(applyOutputGain);
  // setIsDualCore(true); // Audio Core 0: voices 0/2; Arduino-loop Core 1: voice 1.
  audioStart();
}

// --- loop ---

void loop() {
  unsigned long msNow = millis();

  // check controls
  if ((unsigned long)(msNow - controlTime) >= 1) {
    controlTime = msNow;
    readControls();
  }

  // check midi
  if ((unsigned long)(msNow - midiTime) >= 5) {
    midiTime = msNow;
    readMidi();
    updateMidiOut(msNow);
  }

}

// --- audio ---

/* The audioUpdate function is required in all M16 programs. */

void audioUpdate() {
  int32_t mix = 0;

  for (int voice = audioPartitionOffset(); voice < voiceCount; voice += audioPartitionStride()) {
    int32_t source = noise[voice].nextUnlocked();
    int32_t strike = ((int64_t)source * envelopes[voice].next()) >> 16;
    int32_t dampenedStrike = exciterFilters[voice].nextLPFUnlocked(strike);

    int32_t mode1 = modes[voice][0].pluck(modeFilters[voice][0].nextBPFUnlocked(clip16(dampenedStrike)), feedback1);
    int32_t mode2 = modes[voice][1].pluck(modeFilters[voice][1].nextBPFUnlocked(clip16(dampenedStrike)), feedback2);
    int32_t mode3 = modes[voice][2].pluck(modeFilters[voice][2].nextBPFUnlocked(clip16(dampenedStrike)), feedback3);

    int32_t voiceMix = clip16(((dampenedStrike >> 1) + mode1 + (mode2 >> 1) + (mode3 >> 1)) >> 1);
    mix = clip16(mix + voiceMix);
  }

  audioBlockWrite(mix, mix); // M16 combines both voice partitions first.
}
