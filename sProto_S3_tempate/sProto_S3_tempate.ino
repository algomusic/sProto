// sProto S3 Template - a monophonic sine wave with potentiometer controls for pitch, release, and volume
// For Arduino IDE:
// - Select "ESP32-S3 Dev Module" as the board
// - Select the appropriate Port
// - Set, USB CDC on Boot: Enabled
// - Set, PSRAM: OPI PSRAM

#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Gain.h"

// Instantiate audio objects
Osc osc;
Env env;

// Global variables
float basePitch = 72; // root pitch
const int padIntervals[4] = {0, 2, 4, 7}; // unison, maj2, maj3, P5
unsigned long controlTime, midiTime;
Gain outputGain;
int activeTrigger = -1; // -1=none, 0=button, 1-4=pads, 5=MIDI

// All live parameter changes, whether from a pot or MIDI, pass through setters/updaters.
void setVoicePitch(float pitch) {
  osc.setPitch(pitch);
}

void updateRelease(float releaseMs) {
  env.setRelease(releaseMs);
}

void updateVolume(int value) {
  outputGain.setLevel(value);
}

// If a pad is held, retain its interval above the new root.
void updatePitch(float pitch) {
  basePitch = pitch;
  int interval = (activeTrigger >= 1 && activeTrigger <= 4)
                   ? padIntervals[activeTrigger - 1] : 0;
  setVoicePitch(basePitch + interval);
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
void playNote(float pitch, int trigger);
void resumeHeldTouchPadOrRelease();

// Import associated files. midi.h defines midi/sendPadNoteOn; controls.h uses them, so MIDI comes first.
#include "midi.h"
#include "controls.h"

// --- setup ---
void setup() {
  Serial.begin(115200);
  delay(200);
  osc.sinGen(); // setup up sine wavetable
  updatePitch(basePitch);
  env.setAttack(20);
  env.setDecay(200);
  env.setSustain(0.7);
  env.setRelease(500);
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
void audioUpdate() {
  int32_t tone = osc.next();
  int32_t ampEnv = (tone * env.next()) >> 16;
  int32_t out = outputGain.next(ampEnv);
  audioBlockWrite(out, out); // same signal to L & R
}
