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

// Shared waveform memory and audio objects
WaveTable cosTable;
WaveTable pulseTable;
WaveTable squareTable;
WaveTable sawtoothTable;
WaveTable noiseTable;

enum MorphTable : uint8_t {
  MORPH_PULSE,
  MORPH_SQUARE,
  MORPH_SAWTOOTH,
  MORPH_NOISE
};

// Global variables
const uint8_t voices = 5; // Change this compile-time value to set the polyphony.
const int ampCompensation = 1023.0f / voices * 1.5f; // How much to reduce volume to avoid distorting polyphonic playback
Osc osc[voices];
Osc lfoOsc[voices];
Osc subOsc[voices];
Env env[voices];
Env filterEnv[voices];
Env lfoEnv[voices];
Bobi lowPassFilter[voices];
FX effects; // Chorus and soft clipping process the summed voice mix.

enum VoiceSource : uint8_t {
  VOICE_FREE,
  VOICE_PAD,
  VOICE_MIDI
};

volatile bool voiceHeld[voices] = {};
VoiceSource voiceSource[voices] = {};
int16_t voiceKey[voices] = {};
float voicePitch[voices] = {};
float filterPitchScale[voices] = {};
float filterVelocityScale[voices] = {};
float smoothedFilterCutoff[voices] = {};
float pitchBendSemitones = 0.0f;
int8_t padVoice[4] = {-1, -1, -1, -1};
uint8_t nextVoice = 0;

float basePitch = 60; // middle C; Dial 1 selects C/G roots from MIDI 24-96
const uint8_t rootPitches[] = {24, 31, 36, 43, 48, 55, 60, 67, 72, 79, 84, 91, 96};
const int padIntervals[4] = {0, 2, 4, 5}; // unison, maj2, maj3, P4
unsigned long controlTime, midiTime;
unsigned long filterEnvTime;
Gain outputGain;
MorphTable morphTable = MORPH_SAWTOOTH;
float morphAmount = 0.0f;
// Musical vibrato depth in semitones. MIDI Mod Wheel varies this from 0 to 0.05.
volatile float maxModIndex = 0.01f;
const float vibratoRate = 4.0f;
float maxPhaseModIndexes[voices] = {};
float phaseModIndexes[voices] = {};
volatile int subOscLevel = 0; // 0-512: linked to chorus depth, maximum 50%
const float envShape1[] = {0, 400, 0.0f, 400};
const float envShape2[] = {20, 800, 0.5f, 700};
const float envShape3[] = {2000, 1500, 0.9f, 4000};
const float filterEnvFollow = 1.3;
float baseFilterCutoff = cbrtf(1000.0f / 20000.0f);
const float filterPitchFollow = 0.5f;

// All live parameter changes, whether from a pot or MIDI, pass through these setters.
// Root-pitch changes from the pitch pot or MIDI CC pass through these setters.

void updateVoiceVibratoDepth(uint8_t voice) {
  const float frequencyDeviation = osc[voice].getFreq()
      * (powf(2.0f, maxModIndex / 12.0f) - 1.0f);
  maxPhaseModIndexes[voice] = frequencyDeviation
      / (2.0f * PI * vibratoRate);
}

void updateVibratoDepth(float semitones) {
  maxModIndex = max(0.0f, semitones);
  for (uint8_t voice = 0; voice < voices; voice++) {
    updateVoiceVibratoDepth(voice);
  }
}

void applyVoicePitch(uint8_t voice) {
  float soundingPitch = voicePitch[voice] + pitchBendSemitones;
  float pitchRatio = powf(2.0f, (soundingPitch - 60.0f) / 12.0f);
  float cutoffMultiplier = 1.0f + (pitchRatio - 1.0f) * filterPitchFollow;
  filterPitchScale[voice] = cbrtf(cutoffMultiplier);
  osc[voice].setPitch(soundingPitch);
  updateVoiceVibratoDepth(voice);
  // lfoOsc is currently a fixed-frequency LFO and is not pitch-bent.
  subOsc[voice].setPitch(soundingPitch - 12.0f);
}

void setVoicePitch(uint8_t voice, float pitch) {
  voicePitch[voice] = pitch; // retain the unbent note so bend never accumulates
  applyVoicePitch(voice);
}

void updatePitchBend(float semitones) {
  pitchBendSemitones = semitones;
  for (uint8_t voice = 0; voice < voices; voice++) applyVoicePitch(voice);
}

void detachVoice(uint8_t voice) {
  if (voiceSource[voice] == VOICE_PAD && voiceKey[voice] >= 0 &&
      voiceKey[voice] < 4 && padVoice[voiceKey[voice]] == (int8_t)voice) {
    padVoice[voiceKey[voice]] = -1;
  }
}

uint8_t allocateVoice() {
  // First prefer a completely finished voice, starting at the round-robin
  // cursor. This preserves audible release tails when silent voices exist.
  for (uint8_t offset = 0; offset < voices; offset++) {
    uint8_t candidate = (nextVoice + offset) % voices;
    if (!voiceHeld[candidate] && env[candidate].getEnvState() == 0) {
      nextVoice = (candidate + 1) % voices;
      return candidate;
    }
  }

  // If every idle voice is still releasing, reuse a release tail before
  // disturbing any key or pad that remains held.
  for (uint8_t offset = 0; offset < voices; offset++) {
    uint8_t candidate = (nextVoice + offset) % voices;
    if (!voiceHeld[candidate]) {
      nextVoice = (candidate + 1) % voices;
      return candidate;
    }
  }

  // Every voice is sustained: steal only the next round-robin voice.
  uint8_t candidate = nextVoice;
  nextVoice = (nextVoice + 1) % voices;
  detachVoice(candidate);
  return candidate;
}

uint8_t startVoice(float pitch, VoiceSource source, int16_t key, float ampLevel = 1.0f, float cutoffFrequencyScale = 1.0f) {
  uint8_t voice = allocateVoice();
  detachVoice(voice);
  setVoicePitch(voice, pitch);
  env[voice].setMaxLevel(max(0.0f, min(1.0f, ampLevel)));
  // Bob's normalized cutoff control is cubic, so convert the desired
  // frequency-domain velocity multiplier into its control domain once here.
  filterVelocityScale[voice] = cbrtf(
      max(0.0f, cutoffFrequencyScale));
  voiceSource[voice] = source;
  voiceKey[voice] = key;
  voiceHeld[voice] = true;
  if (source == VOICE_PAD && key >= 0 && key < 4) padVoice[key] = voice;
  env[voice].start();
  filterEnv[voice].start();
  lfoOsc[voice].setPhase(0);
  lfoEnv[voice].start();
  return voice;
}

void releaseVoice(uint8_t voice) {
  if (voice >= voices || !voiceHeld[voice]) return;
  detachVoice(voice);
  voiceHeld[voice] = false;
  voiceSource[voice] = VOICE_FREE;
  env[voice].startRelease();
  filterEnv[voice].startRelease();
  lfoEnv[voice].startRelease();
}

void releasePadVoice(uint8_t pad) {
  if (pad < 4 && padVoice[pad] >= 0) releaseVoice(padVoice[pad]);
}

void releaseMidiVoice(uint8_t pitch) {
  // Release the first matching held note in round-robin order. This also
  // handles repeated note-ons of the same pitch one note-off at a time.
  for (uint8_t offset = 0; offset < voices; offset++) {
    uint8_t candidate = (nextVoice + offset) % voices;
    if (voiceHeld[candidate] && voiceSource[candidate] == VOICE_MIDI &&
        voiceKey[candidate] == pitch) {
      releaseVoice(candidate);
      return;
    }
  }
}

void updateWaveMorph(int value) {
  value = max(0, min(1023, value));
  int segment = value >> 8;
  morphAmount = (value & 0xFF) * (1.0f / 255.0f);

  for (uint8_t voice = 0; voice < voices; voice++) {
    osc[voice].setNoise(false);
    osc[voice].setSandH(false);
  }

  if (segment == 0) { // cosine -> pulse
    for (uint8_t voice = 0; voice < voices; voice++) osc[voice].setTable(cosTable);
    morphTable = MORPH_PULSE;
  } else if (segment == 1) { // pulse -> square
    for (uint8_t voice = 0; voice < voices; voice++) osc[voice].setTable(pulseTable);
    morphTable = MORPH_SQUARE;
  } else if (segment == 2) { // square -> sawtooth
    for (uint8_t voice = 0; voice < voices; voice++) osc[voice].setTable(squareTable);
    morphTable = MORPH_SAWTOOTH;
  } else { // sawtooth -> noise
    for (uint8_t voice = 0; voice < voices; voice++) osc[voice].setTable(sawtoothTable);
    morphTable = MORPH_NOISE;
    morphAmount *= morphAmount;
    for (uint8_t voice = 0; voice < voices; voice++) osc[voice].setNoise(true);
  }
}

int16_t phaseModSelectedMorph(uint8_t voice, int16_t modulator) {
  switch (morphTable) {
    case MORPH_SQUARE:
      return osc[voice].phModMorph(modulator, phaseModIndexes[voice], squareTable, morphAmount);
    case MORPH_SAWTOOTH:
      return osc[voice].phModMorph(modulator, phaseModIndexes[voice], sawtoothTable, morphAmount);
    case MORPH_NOISE:
      return osc[voice].phModMorph(modulator, phaseModIndexes[voice], noiseTable, morphAmount);
    case MORPH_PULSE:
    default:
      return osc[voice].phModMorph(modulator, phaseModIndexes[voice], pulseTable, morphAmount);
  }
}

void updatePitch(float pitch) {
  basePitch = pitch;
  // Retune held pads with the root control. MIDI notes retain their pitches.
  for (uint8_t pad = 0; pad < 4; pad++) {
    if (padVoice[pad] >= 0) {
      setVoicePitch(padVoice[pad], basePitch + padIntervals[pad]);
    }
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
  subOscLevel = value >> 4; // rise with chorus depth from silence to 12%
  Serial.println("Chorus: " + String(value / 1023.0f));
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
  for (uint8_t voice = 0; voice < voices; voice++) {
    osc[voice].setTable(cosTable);
    lfoOsc[voice].setTable(cosTable);
    subOsc[voice].setTable(squareTable);
    filterVelocityScale[voice] = 1.0f;
    setVoicePitch(voice, basePitch);
    lfoOsc[voice].setFreq(vibratoRate);
    phaseModIndexes[voice] = maxPhaseModIndexes[voice];
  }
  updateWaveMorph(767); // start at the sawtooth endpoint
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
    phaseModIndexes[voice] = lfoEnv[voice].next() * MAX_16_INV
        * maxPhaseModIndexes[voice];
    int32_t tone = phaseModSelectedMorph(voice, lfoOsc[voice].next());
    tone = tone + ((subOsc[voice].next() * subOscLevel) >> 10);
    tone = lowPassFilter[voice].next(tone);
    const int32_t voiceOut = (tone * env[voice].next()) >> 16;
    mix = mix + ((voiceOut * ampCompensation) >> 10); // sum each voice
  }
  audioBlockWrite(mix, mix);
}
