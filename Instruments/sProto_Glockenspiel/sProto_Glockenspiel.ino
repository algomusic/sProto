// sProto Glockenspiel - a monophonic FM tone with potentiometer controls for pitch, modIndex, and volume
// Andrew R. Brown 2026
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
Osc carrierOsc, modOsc;
Env carrierEnv, modEnv;

// Global variables
WaveTable sineTable; // shared wavetable for both oscillators
float modRatio = 1.73;
float modIndex = 0.7;
float basePitch = 84; // root pitch
const int padIntervals[4] = {0, 2, 4, 7}; // unison, maj2, maj3, P5
unsigned long controlTime, midiTime;
Gain outputGain;
int activeTrigger = -1; // -1=none, 0=button, 1-4=pads, 5=MIDI

// All live parameter changes, whether from a pot or MIDI, pass through these setters.
// Root-pitch changes from the pitch pot or MIDI CC pass through these setters.

// Tune the currently sounding voice without changing the root pitch.
void setVoicePitch(float pitch) {
  carrierOsc.setPitch(pitch);
  modOsc.setPitch(pitch * modRatio);
}

// If a pad is held, retain its interval above the new root.
void updatePitch(float pitch) {
  basePitch = pitch;
  int interval = (activeTrigger >= 1 && activeTrigger <= 4)
                   ? padIntervals[activeTrigger - 1] : 0;
  setVoicePitch(basePitch + interval);
}

void triggerNote(int triggerSource) {
  carrierEnv.start();
  modEnv.start();
  activeTrigger = triggerSource;
}

void releaseNote() {
  carrierEnv.startRelease();
  modEnv.startRelease();
  activeTrigger = -1;
}

void updateModIndex(float mi) {
  modIndex = mi;
  Serial.println("ModIndex = " + String(mi));
}

void updateVolume(int value) {
  outputGain.setLevel(value);
}

void playNote(float pitch, int trigger);
void resumeHeldTouchPadOrRelease();

// Import associated files. midi.h defines midi/sendPadNoteOn; controls.h uses them, so MIDI comes first.
#include "midi.h"
#include "controls.h"

// --- setup ---

void setup() {
  Serial.begin(115200);
  delay(200);
  sineTable.sinGen(); // allocate and fill wavetable
  carrierOsc.setTable(sineTable); // assign wave to osc1
  modOsc.setTable(sineTable); // assign wave to osc2
  updatePitch(basePitch);
  carrierEnv.setAttack(20);
  carrierEnv.setDecay(800);
  carrierEnv.setSustain(0.5);
  carrierEnv.setRelease(3000);
  modEnv.setAttack(0);
  modEnv.setDecay(200);
  modEnv.setSustain(0.07);
  modEnv.setRelease(1000);
  seti2sPins(38,39,40,41); // bck, ws, data_out, data_in // S3
  // seti2sPins(25, 26, 12, -1); // bck, ws, data_out, data_in // OG I2S
  initTouchControls(); // Keep all pads untouched during startup calibration.
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
  int32_t modTone = modOsc.next(); // generate modulator 
  int32_t modAmpEnv = (modTone * modEnv.next()) >> 16; // shape modulator
  int32_t tone = carrierOsc.phMod(modAmpEnv, modIndex); // generate modulated carrier
  int32_t toneAmpEnv = (tone * carrierEnv.next()) >> 16; // shape the carrier
  int32_t out = outputGain.next(toneAmpEnv); // volume
  audioBlockWrite(out, out); // send to DAC, same signal to L & R
}
