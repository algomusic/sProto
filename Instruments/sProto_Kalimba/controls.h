#include "MultiControl.h"

// hardware init
const uint8_t padPins[4] = {1, 2, 3, 4};
MultiControl pads[4];
MultiControl pots[3] = {MultiControl(5, 1), MultiControl(6, 1), MultiControl(7, 1)};
MultiControl btn(8, 2);

bool prevPadState[4] = {false, false, false, false};

void playVoice(int voice, float pitch, VoiceOwner owner, float level) {
  if (voice < 0 || voice >= voiceCount) return;
  // A Kalimba strike starts from a clean resonator state. Phys defers each
  // clear to the voice's owning audio core, so this remains safe in loop().
  for (int mode = 0; mode < modeCount; mode++) modes[voice][mode].resetPluck();
  setVoicePitch(voice, pitch);
  setKeytrackDampening(voice, pitch);
  envelopes[voice].setMaxLevel(level); // velocity
  envelopes[voice].start();
  voiceOwner[voice] = owner;
  voicePitch[voice] = (int8_t)pitch;
  if (owner != VOICE_PAD) voicePadIndex[voice] = -1;
}

void releaseVoice(int voice) {
  if (voice < 0 || voice >= voiceCount) return;
  envelopes[voice].startRelease();
  voiceOwner[voice] = VOICE_FREE;
  voicePitch[voice] = -1;
  voicePadIndex[voice] = -1;
}

void restorePadVoiceIfHeld(int voice) {
  if (voice < 0 || voice >= voiceCount) return;
  for (int pad = 0; pad < 4; pad++) {
    if (padVoice[pad] == voice && prevPadState[pad]) {
      float pitch = basePitch + padIntervals[pad];
      playVoice(voice, pitch, VOICE_PAD);
      voicePadIndex[voice] = pad;
      padLastPitch[pad] = (int8_t)pitch;
      return;
    }
  }
  releaseVoice(voice);
}

void initTouchControls() {
  for (int i = 0; i < 4; i++) {
    pads[i].setPin(padPins[i]);
    pads[i].setControl(0); // 0 = capacitive touch
    pads[i].setTouchDebounceReads(4);
    pads[i].setTouchMinHold(30);
    // Adjacent capacitive pads can produce false dip/rise retrigger patterns.
    pads[i].setRetriggerThreshold(0);
    pads[i].calibrateTouch(50);
    prevPadState[i] = false;
  }
}

// --- handlers ---
// Handlers live here so they can reach audio, midi and global variables.

void handleButton(int value) {
  if (value == 0) {
    playVoice(0, basePitch, VOICE_BUTTON);
    Serial.println("Button pressed");
  }
  else if (voiceOwner[0] == VOICE_BUTTON) {
    restorePadVoiceIfHeld(0);
    Serial.println("Button released");
  }
}

void readTouchControls() {
  bool padState[4];
  // Poll every pad on every control tick so debounce timing stays predictable.
  for (int i = 0; i < 4; i++) {
    padState[i] = pads[i].isTouched();
    if (padState[i] && !prevPadState[i]) {
      int pitch = (int)basePitch + padIntervals[i];
      int voice;
      if (padVoice[i] >= 0 && padLastPitch[i] == pitch) {
        // Repeating the same pad pitch retriggers its current resonator.
        voice = padVoice[i];
      } else {
        // A changed pad pitch shares the same round-robin allocator as MIDI.
        voice = nextRoundRobinVoice;
        nextRoundRobinVoice = (nextRoundRobinVoice + 1) % voiceCount;
      }

      // Only the newest pad assignment may release this voice.
      for (int otherPad = 0; otherPad < 4; otherPad++) {
        if (otherPad != i && padVoice[otherPad] == voice) padVoice[otherPad] = -1;
      }
      padVoice[i] = voice;
      padLastPitch[i] = pitch;
      playVoice(voice, pitch, VOICE_PAD);
      voicePadIndex[voice] = i;
      sendPadNoteOn(i, pitch);
    } else if (!padState[i] && prevPadState[i]) {
      int voice = padVoice[i];
      if (voice >= 0 && voiceOwner[voice] == VOICE_PAD &&
          voicePadIndex[voice] == i) {
        releaseVoice(voice);
      }
    }
  }

  for (int i = 0; i < 4; i++) prevPadState[i] = padState[i];
}

void readControls() {
  readTouchControls();

  static uint8_t index = 0;
  if (index < 3) {
    int v = pots[index].readPotChanged();
    if (v >= 0) {
      switch (index) {
        case 0: updatePitch(map(v, 0, 1023, 48, 84)); break;
        case 1: updateExciterDampening(floatMap(v, 0, 1023, 50.0f, 10000.0f)); break;
        case 2: updateVolume(map(v, 0, 1023, 0, 1024)); break;
      }
      midi.sendControlChange(0, CC_PITCH + index, map(v, 0, 1023, 0, 127));
    }
  } else {
    int v = btn.readChanged();
    if (v >= 0) handleButton(v);
  }
  index = (index + 1) % 4;
}
