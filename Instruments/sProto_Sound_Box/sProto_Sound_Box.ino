// sProto Sound Box
// Andrew R. Brown 2026
//
// Four independent instruments on one sProto board:
//   Pad 1 / Dial 1: Slide Whistle trigger / pitch
//   Pad 2 / Dial 2: Kalimba trigger / pitch
//   Pad 3 / Dial 3: Spring trigger / repeated strikes
//   Pad 4: Glockenspiel trigger
//   Hold the sProto button to shift the dials from bank 0 to bank 1 for effects.
//   Dial 1: Delay Time
//   Dial 2: Delay Level
//   Dial 3: Reverb Level
//
// Arduino IDE settings:
//   Board: ESP32-S3 Dev Module
//   USB CDC on Boot: Enabled
//   PSRAM: OPI PSRAM

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "SVF2.h"
#include "SVF.h"
#include "Phys.h"
#include "Del.h"
#include "BBD.h"
#include "FX.h"
#if IS_ESP32()
  #include <esp_heap_caps.h>
  #include <esp_system.h>
#endif

// -------------------------------------------------------------------------
// Shared effects and timing

BBD bbdDelay;
volatile bool bbdDelayActive = false;
const float minBbdDelayMs = 4096.0f / 44100.0f * 1000.0f / 3.0f;
const float maxBbdDelayMs = 2000.0f;
const float defaultBbdDelayMs = 200.0f;

unsigned long controlTime = 0;
unsigned long midiTime = 0;
unsigned long slidePitchTime = 0;
unsigned long diagnosticTime = 0;
// scale
const int pentatonic[] = {0, 2, 4, 7, 9};

int randomPentatonicOffset() {
  return pentatonic[random(5)];
}

bool isCMajorPitch(int midiPitch) {
  int pitchClass = (midiPitch % 12 + 12) % 12;
  return pitchClass == 0 || pitchClass == 2 || pitchClass == 4 ||
         pitchClass == 5 || pitchClass == 7 || pitchClass == 9 ||
         pitchClass == 11;
}

float quantizeToCMajor(float pitch) {
  int firstCandidate = (int)floorf(pitch) - 2;
  int lastCandidate = (int)ceilf(pitch) + 2;
  int closestPitch = (int)roundf(pitch);
  float closestDistance = 1000.0f;

  for (int candidate = firstCandidate; candidate <= lastCandidate; candidate++) {
    if (!isCMajorPitch(candidate)) continue;
    float distance = fabsf(pitch - candidate);
    if (distance < closestDistance) {
      closestDistance = distance;
      closestPitch = candidate;
    }
  }
  return closestPitch;
}

// -------------------------------------------------------------------------
// Slide Whistle

Osc slideOsc;
Osc slideNoise;
Osc slideVibratoLfo;
Env slideToneEnv;
Env slideNoiseEnv;
SVF2 slideLowPass;
SVF2 slideHighPass;
SVF2 slideNoiseBandPass;
FX effects;

float slidePitch = 72.0f;
float slideTargetPitch = 72.0f;
float slideToneCutoff = 900.0f;
const float slideVibratoRate = 5.0f;  // Hz
const float slideVibratoIndex = 0.05f; // musical depth in semitones
float slideVibratoPhaseIndex = 0.0f;  // cached phMod depth in phase cycles

void adjustSlideFilters() {
  float cutoffRatio = 1.0f;
  if (slideOsc.getPitch() > 60.0f) {
    cutoffRatio = slideOsc.getFreq() / 261.0f * 0.75f;
  }
  slideLowPass.setFreq(slideToneCutoff * cutoffRatio);
  slideNoiseBandPass.setFreq(slideOsc.getFreq());
}

void setSlidePlayingPitch(float pitch) {
  slideOsc.setPitch(pitch);
  // phMod depth is phase excursion, so the audible frequency deviation is
  // 2*pi*LFO-rate*phase-index. Convert the requested semitone interval into
  // the phase index required at the current carrier frequency.
  float frequencyDeviation = slideOsc.getFreq() *
      (powf(2.0f, slideVibratoIndex / 12.0f) - 1.0f);
  slideVibratoPhaseIndex = frequencyDeviation /
      (2.0f * PI * slideVibratoRate);
  adjustSlideFilters();
}

void setSlidePitch(float pitch) {
  slidePitch = pitch;
  slideTargetPitch = pitch;
}

void triggerSlideWhistle() {
  // Dial movement is continuous while a note sounds. At the next attack,
  // capture an in-key base and quantize the randomized note to C major too.
  slidePitch = quantizeToCMajor(slidePitch);
  slideTargetPitch = quantizeToCMajor(
      slidePitch + randomPentatonicOffset());
  slideToneEnv.start();
  slideNoiseEnv.start();
}

void releaseSlideWhistle() {
  slideToneEnv.startRelease();
  slideNoiseEnv.startRelease();
}

// -------------------------------------------------------------------------
// Kalimba

Osc kalimbaNoise;
Env kalimbaEnv;
SVF2 kalimbaExciterFilter;
Phys kalimbaModes[3];

float kalimbaPitch = 55.0f;
const float kalimbaModeRatio1 = 6.72f;
const float kalimbaModeRatio2 = 20.16f;
const float kalimbaFeedback1 = 0.995f;
const float kalimbaFeedback2 = 0.98f;
const float kalimbaFeedback3 = 0.97f;

void setKalimbaPitch(float pitch) {
  kalimbaPitch = pitch;
  kalimbaNoise.setPitch(pitch);
}

void triggerKalimba() {
  // The current note can glide freely with the dial; only a new attack snaps
  // its stored base and randomized sounding pitch to C major.
  kalimbaPitch = quantizeToCMajor(kalimbaPitch);
  kalimbaNoise.setPitch(quantizeToCMajor(
      kalimbaPitch + randomPentatonicOffset()));
  kalimbaEnv.start();
}

void releaseKalimba() {
  kalimbaEnv.startRelease();
}

// -------------------------------------------------------------------------
// Spring

Osc springNoise;
Env springEnv;
SVF springNoiseFilter;
Del springDelays[3];
int32_t springDelaySum = 0;

void triggerSpring() {
  springNoiseFilter.setFreq(2000 + audioRand(2000));
  springEnv.start();
}

void releaseSpring() {
  springEnv.startRelease();
}

// -------------------------------------------------------------------------
// Glockenspiel

Osc glockCarrier;
Osc glockModulator;
Env glockCarrierEnv;
Env glockModulatorEnv;

WaveTable glockSineTable;
float glockPitch = 84.0f;
const float glockModRatio = 1.73;
const float glockModIndex = 0.7;

void setGlockPitch(float pitch) {
  glockPitch = pitch;
  glockCarrier.setPitch(pitch);
  glockModulator.setPitch(pitch * glockModRatio);
}

void triggerGlockenspiel() {
  float notePitch = glockPitch + randomPentatonicOffset();
  glockCarrier.setPitch(notePitch);
  glockModulator.setPitch(notePitch * glockModRatio);
  glockCarrierEnv.start();
  glockModulatorEnv.start();
}

void releaseGlockenspiel() {
  glockCarrierEnv.startRelease();
  glockModulatorEnv.startRelease();
}

// Set the amplitude-driving envelope level for one of the four instruments.
// The whistle noise retains its original 25% balance, and the Glock modulator
// is deliberately excluded because changing it alters timbre rather than volume.
void setInstrumentMaxLevel(uint8_t instrument, float level) {
  level = max(0.0f, min(1.0f, level));
  if (instrument == 0) {
    slideToneEnv.setMaxLevel(level);
    slideNoiseEnv.setMaxLevel(level * 0.25f);
  } else if (instrument == 1) {
    kalimbaEnv.setMaxLevel(level);
  } else if (instrument == 2) {
    springEnv.setMaxLevel(level);
  } else if (instrument == 3) {
    glockCarrierEnv.setMaxLevel(level);
  }
}

// -------------------------------------------------------------------------
void initSlideWhistle() {
  slideOsc.sawGen();
  slideOsc.disableAntiAlias(); // not necessary for LFO
  slideNoise.pinkNoiseGen();
  slideNoise.setNoise(true);
  slideVibratoLfo.sinGen();
  slideVibratoLfo.setFreq(slideVibratoRate);

  setSlidePlayingPitch(slidePitch - 0.5f);
  slideLowPass.setFreq(900.0f);
  slideHighPass.setFreq(200.0f);
  slideNoiseBandPass.setFreq(mtof(slidePitch));

  slideToneEnv.setAttack(300);
  slideToneEnv.setDecay(500);
  slideToneEnv.setSustain(0.9f);
  slideToneEnv.setRelease(300);

  slideNoiseEnv.setAttack(50);
  slideNoiseEnv.setDecay(500);
  slideNoiseEnv.setSustain(0.5f);
  slideNoiseEnv.setRelease(150);
  slideNoiseEnv.setMaxLevel(0.25f);
}

void initKalimba() {
  kalimbaNoise.noiseGen();
  kalimbaNoise.setNoise(true);
  setKalimbaPitch(kalimbaPitch);

  kalimbaEnv.setAttack(0);
  kalimbaEnv.setDecay(50);
  kalimbaEnv.setSustain(0);
  kalimbaEnv.setRelease(50);
  kalimbaExciterFilter.setFreq(280.0f);
}

void initSpring() {
  springNoise.noiseGen();
  springNoise.setNoise(true);
  springNoiseFilter.setFreq(3000);
  springNoiseFilter.setRes(0.6);
  springEnv.setAttack(0);
  springEnv.setDecay(8);
  springEnv.setSustain(0);
  springEnv.setRelease(8);

  const float springDelayTimes[3] = {12.0f, 14.18f, 22.24f};
  for (int i = 0; i < 3; i++) {
    springDelays[i].setTime(springDelayTimes[i]);
    springDelays[i].setFeedback(true);
    springDelays[i].setFeedbackLevel(0.98f);
    springDelays[i].setFiltered(2);
    springDelays[i].setLevel(0.92f);
  }
}

void initGlockenspiel() {
  glockSineTable.sinGen();
  glockCarrier.setTable(glockSineTable);
  glockModulator.setTable(glockSineTable);
  setGlockPitch(glockPitch);

  glockCarrierEnv.setAttack(20);
  glockCarrierEnv.setDecay(800);
  glockCarrierEnv.setSustain(0.5f);
  glockCarrierEnv.setRelease(3000);

  glockModulatorEnv.setAttack(0);
  glockModulatorEnv.setDecay(200);
  glockModulatorEnv.setSustain(0.6f);
  glockModulatorEnv.setRelease(2000);
}

// Import associated files. MIDI comes first so controls can use it when MIDI
// mappings are added without changing the sketch structure.
void updateDialControl(uint8_t dial, uint8_t bank, int value);
void syncDialBankValue(uint8_t dial, uint8_t bank, int value);
bool isPadHeld(uint8_t pad);
#include "midi.h"
#include "controls.h"

void setup() {
  Serial.begin(115200);
  delay(200);

  #if IS_ESP32()
    Serial.print("ESP reset reason: ");
    Serial.println((int)esp_reset_reason());
  #endif

  initSlideWhistle();
  initKalimba();
  initSpring();
  initGlockenspiel();
  initTouchControls(); // Keep pads 1-4 untouched during calibration.

  bbdDelay.setTime(defaultBbdDelayMs);
  bbdDelay.setLevel(0.0f);
  bbdDelay.setFeedbackLevel(0.0f);
  bbdDelay.setFiltered(2);

  // Allocate reverb before audio tasks start, then process it once after M16
  // combines the two independently rendered instrument partitions.
  effects.initReverbSafe();
  setAudioPostProcessCallback(audioPostProcess);
  // seti2sPins(38, 39, 40, -1);
  setIsDualCore(true);
  initMidiInput();
  audioStart();
}

// -------------------------------------------------------------------------
// Control processing

void loop() {
  unsigned long now = millis();

  if ((unsigned long)(now - controlTime) >= 1) {
    controlTime = now;
    readControls();
  }

  if ((unsigned long)(now - midiTime) >= 5) {
    midiTime = now;
    readMidi();
  }

  if ((unsigned long)(now - slidePitchTime) >= 4) {
    slidePitchTime = now;
    if (slideToneEnv.getValue() > 0) {
      setSlidePlayingPitch(slew(slideOsc.getPitch(), slideTargetPitch, 0.5f));
    } else {
      // Begin each whistle note with its characteristic short upward slide.
      setSlidePlayingPitch(slidePitch - 0.5f);
    }
  }
}

// -------------------------------------------------------------------------
// Audio processing

int32_t renderSlideWhistle() {
  if (slideToneEnv.getEnvState() == 0 && slideNoiseEnv.getEnvState() == 0) {
    return 0;
  }
  int32_t tone = (slideOsc.phMod(slideVibratoLfo.next(), slideVibratoPhaseIndex) * slideToneEnv.next()) >> 16;
  int32_t wind = slideNoiseBandPass.nextBPF(slideNoise.next());
  wind = (wind * slideNoiseEnv.next()) >> 16;
  return slideHighPass.nextHPF(slideLowPass.nextLPF(clip16(tone + wind)));
}

int32_t renderKalimba() {
  int32_t source = kalimbaNoise.next();
  int32_t dampened = kalimbaExciterFilter.nextLPF(source);
  int32_t exciter = (dampened * kalimbaEnv.next()) >> 16;
  float frequency = kalimbaNoise.getFreq();
  int32_t mode1 = kalimbaModes[0].pluck(exciter, frequency, kalimbaFeedback1);
  int32_t mode2 = kalimbaModes[1].pluck(
      exciter, frequency * kalimbaModeRatio1, kalimbaFeedback2);
  int32_t mode3 = kalimbaModes[2].pluck(
      exciter, frequency * kalimbaModeRatio2, kalimbaFeedback3);
  return clip16(exciter + mode1 + (mode2 >> 1) + (mode3 >> 2));
}

int32_t renderSpring() {
  int32_t tone = springNoise.next();
  int32_t filteredTone = springNoiseFilter.nextBPF(tone);
  int32_t exciter = (filteredTone * springEnv.next()) >> 16;
  int32_t feed = clip16(exciter + (springDelaySum >> 6));
  springDelaySum = clip16(springDelays[0].next(feed) + springDelays[1].next(feed) + springDelays[2].next(feed));
  return (exciter >> 1) + springDelaySum;
}

int32_t renderGlockenspiel() {
  if (glockCarrierEnv.getEnvState() == 0 &&
      glockModulatorEnv.getEnvState() == 0) {
    return 0;
  }
  int32_t modulator = glockModulator.next();
  modulator = (modulator * glockModulatorEnv.next()) >> 16;
  int32_t carrier = glockCarrier.phMod(modulator, glockModIndex);
  return (carrier * glockCarrierEnv.next()) >> 16;
}

// Shared effects run once on Core 0 after M16 combines both core partitions.
void audioPostProcess(int32_t &left, int32_t &right) {
  if (bbdDelayActive) {
    const int32_t monoInput = (left + right) >> 1;
    const int32_t delayInput = clip16((monoInput * 3) >> 1);
    const int32_t delayed = bbdDelay.next(delayInput);
    left = clip16(left + delayed);
    right = clip16(right + delayed);
  }
  effects.compressionStereo(left * 1.5f, right * 1.5f, left, right);
  int32_t fxL, fxR;
  effects.reverbStereoInterp(left, right, fxL, fxR);
  left = fxL;
  right = fxR;
}

void audioUpdate() {
  int32_t mix;
  if (audioPartitionOffset() == 0) {
    // Core 0 finalises the mix and runs BBD/reverb, so keep its instruments light.
    mix = (renderSlideWhistle() >> 1) + (renderGlockenspiel() >> 2);
  } else {
    // Balance the physical-model voices against Core 0's shared effects.
    mix = renderKalimba() + renderSpring();
  }
  audioBlockWrite(mix, mix);
}
