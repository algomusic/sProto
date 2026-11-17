// sProto Poly - a multi-voice synthesizer with controls for pitch, waveform, cutoff, anp and filter envelop, and chorus.
// Inspired by the Roland Juno 6
// Andrew R. Brown 2026
// For Arduino IDE:
// - Select "ESP32-S3 Dev Module" as the board
// - Select the appropriate Port
// - Set, USB CDC on Boot: Enabled
// - Set, PSRAM: OPI PSRAM
//
// Synth Controls:
// A - Pitch, B - Waveform, C - Filter Cutoff
// Button+A - Amplitude Envelope, Button+B - Filter Envelope, Button+C - Chorus Depth
// Touch pads: 4 pitches from C major scale, pitch control jumps the base pitch to next C or G up or down

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Bobi.h"
#include "FX.h"
#include "Gain.h"
#include "Voices.h"

// Shared waveform memory and audio objects
WaveTable cosTable;
WaveTable pulseTable;
WaveTable squareTable;
WaveTable sawtoothTable;
WaveTable noiseTable;

const uint8_t WAVE_COSINE = 0;
const uint8_t WAVE_PULSE = 1;
const uint8_t WAVE_SQUARE = 2;
const uint8_t WAVE_SAWTOOTH = 3;
const uint8_t WAVE_NOISE = 4;

// Global variables
const uint8_t voices = 6; // Change this at compile-time to set the polyphony.
const int ampCompensation = 1023.0f * 0.7f; // Reduce volume to avoid distorting polyphonic playback
Osc osc[voices];
Osc lfoOsc;
Osc subOsc[voices];
Env env[voices];
Env filterEnv[voices];
Env lfoEnv[voices];
Bobi lowPassFilter[voices];
FX effects; // Chorus and soft clipping process the summed voice mix.

// Change LAST_NOTE to LOW_NOTE or HIGH_NOTE to prioritize pitches when full.
Voices allocator(voices, POLY, LAST_NOTE);
float velocityAmpMod = 0.7;
float velocityFilterMod = 0.4;

volatile bool voiceHeld[voices] = {};
float voicePitch[voices] = {};
unsigned long lfoStartTime[voices] = {};
float filterPitchScale[voices] = {};
float filterVelocityScale[voices] = {};
float smoothedFilterCutoff[voices] = {};
float pitchBendSemitones = 0.0f;

float basePitch = 60; // middle C; Dial 1 selects C/G roots from MIDI 24-96
const uint8_t rootPitches[] = {24, 31, 36, 43, 48, 55, 60, 67, 72, 79, 84, 91, 96};
const int padIntervals[4] = {0, 2, 4, 5}; // unison, maj2, maj3, P4
unsigned long controlTime, midiTime, filterEnvTime, lfoTime;
Gain outputGain;
uint8_t selectedWaveform = WAVE_SAWTOOTH;
// Musical vibrato depth in semitones. MIDI Mod Wheel varies this from 0 to 1.
volatile float maxModIndex = 0.01f;
const float vibratoRate = 4.0f;
const unsigned long lfoUpdateInterval = 4;
volatile int subOscLevel = 0; // 0-512: linked to chorus depth, maximum 50%
const float envShape1[] = {5, 400, 0.0f, 400};
const float envShape2[] = {20, 800, 0.5f, 700};
const float envShape3[] = {2000, 1500, 0.9f, 4000};
const float filterEnvFollow = 1.3;
float baseFilterCutoff = cbrtf(1000.0f / 20000.0f);
const float filterPitchFollow = 0.5f;

// All live parameter changes, whether from a pot or MIDI, pass through these setters.

void updateVibratoDepth(float semitones) {
  maxModIndex = max(0.0f, semitones);
}

void applyVoicePitch(uint8_t voice) {
  float soundingPitch = voicePitch[voice] + pitchBendSemitones;
  float pitchRatio = powf(2.0f, (soundingPitch - 60.0f) / 12.0f);
  float cutoffMultiplier = 1.0f + (pitchRatio - 1.0f) * filterPitchFollow;
  filterPitchScale[voice] = cbrtf(cutoffMultiplier);
  osc[voice].setPitch(soundingPitch);
  subOsc[voice].setPitch(soundingPitch - 12.0f);
}

void updateVibratoPitches(unsigned long msNow) {
  for (uint8_t voice = 0; voice < voices; voice++) {
    if (!voiceHeld[voice] && env[voice].getEnvState() == 0) continue;
    const float lfoValue = lfoOsc.atTimeNormal(msNow - lfoStartTime[voice]);
    const float envelope = lfoEnv[voice].getValue() * MAX_16_INV;
    const float vibratoSemitones = (lfoValue - 0.5f) * 2.0f * maxModIndex * envelope;
    osc[voice].setPitch(voicePitch[voice] + pitchBendSemitones +
                        vibratoSemitones);
  }
}

void setVoicePitch(uint8_t voice, float pitch) {
  voicePitch[voice] = pitch; // retain the unbent note so bend never accumulates
  applyVoicePitch(voice);
}

void updatePitchBend(float semitones) {
  pitchBendSemitones = semitones;
  for (uint8_t voice = 0; voice < voices; voice++) applyVoicePitch(voice);
}

// Apply allocation decisions on the control thread; DSP remains in the sketch.
void applyVoiceChanges() {
  Voices::Change change;
  while (allocator.readChange(change)) {
    const uint8_t v = change.voice;
    if (change.action == VOICE_RELEASE) {
      voiceHeld[v] = false;
      env[v].startRelease();
      filterEnv[v].startRelease();
      lfoEnv[v].startRelease();
      continue;
    }
    const float velocityNormal = (change.note.velocity - 1) / 126.0f;
    const float ampLevel = 1.0f - max(0.0f, min(1.0f, velocityAmpMod)) * (1.0f - velocityNormal);
    const float cutoffScale = 1.0f - max(0.0f, min(1.0f, velocityFilterMod)) * (1.0f - velocityNormal);
    setVoicePitch(v, change.note.pitch);
    env[v].setMaxLevel(ampLevel);
    filterVelocityScale[v] = cbrtf(cutoffScale);
    voiceHeld[v] = true;
    if (change.action == VOICE_START) {
      env[v].start();
      filterEnv[v].start();
      lfoStartTime[v] = millis();
      lfoEnv[v].start();
    }
  }
}

void startVoice(float pitch, uint8_t source, uint16_t key, uint8_t velocity = 127, uint8_t channel = 0) {
  for (uint8_t v = 0; v < voices; ++v)
    allocator.setVoiceIdle(v, env[v].getEnvState() == 0);
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

void updateWaveformSelection(int value) {
  value = max(0, min(1023, value));
  const uint8_t waveform = min((uint8_t)4, (uint8_t)((value * 5L) / 1024L));
  selectedWaveform = waveform;

  for (uint8_t voice = 0; voice < voices; voice++) {
    osc[voice].setNoise(selectedWaveform == WAVE_NOISE);
    osc[voice].setSandH(false);
    switch (selectedWaveform) {
      case WAVE_COSINE:   osc[voice].setTable(cosTable); break;
      case WAVE_PULSE:    osc[voice].setTable(pulseTable); break;
      case WAVE_SQUARE:   osc[voice].setTable(squareTable); break;
      case WAVE_NOISE:    osc[voice].setTable(noiseTable); break;
      case WAVE_SAWTOOTH:
      default:            osc[voice].setTable(sawtoothTable); break;
    }
  }
}

void updatePitch(float pitch) {
  basePitch = pitch;
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

  for (uint8_t voice = 0; voice < voices; voice++) {
    env[voice].setAttack(attack);
    env[voice].setDecay(decay);
    env[voice].setSustain(sustain);
    env[voice].setRelease(release);
  }
}

void updateFilterEnvelopeMorph(int value) {
  float attack, decay, sustain, release;
  calculateEnvelopeMorph(value, attack, decay, sustain, release);

  for (uint8_t voice = 0; voice < voices; voice++) {
    filterEnv[voice].setAttack(attack);
    filterEnv[voice].setDecay(decay);
    filterEnv[voice].setSustain(sustain);
    filterEnv[voice].setRelease(release);
  }
}

void updateFilterCutoff(int value) {
  value = max(0, min(1023, value));
  baseFilterCutoff = value / 1023.0f;
}

void updateChorusDepth(int value) {
  value = max(0, min(1023, value));
  effects.setChorusDepth(value / 1023.0f);
  subOscLevel = value >> 3; // rise with chorus depth
}

void updateFilterEnvelopeCutoff() {
  for (uint8_t voice = 0; voice < voices; voice++) {
    if (!voiceHeld[voice] && filterEnv[voice].getEnvState() == 0) continue;
    float envelopeLevel = filterEnv[voice].getValue() / 65534.0f;
    float cutoff = baseFilterCutoff * filterPitchScale[voice] * filterVelocityScale[voice] * (1.0f + envelopeLevel * filterEnvFollow);
    cutoff = min(1.0f, cutoff);
    // Smooth abrupt cutoff changes, including the filter envelope resetting at
    // note-on. At the 4 ms control rate, 0.25 gives a short click-free glide.
    smoothedFilterCutoff[voice] += (cutoff - smoothedFilterCutoff[voice]) * 0.25f;
    lowPassFilter[voice].setCutoff(smoothedFilterCutoff[voice]);
  }
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
  Serial.begin(115200);
  delay(200);
  cosTable.cosGen();
  pulseTable.pulseGen(0.075);
  squareTable.sqrGen();
  sawtoothTable.sawGen();
  noiseTable.noiseGen();
  lfoOsc.setTable(cosTable);
  lfoOsc.setFreq(vibratoRate);
  for (uint8_t voice = 0; voice < voices; voice++) {
    osc[voice].setTable(cosTable);
    subOsc[voice].setTable(squareTable);
    filterVelocityScale[voice] = 1.0f;
    setVoicePitch(voice, basePitch);
  }
  updateWaveformSelection(767); // start on sawtooth
  updatePitch(basePitch);
  updateEnvelopeMorph(512); // start exactly at envShape2
  updateFilterEnvelopeMorph(512); // independent filter shape starts at envShape2
  // Give every note a complete, repeatable filter sweep. Without this,
  // retriggering begins at the filter envelope's current level.
  for (uint8_t voice = 0; voice < voices; voice++) {
    // Retrigger amplitude from its current level so reusing a release tail or
    // stealing a sustained voice does not introduce a zero-level discontinuity.
    env[voice].setResetOnStart(false);
    // The filter envelope still starts a complete sweep; its resulting cutoff
    // reset is smoothed at control rate in updateFilterEnvelopeCutoff().
    filterEnv[voice].setResetOnStart(true);
    filterEnv[voice].setResetTransition(5.0f);
    // LFO envelope
    lfoEnv[voice].setAttack(1500);
    lfoEnv[voice].setDecay(0);
    lfoEnv[voice].setSustain(1.0);
    lfoEnv[voice].setRelease(500);
    lfoEnv[voice].setResetOnStart(true);
  }
  // Bob::setCutoff() uses a cubic 0.0-1.0 mapping to 0-20 kHz.
  for (uint8_t voice = 0; voice < voices; voice++) {
    lowPassFilter[voice].setCutoff(baseFilterCutoff);
    smoothedFilterCutoff[voice] = baseFilterCutoff * filterPitchScale[voice];
    lowPassFilter[voice].setRes(0.04);
    // Voice partitioning gives each filter one permanent audio-core owner.
    lowPassFilter[voice].setThreadSafe(false);
  }
  effects.setCompression(0.5f, 4.0f, 5.0f, 100.0f);
  effects.setChorusDepth(0.0f); // wet/dry mix
  effects.setChorusWidth(0.6f); // detune amount
  effects.setChorusSpread(1.0f); // stereo image
  setAudioPostProcessCallback(audioPostProcess);
  setIsDualCore(true); // Partition the independent voice arrays across both cores.
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

  // Keep the finalizer core lighter because it also runs audioPostProcess().
  // With ffinalizerVoiceCount = 2 it owns voices 0-1 and the
  // producer owns voices 2. If dual-core partitioning is unavailable, the
  // active audio core renders the complete voice range.
  uint8_t firstVoice = 0;
  uint8_t endVoice = voices;
  if (audioPartitionIsActive()) {
    const uint8_t finalizerVoiceCount = 2; //voices / 2;
    if (audioIsFinalizerCore()) {
      endVoice = finalizerVoiceCount;
    } else {
      firstVoice = finalizerVoiceCount;
    }
  }

  for (uint8_t voice = firstVoice; voice < endVoice; ++voice) {
    if (!voiceHeld[voice] && env[voice].getEnvState() == 0) continue;
    int32_t tone = osc[voice].next();
    tone = tone + ((subOsc[voice].next() * subOscLevel) >> 10);
    tone = lowPassFilter[voice].next(tone);
    const int32_t voiceOut = (tone * env[voice].next()) >> 16;
    mix = mix + ((voiceOut * ampCompensation) >> 10); // sum each voice
  }
  audioBlockWrite(mix, mix);
}
