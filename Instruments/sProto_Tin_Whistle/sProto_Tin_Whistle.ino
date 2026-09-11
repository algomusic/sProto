// sProto Slide Whistle - a monophonic recorder with pitch glide
// for Arduino IDE.
// - Select "ESP32-S3 Dev Module" as the board
// - USB CDC on Boot: Enabled
// - PSRAM: OPI PSRAM
#include "M16.h"
#include "Osc.h"
#include "SVF2.h"
#include "Env.h"
#include "Gain.h"

Osc osc, noise, slideVibratoLfo;
SVF2 toneLPFilter, toneHPFilter, noiseBPFilter;
Env toneAmpEnv, noiseAmpEnv;

float basePitch = 72; // root pitch
float targetPitch = 72;
const float slideVibratoIndex = 0.05f; // depth in semitones
const float slideVibratoRate = 5.0f;   // Hz
float slideVibratoPhaseIndex = 0.0f;
float baseToneCutoff = 900;
const int padIntervals[4] = {0, 2, 4, 7}; // unison, maj2, maj3, P5
unsigned long controlTime, midiTime, pitchTime;
// int noiseLevel = 100; // 0 - 1024
Gain outputGain;
int activeTrigger = -1; // -1=none, 0=button, 1-4=pads, 5=MIDI

void adjustToneCutoff() {
  float keyTracking = 0.75;
  float cutoffRatio = 1.0;
  if (osc.getPitch() > 60) cutoffRatio = osc.getFreq() / 261.0f * keyTracking;
  toneLPFilter.setFreq(baseToneCutoff * cutoffRatio);
}

void adjustNoiseCutoff() {
  noiseBPFilter.setFreq(osc.getFreq());
}

void setPlayingPitch(float pitch) {
  osc.setPitch(pitch);
  const float frequencyDeviation = osc.getFreq()
      * (powf(2.0f, slideVibratoIndex / 12.0f) - 1.0f);
  slideVibratoPhaseIndex = frequencyDeviation
      / (2.0f * PI * slideVibratoRate);
  adjustToneCutoff();
  adjustNoiseCutoff();
}

// All live parameter changes, whether from a pot or MIDI, pass through these setters.
void updatePitch(float pitch) {
  basePitch = pitch;
  int interval = (activeTrigger >= 1 && activeTrigger <= 4)
                   ? padIntervals[activeTrigger - 1] : 0;
  targetPitch = basePitch + interval;
  // setPlayingPitch(basePitch + interval);
}

void updateRelease(float releaseMs) {
  toneAmpEnv.setRelease(releaseMs);
  noiseAmpEnv.setRelease(releaseMs / 2);
}

void updateVolume(int value) {
  outputGain.setLevel(value);
}

void startEnvelopes() {
  toneAmpEnv.start();
  noiseAmpEnv.start();
}

void releaseEnvelopes() {
  toneAmpEnv.startRelease();
  noiseAmpEnv.startRelease();
}

void resumeHeldTouchPadOrRelease();

// midi.h defines midi/sendPadNoteOn; controls.h uses them, so MIDI comes first.
#include "midi.h"
#include "controls.h"

// --- setup ---

void setup() {
  Serial.begin(115200);
  delay(200);
  osc.sawGen(); // setup up sawtooth wavetable
  // Preserve the requested low-rate PM depth across the full pitch range.
  osc.disableAntiAlias();
  slideVibratoLfo.sinGen();
  slideVibratoLfo.setFreq(slideVibratoRate);
  updatePitch(basePitch);
  noise.pinkNoiseGen();
  noise.setNoise(true);
  toneLPFilter.setFreq(900);
  toneHPFilter.setFreq(200);
  noiseBPFilter.setFreq(mtof(basePitch));
  toneAmpEnv.setAttack(300);
  toneAmpEnv.setDecay(500);
  toneAmpEnv.setSustain(0.9);
  toneAmpEnv.setRelease(300);
  noiseAmpEnv.setAttack(50);
  noiseAmpEnv.setDecay(500);
  noiseAmpEnv.setSustain(0.5);
  noiseAmpEnv.setRelease(300);
  noiseAmpEnv.setMaxLevel(0.25);
  setPlayingPitch(basePitch - 0.5f);
  seti2sPins(38, 39, 40, 41);
  initTouchControls(); // Keep all pads untouched during startup calibration.
  setIsDualCore(false); // This sketch is one serial, shared-state DSP graph.
  audioStart();
}

// --- loop ---

void loop() {
  unsigned long msNow = millis();

  // adjust pitch
  if ((unsigned long)(msNow - pitchTime) >= 4) {
    pitchTime = msNow;
    if (toneAmpEnv.getValue() > 0) {
      float p = slew(osc.getPitch(), targetPitch, 0.5);
      setPlayingPitch(p);
    } else setPlayingPitch(basePitch - 0.5); // force slide up into first note
  }

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
  int32_t tone = osc.phMod(slideVibratoLfo.next(), slideVibratoPhaseIndex);
  tone = (tone * toneAmpEnv.next()) >> 16; // apply amp env
  // int32_t tone = (toneHPFilter.nextHPF(toneLPFilter.nextLPF(osc.next())) * toneAmpEnv.next()) >> 16; //toneHPFilter.nextHPF(toneLPFilter.nextLPF(osc.next()));
  int32_t wind = noise.next();
  wind = noiseBPFilter.nextBPF(wind); // filter
  wind = (wind * noiseAmpEnv.next()) >> 16; // amp env
  // int32_t wind = (noiseBPFilter.nextBPF(noise.next()) * noiseAmpEnv.next()) >> 16; 
  int32_t out = clip16(tone + wind); // mix
  out = toneHPFilter.nextHPF(toneLPFilter.nextLPF(out)); // filter
  out = outputGain.next(out); // level
  // audioBlockWrite(wind, wind);
  // int32_t sample = (clip16(tone + wind) * toneAmpEnv.next()) >> 16;
  // int32_t out = outputGain.next(clip16(tone + wind));
  audioBlockWrite(out, out);
}
