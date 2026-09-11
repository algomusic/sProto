// sProto Spring - a monophonic sine wave with potentiometer controls for pitch, release, and volume
// For Arduino IDE:
// - Select "ESP32-S3 Dev Module" as the board
// - Select the appropriate Port
// - Set, USB CDC on Boot: Enabled
// - Set, PSRAM: OPI PSRAM

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Gain.h"
#include "SVF.h"
#include "Del.h"

// Instantiate audio objects
Osc noise;
Env env;
SVF noiseFilter;
Del delays[3];

// Global variables
float basePitch = 72; // root pitch
const int padIntervals[4] = {0, 2, 4, 7}; // unison, maj2, maj3, P5
unsigned long controlTime, midiTime;
Gain outputGain;
int activeTrigger = -1; // -1=none, 0=button, 1-4=pads, 5=MIDI

// All live parameter changes, whether from a pot or MIDI, pass through setters/updaters.
void updateRelease(float releaseMs) {
  env.setRelease(releaseMs);
}

void updateVolume(int value) {
  outputGain.setLevel(value);
}

// Handle note start and stop
void triggerNote(int triggerSource) {
  env.start();
  activeTrigger = triggerSource;
}

void releaseNote() {
  env.startRelease();
  activeTrigger = -1;
}

// function declarations: A function with these signatures will be defined later.
void playNote(int trigger);
void resumeHeldTouchPadOrRelease();

// Import associated files. midi.h defines midi/sendPadNoteOn; controls.h uses them, so MIDI comes first.
#include "midi.h"
#include "controls.h"

// --- setup ---
void setup() {
  Serial.begin(115200);
  delay(200);
  noise.noiseGen(); // setup up sine wavetable
  noise.setNoise(true); // not a lopping oscillator
  noiseFilter.setFreq(3000);
  noiseFilter.setRes(0.6);
  env.setAttack(0);
  env.setDecay(8);
  env.setSustain(0);
  env.setRelease(8);
  delays[0].setTime(12);
  delays[1].setTime(14.18);
  delays[2].setTime(22.24);
  for (int i=0; i<3; i++) {
    delays[i].setFeedback(true); // recirculating
    delays[i].setFeedbackLevel(0.98);
    delays[i].setFiltered(2); // 0 - 4
    delays[i].setLevel(0.92);
  }
  seti2sPins(38,39,40,41);
  initTouchControls(); // Keep all pads untouched during startup calibration.
  initMidiInput();
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
int32_t delaySum;

void audioUpdate() {
  int32_t tone = noise.next();
  int32_t filteredTone = noiseFilter.nextBPF(tone);
  int32_t ampEnv = (filteredTone * env.next()) >> 16;
  int32_t feed = clip16(ampEnv + (delaySum >> 6)); // add some cross mod between delays
  delaySum = clip16(delays[0].next(feed) + delays[1].next(feed) + delays[2].next(feed));
  int32_t out = outputGain.next((ampEnv >> 1) + delaySum);
  audioBlockWrite(out, out); // same signal to L & R
}
