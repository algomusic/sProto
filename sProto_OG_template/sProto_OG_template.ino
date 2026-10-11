// sProto template - a simple monophonic sine wave synth
// for Arduino IDE.
// - Select "ESP32 Dev Module" as the board
// - Choose 460800 as the upload speed
#include <WiFi.h> // just to turn it off
#include "M16.h"
#include "Osc.h"
#include "Env.h"
#include "Gain.h"

Osc osc;
Env env;
Gain outputGain(1023);

volatile uint32_t envVal = 0;     // shared between loop() and audioUpdate()
int basePitch = 60;               // set from pot1, applied on next trigger
const int padIntervals[4] = {0, 2, 4, 7}; // unison, maj2, maj3, P5
unsigned long controlTime, envTime, touchTime;

// midi.h and controls.h use osc, env, activeTrigger, potPitch/Pan/Vol — must come after those globals
#include "controls.h"
#include "midi.h"

// --- setup ---

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.mode(WIFI_OFF); // release resources
  initControls();
  osc.sinGen();
  env.setAttack(10);
  env.setDecay(100);
  env.setSustain(0.7);
  env.setRelease(500);
  useInternalDAC();
  audioStart();
}

// --- loop ---

void loop() {
  unsigned long msNow = millis();

  readMidi();
  updateMidiOut(msNow);

  // check touch pads and button
  if ((unsigned long)(msNow - touchTime) >= 4) {
    touchTime = msNow;
    handleButton();
    readTouchPads();
  }

  // update envelope
  if ((unsigned long)(msNow - envTime) >= 1) {
    envTime = msNow;
    envVal = env.next();
  }

  // check pots
  if ((unsigned long)(msNow - controlTime) >= 3) {
    controlTime = msNow;
    readPots();
  }
}

// --- audio ---

/* The audioUpdate function is required in all M16 programs.
   Always finish with audioBlockWrite() */
void audioUpdate() {
  int32_t ev = (int32_t)envVal; 
  int32_t sample = ((int32_t)osc.next() * ev) >> 16;
  int32_t out = outputGain.next(sample);
  audioBlockWrite(out, out);
}
