// sProto Mono - a two-scilator single-voice synthesizer with controls for
// pitch, oscillator balance, cutoff, amplitude and filter envelopes, and resonance.
// Inspired by the Novation Bass Station
// Andrew R. Brown 2026
// For Arduino IDE:
// - Select "ESP32-S3 Dev Module" as the board
// - Select the appropriate Port
// - Set, USB CDC on Boot: Enabled
// - Set, PSRAM: OPI PSRAM
//
// Synth Controls:
// A - Pitch, B - Oscillator Balance, C - Filter Cutoff
// Button+A - Amplitude Envelope, Button+B - Filter Envelope, Button+C - Filter Resonance
// Touch pads: 4 pitches from C major scale, pitch control jumps the base pitch to next C or G up or down

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF2.h"
#include "FX.h"
#include "Gain.h"
#include "Voices.h"

// Shared waveform memory and audio objects
WaveTable sineTable;
WaveTable squareTable;
WaveTable sawtoothTable;
WaveTable triangleTable;

// Global variables
Osc osc1;
Osc osc2;
Osc pitchLfo;
Osc pulseWidthLfo;
Osc subOsc;
Env env;
Env filterEnv;
Env lfoEnv;
SVF2 lowPassFilter;
FX effects; // Chorus and soft clipping process the summed voice mix.

// Change LAST_NOTE to LOW_NOTE or HIGH_NOTE for a different key priority.
Voices allocator(1, LEGATO, LAST_NOTE);
float velocityAmpMod = 0.7;
float velocityFilterMod = 0.4;

volatile bool voiceHeld = false;
float voicePitch = 60.0f;
float targetPitch = 60.0f;
float glideAmnt = 0.1; // 0.0 - 1.0
unsigned long lfoStartTime = 0;
float filterPitchScale = 1.0f;
float filterVelocityScale = 1.0f;
float smoothedFilterCutoff = 0.0f;
float pitchBendSemitones = 0.0f;
int8_t octave = -2;
float overdriveLevel = 2.0f; // 1 - 3
float distortionLevel = 1.5f; // 1 - 5

float basePitch = 60; // middle C; Dial 1 selects C/G roots from MIDI 24-96
const uint8_t rootPitches[] = {24, 31, 36, 43, 48, 55, 60, 67, 72, 79, 84, 91, 96};
const int padIntervals[4] = {0, 2, 4, 5}; // unison, maj2, maj3, P4
unsigned long controlTime, midiTime, filterEnvTime, pulseWidthTime, lfoTime, pitchTime;

Gain outputGain;
const int32_t oscBalanceScale = 1000;
uint16_t osc1Balance = oscBalanceScale;
uint16_t osc2Balance = 0;
// Musical vibrato depth in semitones. MIDI Mod Wheel varies this from 0 to 1.
volatile float maxModIndex = 0.01f;
const float vibratoRate = 4.0f;
const unsigned long lfoUpdateInterval = 4;
const float pulseWidthLfoRate = 1.0f;
const unsigned long pulseWidthUpdateInterval = 29;
const float pulseWidthCentre = 0.475f;
const float pulseWidthDeviation = 0.05f;
const unsigned long pitchUpdateInterval = 7;
volatile int subOscLevel = 800; // 0-1024
const float envShape1[] = {5, 300, 0.0f, 300}; // ADSR
const float envShape2[] = {20, 800, 0.3f, 500};
const float envShape3[] = {50, 5000, 0.6f, 800};
const float filterEnvFollow = 1.6;
float baseFilterCutoff = 0.3f;
float baseFilterResonance = 0.2f;
const float filterPitchFollow = 0.5f;

// All live parameter changes, whether from a pot or MIDI, pass through these setters.

void updateVibratoDepth(float semitones) {
  maxModIndex = max(0.0f, semitones);
}

void updateGlideAmount(int value) {
  value = max(0, min(1023, value));
  glideAmnt = 1.0f - value / 1023.0f;
}

void updateOverdriveLevel(int value) {
  value = max(0, min(1023, value));
  overdriveLevel = map(value, 0, 1023, 10, 30) * 0.1f;
}

void updateDistortionLevel(int value) {
  value = max(0, min(1023, value));
  distortionLevel = map(value, 0, 1023, 10, 50) * 0.1f;
}

void applyVoicePitch(float vibratoSemitones = 0.0f) {
  float soundingPitch = voicePitch + pitchBendSemitones + vibratoSemitones;
  float pitchRatio = powf(2.0f, (soundingPitch - 60.0f) / 12.0f);
  float cutoffMultiplier = 1.0f + (pitchRatio - 1.0f) * filterPitchFollow;
  filterPitchScale = cbrtf(cutoffMultiplier);
  osc1.setPitch(soundingPitch);
  osc2.setFreq(osc1.getFreq() * 0.999f); // slight detune
  subOsc.setPitch(soundingPitch - 12.0f);
}

void updateVibratoPitches(unsigned long msNow) {
  if (!voiceHeld && env.getEnvState() == 0) return;
  const float lfoValue = pitchLfo.atTimeNormal(msNow - lfoStartTime);
  const float envelope = lfoEnv.getValue() * MAX_16_INV;
  const float vibratoSemitones = (lfoValue - 0.5f) * 2.0f * maxModIndex * envelope;
  applyVoicePitch(vibratoSemitones);
}

void setVoicePitch(float pitch) {
  targetPitch = pitch;
}

void updatePitchBend(float semitones) {
  pitchBendSemitones = semitones;
  applyVoicePitch();
}

// Apply allocation decisions on the control thread; DSP remains in the sketch.
void applyVoiceChanges() {
  Voices::Change change;
  while (allocator.readChange(change)) {
    if (change.action == VOICE_RELEASE) {
      voiceHeld = false;
      env.startRelease();
      filterEnv.startRelease();
      lfoEnv.startRelease();
      continue;
    }
    const float velocityNormal = (change.note.velocity - 1) / 126.0f;
    const float ampLevel = 1.0f - max(0.0f, min(1.0f, velocityAmpMod)) * (1.0f - velocityNormal);
    const float cutoffScale = 1.0f - max(0.0f, min(1.0f, velocityFilterMod)) * (1.0f - velocityNormal);
    setVoicePitch(change.note.pitch);
    env.setMaxLevel(ampLevel);
    filterVelocityScale = cbrtf(cutoffScale);
    voiceHeld = true;
    if (change.action == VOICE_START) {
      env.start();
      filterEnv.start();
      lfoStartTime = millis();
      lfoEnv.start();
    }
  }
}

void startVoice(float pitch, uint8_t source, uint16_t key, uint8_t velocity = 127, uint8_t channel = 0) {
  allocator.setVoiceIdle(0, env.getEnvState() == 0);
  allocator.noteOn(source, key, pitch, velocity, channel);
  applyVoiceChanges();
}

void releasePadVoice(uint8_t pad) {
  if (pad < 4) allocator.noteOff(HARDWARE_SOURCE, pad);
  applyVoiceChanges();
}

void releaseMidiVoice(uint8_t pitch, uint8_t channel = 0) {
  allocator.noteOff(MIDI_SOURCE, pitch, channel);
  applyVoiceChanges();
}

void updateOscBalance(int value) {
  value = max(0, min(1023, value));
  const float balance = value * (1.0f / 1023.0f);
  osc1Balance = (uint16_t)(panLeft(balance) * oscBalanceScale + 0.5f);
  osc2Balance = (uint16_t)(panRight(balance) * oscBalanceScale + 0.5f);
}

void updatePitch(float pitch) {
  basePitch = pitch + octave * 12;
  // Retune every held pad, keeping note identity and press order intact.
  for (uint8_t pad = 0; pad < 4; ++pad) {
    allocator.setNotePitch(HARDWARE_SOURCE, pad, basePitch + padIntervals[pad]);
    applyVoiceChanges();
  }
}

void updatePitchFromDial(int value) {
  value = max(0, min(1023, value));
  const int lastRoot = (sizeof(rootPitches) / sizeof(rootPitches[0])) - 1;
  int rootIndex = (value * lastRoot + 511) / 1023;
  updatePitch(rootPitches[rootIndex]);
}

void calculateEnvelopeMorph(int value, float& attack, float& decay, float& sustain, float& release) {
  value = max(0, min(1023, value));

  const float* fromShape;
  const float* toShape;
  float amount;
  if (value <= 512) {
    fromShape = envShape1;
    toShape = envShape2;
    amount = value / 512.0f;
  } else {
    fromShape = envShape2;
    toShape = envShape3;
    amount = (value - 512) / 511.0f;
  }

  attack = fromShape[0] + (toShape[0] - fromShape[0]) * amount;
  decay = fromShape[1] + (toShape[1] - fromShape[1]) * amount;
  sustain = fromShape[2] + (toShape[2] - fromShape[2]) * amount;
  release = fromShape[3] + (toShape[3] - fromShape[3]) * amount;
}

void updateEnvelopeMorph(int value) {
  float attack, decay, sustain, release;
  calculateEnvelopeMorph(value, attack, decay, sustain, release);

  env.setAttack(attack);
  env.setDecay(decay);
  env.setSustain(sustain);
  env.setRelease(release);
}

void updateFilterEnvelopeMorph(int value) {
  float attack, decay, sustain, release;
  calculateEnvelopeMorph(value, attack, decay, sustain, release);

  filterEnv.setAttack(attack);
  filterEnv.setDecay(decay);
  filterEnv.setSustain(sustain);
  filterEnv.setRelease(release);
}

void updateFilterCutoff(int value) {
  value = max(0, min(1023, value));
  baseFilterCutoff = value / 1023.0f;
}

void updateFilterResonance(int value) {
  value = max(0, min(1023, value));
  baseFilterResonance = value / 1023.0f;
  lowPassFilter.setRes(baseFilterResonance);
}

void updateChorusDepth(int value) {
  value = max(0, min(1023, value));
  effects.setChorusDepth(value / 1023.0f);
  Serial.println("Chorus: " + String(value / 1023.0f));
}

void updateSubOscLevel(int value) {
  value = max(0, min(1023, value));
  subOscLevel = (value * 512 + 511) / 1023;
}

void updateFilterEnvelopeCutoff() {
  if (!voiceHeld && filterEnv.getEnvState() == 0) return;
  float envelopeLevel = filterEnv.getValue() / 65534.0f;
  float cutoff = baseFilterCutoff * filterPitchScale * filterVelocityScale *
      (1.0f + envelopeLevel * filterEnvFollow);
  cutoff = min(1.0f, cutoff);
  // Smooth abrupt cutoff changes, including the filter envelope resetting at
  // note-on. At the 4 ms control rate, 0.25 gives a short click-free glide.
  smoothedFilterCutoff += (cutoff - smoothedFilterCutoff) * 0.25f;
  lowPassFilter.setCutoff(smoothedFilterCutoff);
}

// Master processing runs once, after M16 combines the two voice partitions.
void audioPostProcess(int32_t &left, int32_t &right) {
  // int32_t compressed = effects.compression(left);
  int32_t limited = effects.softLimit(left, 0.85f);
  int32_t outL, outR;
  effects.chorusStereo(limited, limited, outL, outR);
  left = outputGain.next(outL);
  right = outputGain.next(outR);
}

void syncPotBankValue(uint8_t dial, uint8_t bank, int value);

// Import associated files. midi.h defines midi/sendPadNoteOn; controls.h uses them, so MIDI comes first.
#include "midi.h"
#include "controls.h"

// --- setup ---

void setup() {
  // Retrigger new presses, but keep envelopes running when returning to a held key.
  allocator.setRetriggerOnNoteOn(true);
  Serial.begin(115200);
  delay(200);
  sineTable.sinGen();
  squareTable.sqrGen();
  sawtoothTable.sawGen();
  triangleTable.triGen();
  pitchLfo.setTable(sineTable);
  pitchLfo.setFreq(vibratoRate);
  pulseWidthLfo.setTable(triangleTable);
  pulseWidthLfo.setFreq(pulseWidthLfoRate);
  osc1.setTable(sawtoothTable);
  osc2.setTable(squareTable);
  osc2.setPulseWidthSmoothing((float)pulseWidthUpdateInterval);
  subOsc.setTable(sineTable);
  filterVelocityScale = 1.0f;
  setVoicePitch(basePitch);
  updateOscBalance(512); // equal-power center
  updatePitch(basePitch);
  updateEnvelopeMorph(512); // start exactly at envShape2
  updateFilterEnvelopeMorph(512); // independent filter shape starts at envShape2
  // Give every note a complete, repeatable filter sweep. Without this,
  // retriggering begins at the filter envelope's current level.
  // Retrigger amplitude from its current level to avoid a zero-level jump.
  env.setResetOnStart(false);
  // Reset the filter envelope cleanly, smoothing the cutoff transition.
  filterEnv.setResetOnStart(true);
  filterEnv.setResetTransition(5.0f);
  lfoEnv.setAttack(1500);
  lfoEnv.setDecay(0);
  lfoEnv.setSustain(1.0);
  lfoEnv.setRelease(500);
  lfoEnv.setResetOnStart(true);
  lowPassFilter.setCutoff(baseFilterCutoff);
  smoothedFilterCutoff = baseFilterCutoff * filterPitchScale;
  lowPassFilter.setRes(baseFilterResonance);
  effects.setCompression(0.5f, 4.0f, 5.0f, 100.0f);
  effects.setChorusDepth(0.0f); // wet/dry mix
  effects.setChorusWidth(0.6f); // detune amount
  effects.setChorusSpread(1.0f); // stereo image
  setAudioPostProcessCallback(audioPostProcess);
  setIsDualCore(false); // The single oscillator has no voice partitions.
  seti2sPins(38,39,40,41);
  initTouchControls(); // Keep all pads untouched during startup calibration.
  initMidiInput();
  audioStart();
}

// --- loop ---

void loop() {
  #if IS_RP2040()
    audioLoop(); // Service the cooperative second partition on RP2040.
  #endif

  unsigned long msNow = millis();

  // check controls
  if ((unsigned long)(msNow - controlTime) >= 1) {
    controlTime = msNow;
    readControls();
  }

  if ((unsigned long)(msNow - filterEnvTime) >= 4) {
    filterEnvTime = msNow;
    updateFilterEnvelopeCutoff();
  }

  if ((unsigned long)(msNow - lfoTime) >= lfoUpdateInterval) {
    lfoTime = msNow;
    updateVibratoPitches(msNow);
  }

  if ((unsigned long)(msNow - pulseWidthTime) >= pulseWidthUpdateInterval) {
    pulseWidthTime = msNow;
    const float lfoValue = pulseWidthLfo.atTime(msNow) * MAX_16_INV;
    osc2.setPulseWidth(pulseWidthCentre + lfoValue * pulseWidthDeviation);
  }

  if ((unsigned long)(msNow - pitchTime) >= pitchUpdateInterval) {
    pitchTime = msNow;
    if (voicePitch != targetPitch) {
      voicePitch = slew(voicePitch, targetPitch, 1.0f - glideAmnt);
      if (fabsf(targetPitch - voicePitch) < 0.001f) voicePitch = targetPitch; // close?
      applyVoicePitch();
    }
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
  if (voiceHeld || env.getEnvState() != 0) {
    const int32_t osc1Sample = osc1.nextUnlocked();
    const int32_t osc2Sample = osc2.nextUnlocked();
    int32_t tone = (osc1Sample * osc1Balance + osc2Sample * osc2Balance) / oscBalanceScale;
    tone += (subOsc.nextUnlocked() * subOscLevel) >> 10;
    tone = effects.softClipTube(tone, overdriveLevel);
    tone = lowPassFilter.next(tone);
    tone = effects.softClipAtan(tone, distortionLevel);
    const int32_t voiceOut = (tone * env.next()) >> 16;
    mix = voiceOut;
  }
  audioBlockWrite(mix, mix);
}
