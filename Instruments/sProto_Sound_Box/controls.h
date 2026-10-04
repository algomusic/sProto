#ifndef SPROTO_SOUND_BOX_CONTROLS_H
#define SPROTO_SOUND_BOX_CONTROLS_H

#include "MultiControl.h"

const uint8_t padCount = 4;
const uint8_t dialCount = 3;
const uint8_t padPins[padCount] = {1, 2, 3, 4};
const uint8_t dialPins[dialCount] = {5, 6, 7};

MultiControl pads[padCount];
MultiControl dials[dialCount];
MultiControl button(8, 2);
bool previousPadState[padCount] = {false, false, false, false};

bool isPadHeld(uint8_t pad) {
  return pad < padCount && previousPadState[pad];
}

void syncDialBankValue(uint8_t dial, uint8_t bank, int value) {
  if (dial < dialCount && bank < 2) dials[dial].setBankValue(bank, value);
}

// One mapping shared by physical dials and MIDI CC input.
void updateDialControl(uint8_t dial, uint8_t bank, int value) {
  value = max(0, min(1023, value));
  if (bank == 1) {
    if (dial == 0) {
      bbdDelay.setTime(floatMap(
          value, 0, 1023, minBbdDelayMs, maxBbdDelayMs));
    } else if (dial == 1) {
      const float delayAmount = value / 1023.0f;
      bbdDelay.setLevel(delayAmount);
      bbdDelay.setFeedbackLevel(delayAmount);
      // Avoid BBD processing until the delay has first been used.
      if (value > 0) bbdDelayActive = true;
    } else if (dial == 2) {
      effects.setReverbMix(value / 1023.0f);
    }
  } else {
    if (dial == 0) {
      setWhistlePitch(floatMap(value, 0, 1023, 48.0f, 84.0f));
    } else if (dial == 1) {
      setKalimbaPitch(floatMap(value, 0, 1023, 36.0f, 72.0f));
    } else if (dial == 2 && value % 12 == 0) {
      setInstrumentMaxLevel(2, 1.0f);
      triggerSpring();
    }
  }
}

void initTouchControls() {
  for (int i = 0; i < padCount; i++) {
    pads[i].setPin(padPins[i]);
    pads[i].setControl(0); // capacitive touch
    pads[i].setTouchDebounceReads(4);
    pads[i].setTouchMinHold(30);
    pads[i].setRetriggerThreshold(0);
    pads[i].calibrateTouch(50);
  }

  for (int i = 0; i < dialCount; i++) {
    dials[i].setPin(dialPins[i]);
    dials[i].setControl(1); // potentiometer
    dials[i].initBanks(2);
  }

  // Seed both logical dial layers. MultiControl's soft takeover prevents
  // parameter jumps when the button changes banks.
  dials[0].setBankValue(0, 682); // whistle pitch: MIDI 72
  dials[1].setBankValue(0, 682); // kalimba pitch: MIDI 60
  dials[2].setBankValue(0, 0);   // spring strike position
  for (int i = 0; i < dialCount; i++) {
    dials[i].setBankValue(1, dials[i].getBankValue(0));
    dials[i].setBank(0);
  }
  dials[0].setBankValue(1, (int)(
      (defaultBbdDelayMs - minBbdDelayMs)
      / (maxBbdDelayMs - minBbdDelayMs) * 1023.0f + 0.5f));
  dials[1].setBankValue(1, 0); // delay level and feedback
  dials[2].setBankValue(1, (int)(effects.getReverbMix() * 1023.0f + 0.5f));
}

void readTouchControls() {
  for (int i = 0; i < padCount; i++) {
    bool touched = pads[i].isTouched();

    if (touched && !previousPadState[i]) {
      setInstrumentMaxLevel(i, 1.0f);
      if (i == 0) triggerWhistle();
      if (i == 1) triggerKalimba();
      if (i == 2) triggerSpring();
      if (i == 3) triggerGlockenspiel();
      Serial.println(i);
    }

    if (!touched && previousPadState[i] && !isMidiInstrumentHeld(i)) {
      if (i == 0) releaseWhistle();
      if (i == 1) releaseKalimba();
      if (i == 2) releaseSpring();
      if (i == 3) releaseGlockenspiel();
    }

    previousPadState[i] = touched;
  }

  // Pressure on the whistle pad retains the original breath/tone response.
  if (previousPadState[0]) {
    int level = pads[0].getValue();
    whistleToneEnv.setMaxLevel(pow(level / 1024.0f, 2.0f) * 0.3f + 0.7f);
    whistleToneCutoff = level / 1024.0f * 400.0f + 500.0f;
    adjustwhistleFilters();
  }
}

void readDialControls() {
  static uint8_t dialIndex = 0;
  const uint8_t requestedBank = button.isPressed() ? 1 : 0;
  if (dials[0].getBank() != requestedBank) {
    for (int i = 0; i < dialCount; i++) dials[i].setBank(requestedBank);
  }

  int value = dials[dialIndex].readPotChanged();
  if (value >= 0) {
    const uint8_t bank = dials[dialIndex].getBank();
    updateDialControl(dialIndex, bank, value);
    if (bank == 1) button.notifyHoldAction();
  }

  dialIndex = (dialIndex + 1) % dialCount;
}

void readControls() {
  readTouchControls();
  readDialControls();
}

#endif
