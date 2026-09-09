#pragma once
// ReBirth338 — dual TB-303-style acid bass engines + a TR-808-style drum kit,
// procedurally synthesized (no samples). Runs as an always-on subsystem mixed
// into the master bus from synthESP32.ino's write_buffer(), independent of
// the existing 16 sample/synth tracks and their ROTvalue/rotary-encoder page
// framework. Driven by its own 16-step patterns (seeded with a demo groove
// so it's audible with no MIDI gear attached) and by MIDI note/CC messages
// arriving from the USB MIDI host (USB_tools.ino), which funnel into
// rebirth338_noteOn/noteOff/controlChange().
//
// MIDI channel map (1-indexed, matching parse_midi_message()'s convention):
//   channel 3  -> Acid 303 A
//   channel 4  -> Acid 303 B
//   channel 10 -> 808 kit (General-MIDI-style drum note numbers)
// CC map on the acid channels: 74 cutoff, 71 resonance, 73 env mod, 72 decay,
// 91 accent amount (all 0-127, matching common MIDI controller conventions).

#include <Arduino.h>
#include <math.h>

static inline float r338_noteToFreq(uint8_t note) {
  return 440.0f * powf(2.0f, ((float)note - 69.0f) / 12.0f);
}

// A sine table and incremental envelopes, because calling sinf()/expf() once
// per voice per sample at 44.1kHz is what made the audio stutter once the 909
// doubled the voice count. Both are exact enough here and vastly cheaper.
#define R338_SINE_BITS 10
#define R338_SINE_SIZE (1 << R338_SINE_BITS)
static float r338SineLut[R338_SINE_SIZE];
static bool r338LutReady = false;

static void r338_init_lut() {
  if (r338LutReady) return;
  for (int i = 0; i < R338_SINE_SIZE; i++) {
    r338SineLut[i] = sinf((float)i * 6.2831853f / (float)R338_SINE_SIZE);
  }
  r338LutReady = true;
}

// phase is 0..1
static inline float r338_sin(float phase) {
  int idx = (int)(phase * (float)R338_SINE_SIZE) & (R338_SINE_SIZE - 1);
  return r338SineLut[idx];
}

// exp(-rate * age) accumulated one sample at a time: pitchEnv *= coef.
#define R338_KICK808_SWEEP 0.99780242f
#define R338_TOM808_LO_SWEEP 0.90483742f
#define R338_TOM808_HI_SWEEP 0.93239382f
#define R338_KICK909_SWEEP 0.99252805f
#define R338_TOM909_SWEEP 0.95122942f

// ------------------------------------------------------------- Acid303Voice
// Monophonic TB-303-style voice: saw/square oscillator -> resonant lowpass
// filter swept by its own decaying envelope, plus accent and slide (glide),
// mirroring the classic acid-bassline behaviour.
class Acid303Voice {
public:
  enum Wave : uint8_t { WAVE_SAW = 0, WAVE_SQUARE = 1 };

  void begin() {
    filter.setResonance(180);
    filter.setCutoffFreq(120);
  }

  void setWave(Wave w) { wave = w; }
  void setCutoff(uint8_t v) { cutoffBase = v; }             // 0-255
  void setResonance(uint8_t v) { resonance = v; }           // 0-255
  void setEnvMod(uint8_t v) { envMod = v; }                 // 0-127
  void setDecay(uint8_t v) { decay = v; }                   // 0-127 (higher = longer)
  void setAccentAmount(uint8_t v) { accentAmount = v; }     // 0-127
  void setTune(int8_t semis) { tune = semis; }              // -24..+24

  // Read-back for the on-screen panels.
  Wave getWave() const { return wave; }
  uint8_t getCutoff() const { return cutoffBase; }
  uint8_t getResonance() const { return resonance; }
  uint8_t getEnvMod() const { return envMod; }
  uint8_t getDecay() const { return decay; }
  uint8_t getAccentAmount() const { return accentAmount; }
  int8_t getTune() const { return tune; }

  void noteOn(uint8_t note, bool accent, bool slide) {
    int pitch = (int)note + tune;
    if (pitch < 0) pitch = 0;
    if (pitch > 127) pitch = 127;
    float target = r338_noteToFreq((uint8_t)pitch);
    if (slide && gateOpen) {
      slideFrom = currentFreq;
      slideSamplesLeft = SLIDE_SAMPLES;
    } else {
      slideSamplesLeft = 0;
      currentFreq = target;
    }
    slideTo = target;
    accenting = accent;
    ampEnv = 1.0f;
    filtEnv = 1.0f;
    gateOpen = true;
  }

  void noteOff() { gateOpen = false; }

  int16_t render() {
    if (slideSamplesLeft > 0) {
      float t = 1.0f - ((float)slideSamplesLeft / (float)SLIDE_SAMPLES);
      currentFreq = slideFrom + (slideTo - slideFrom) * t;
      slideSamplesLeft--;
      if (slideSamplesLeft == 0) currentFreq = slideTo;
    }

    phase += currentFreq / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    int16_t osc = (wave == WAVE_SQUARE) ? ((phase < 0.5f) ? 8000 : (int16_t)-8000)
                                         : (int16_t)((phase * 2.0f - 1.0f) * 8000.0f);

    float ampDecayRate = 0.00025f + (float)(127 - decay) * 0.00012f;
    ampEnv -= ampEnv * ampDecayRate;
    if (ampEnv < 0.0f) ampEnv = 0.0f;

    float filtDecayRate = 0.0006f + (float)(127 - decay) * 0.0003f;
    filtEnv -= filtEnv * filtDecayRate;
    if (filtEnv < 0.0f) filtEnv = 0.0f;

    float accentBoost = accenting ? ((float)accentAmount / 127.0f) : 0.0f;
    float envAmount = ((float)envMod / 127.0f) + accentBoost * 0.5f;
    float cutoffMod = (float)cutoffBase + filtEnv * envAmount * 255.0f;
    cutoffMod = constrain(cutoffMod, 0.0f, 255.0f);

    filter.setCutoffFreq((uint8_t)cutoffMod);
    filter.setResonance((uint8_t)constrain((int)resonance + (accenting ? 40 : 0), 0, 255));

    float amp = ampEnv * (accenting ? (0.7f + 0.3f * accentBoost) : 0.55f);
    int32_t filtered = filter.next((int16_t)((float)osc * amp));
    return (int16_t)constrain((long)filtered, -32000L, 32000L);
  }

private:
  static const uint32_t SLIDE_SAMPLES = (SAMPLE_RATE * 60) / 1000;  // ~60ms glide

  LowPassFilter filter;
  Wave wave = WAVE_SAW;
  float phase = 0.0f;
  float currentFreq = 110.0f, slideFrom = 110.0f, slideTo = 110.0f;
  uint32_t slideSamplesLeft = 0;
  float ampEnv = 0.0f, filtEnv = 0.0f;
  bool gateOpen = false, accenting = false;
  uint8_t cutoffBase = 90, resonance = 160, envMod = 80, decay = 40, accentAmount = 90;
  int8_t tune = 0;
};

// ------------------------------------------------------------- Drum808Voice
// Procedural analog-style drum synthesis (no samples), one instance per
// classic TR-808 voice.
class Drum808Voice {
public:
  enum Type : uint8_t { KICK, SNARE, CLAP, CLOSED_HAT, OPEN_HAT, LOW_TOM, HI_TOM, COWBELL, RIMSHOT, TYPE_COUNT };

  void begin(Type t) {
    type = t;
    lfsr = 0xACE1u ^ (uint16_t)((uint16_t)t * 977u + 1u);
  }

  void trigger(uint8_t velocity) {
    active = true;
    vel = (float)velocity / 127.0f;
    phase = phase2 = 0.0f;
    age = 0;
    ampEnv = 1.0f;
    pitchEnv = 1.0f;
    pulseIndex = 0;
    pulseAge = 0;
  }

  int16_t render() {
    if (!active) return 0;
    float s;
    switch (type) {
      case KICK:       s = renderKick(); break;
      case LOW_TOM:    s = renderTomLike(120.0f, 55.0f, R338_TOM808_LO_SWEEP, 0.0016f); break;
      case HI_TOM:     s = renderTomLike(240.0f, 140.0f, R338_TOM808_HI_SWEEP, 0.0022f); break;
      case SNARE:      s = renderSnare(); break;
      case RIMSHOT:    s = renderRimshot(); break;
      case CLAP:       s = renderClap(); break;
      case CLOSED_HAT: s = renderHat(0.0022f); break;
      case OPEN_HAT:   s = renderHat(0.00035f); break;
      case COWBELL:    s = renderCowbell(); break;
      default:         s = 0.0f; break;
    }
    age++;
    if (ampEnv <= 0.0008f) active = false;
    return (int16_t)constrain((long)s, -32000L, 32000L);
  }

private:
  uint16_t nextNoise() {
    lfsr ^= lfsr << 7;
    lfsr ^= lfsr >> 9;
    lfsr ^= lfsr << 8;
    return lfsr;
  }

  float renderKick() {
    pitchEnv *= R338_KICK808_SWEEP;  // 205Hz -> 55Hz sweep
    float pitchHz = 55.0f + 150.0f * pitchEnv;
    phase += pitchHz / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    float tone = r338_sin(phase);
    float click = (age < 6) ? ((float)(int16_t)nextNoise() / 32768.0f) * 0.5f : 0.0f;
    ampEnv -= ampEnv * 0.00135f;
    return (tone + click) * ampEnv * vel * 13000.0f;
  }

  float renderTomLike(float startHz, float endHz, float sweepCoef, float decayRate) {
    pitchEnv *= sweepCoef;
    float pitchHz = endHz + (startHz - endHz) * pitchEnv;
    phase += pitchHz / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    float tone = r338_sin(phase);
    ampEnv -= ampEnv * decayRate;
    return tone * ampEnv * vel * 11500.0f;
  }

  float renderSnare() {
    phase += 180.0f / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    phase2 += 330.0f / (float)SAMPLE_RATE;
    if (phase2 >= 1.0f) phase2 -= 1.0f;
    float tone = r338_sin(phase) * 0.5f + r338_sin(phase2) * 0.5f;
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    ampEnv -= ampEnv * 0.0032f;
    float noiseAmp = ampEnv * ampEnv;  // noise tail dies faster than the tone
    return (tone * ampEnv * 0.5f + noise * noiseAmp * 0.8f) * vel * 13000.0f;
  }

  float renderRimshot() {
    phase += 420.0f / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    float tone = (phase < 0.5f) ? 1.0f : -1.0f;
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    ampEnv -= ampEnv * 0.02f;
    return (tone * 0.6f + noise * 0.4f) * ampEnv * vel * 10500.0f;
  }

  float renderClap() {
    const uint16_t pulseGap = (uint16_t)(SAMPLE_RATE * 0.011f);
    if (pulseIndex < 3 && pulseAge > pulseGap) {
      pulseIndex++;
      pulseAge = 0;
      ampEnv = 1.0f;
    }
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    float rate = (pulseIndex < 3) ? 0.02f : 0.0028f;
    ampEnv -= ampEnv * rate;
    pulseAge++;
    return noise * ampEnv * vel * 11500.0f;
  }

  float renderHat(float decayRate) {
    // Six square oscillators at inharmonic ratios, summed and high-passed —
    // the same recipe the real 808 used for its metallic cymbal/hat sound.
    static const float ratios[6] = { 1.0f, 1.342f, 1.640f, 1.884f, 2.023f, 2.253f };
    const float fundamental = 205.0f;
    float mix = 0.0f;
    for (uint8_t i = 0; i < 6; i++) {
      hatPhase[i] += (fundamental * ratios[i]) / (float)SAMPLE_RATE;
      if (hatPhase[i] >= 1.0f) hatPhase[i] -= 1.0f;
      mix += (hatPhase[i] < 0.5f) ? 1.0f : -1.0f;
    }
    mix /= 6.0f;
    hpState += (mix - hpState) * 0.35f;  // crude one-pole high-pass
    float hp = mix - hpState;
    ampEnv -= ampEnv * decayRate;
    return hp * ampEnv * vel * 8000.0f;
  }

  float renderCowbell() {
    phase += 540.0f / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    phase2 += 800.0f / (float)SAMPLE_RATE;
    if (phase2 >= 1.0f) phase2 -= 1.0f;
    float tone = ((phase < 0.5f) ? 1.0f : -1.0f) * 0.5f + ((phase2 < 0.5f) ? 1.0f : -1.0f) * 0.5f;
    ampEnv -= ampEnv * 0.0026f;
    return tone * ampEnv * vel * 8000.0f;
  }

  Type type = KICK;
  bool active = false;
  float vel = 1.0f;
  float phase = 0.0f, phase2 = 0.0f;
  float hatPhase[6] = { 0, 0, 0, 0, 0, 0 };
  float hpState = 0.0f;
  float ampEnv = 0.0f, pitchEnv = 1.0f;
  uint16_t lfsr = 0xACE1u;
  uint32_t age = 0;
  uint8_t pulseIndex = 0;
  uint16_t pulseAge = 0;
};

// ------------------------------------------------------------- Drum909Voice
// The 909's character versus the 808: a much shorter, sharper pitch sweep and
// an audible click on the kick, a noisier and brighter snare, and metallic
// rather than filtered-noise cymbals.
class Drum909Voice {
public:
  enum Type : uint8_t { BD, SD, LOW_TOM, MID_TOM, HI_TOM, RIM, CLAP, CH, OH, CRASH, RIDE, TYPE_COUNT };

  void begin(Type t) {
    type = t;
    lfsr = 0x1234u ^ (uint16_t)((uint16_t)t * 613u + 7u);
  }

  void trigger(uint8_t velocity) {
    active = true;
    vel = (float)velocity / 127.0f;
    phase = phase2 = 0.0f;
    age = 0;
    ampEnv = 1.0f;
    pitchEnv = 1.0f;
    noiseEnv = 1.0f;
    pulseIndex = 0;
    pulseAge = 0;
  }

  int16_t render() {
    if (!active) return 0;
    float s;
    switch (type) {
      case BD:      s = renderKick(); break;
      case SD:      s = renderSnare(); break;
      case LOW_TOM: s = renderTom(100.0f, 65.0f, 0.0018f); break;
      case MID_TOM: s = renderTom(150.0f, 95.0f, 0.0021f); break;
      case HI_TOM:  s = renderTom(220.0f, 140.0f, 0.0025f); break;
      case RIM:     s = renderRim(); break;
      case CLAP:    s = renderClap(); break;
      case CH:      s = renderCymbal(0.0030f, 0.0f); break;
      case OH:      s = renderCymbal(0.00045f, 0.0f); break;
      case CRASH:   s = renderCymbal(0.00016f, 0.35f); break;
      case RIDE:    s = renderCymbal(0.00030f, 0.6f); break;
      default:      s = 0.0f; break;
    }
    age++;
    if (ampEnv <= 0.0008f) active = false;
    return (int16_t)constrain((long)s, -32000L, 32000L);
  }

private:
  uint16_t nextNoise() {
    lfsr ^= lfsr << 7;
    lfsr ^= lfsr >> 9;
    lfsr ^= lfsr << 8;
    return lfsr;
  }

  float renderKick() {
    // Tighter sweep than the 808 and a hard click at the very start.
    pitchEnv *= R338_KICK909_SWEEP;
    float pitchHz = 48.0f + 90.0f * pitchEnv;
    phase += pitchHz / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    float tone = r338_sin(phase);
    float click = (age < 30) ? (1.0f - (float)age / 30.0f) * 0.7f : 0.0f;
    ampEnv -= ampEnv * 0.00105f;
    return (tone + click) * ampEnv * vel * 15000.0f;
  }

  float renderSnare() {
    phase += 238.0f / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    phase2 += 476.0f / (float)SAMPLE_RATE;
    if (phase2 >= 1.0f) phase2 -= 1.0f;
    float tone = r338_sin(phase) * 0.6f + r338_sin(phase2) * 0.4f;
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    ampEnv -= ampEnv * 0.0030f;
    noiseEnv -= noiseEnv * 0.0020f;  // noise outlasts the tone, unlike the 808
    return (tone * ampEnv * 0.4f + noise * noiseEnv * 1.0f) * vel * 13000.0f;
  }

  float renderTom(float startHz, float endHz, float decayRate) {
    pitchEnv *= R338_TOM909_SWEEP;
    float pitchHz = endHz + (startHz - endHz) * pitchEnv;
    phase += pitchHz / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    float tone = r338_sin(phase);
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    ampEnv -= ampEnv * decayRate;
    return (tone * 0.9f + noise * 0.1f) * ampEnv * vel * 11500.0f;
  }

  float renderRim() {
    phase += 1700.0f / (float)SAMPLE_RATE;
    if (phase >= 1.0f) phase -= 1.0f;
    float tone = (phase < 0.5f) ? 1.0f : -1.0f;
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    ampEnv -= ampEnv * 0.035f;
    return (tone * 0.5f + noise * 0.5f) * ampEnv * vel * 10500.0f;
  }

  float renderClap() {
    const uint16_t pulseGap = (uint16_t)(SAMPLE_RATE * 0.009f);
    if (pulseIndex < 3 && pulseAge > pulseGap) {
      pulseIndex++;
      pulseAge = 0;
      ampEnv = 1.0f;
    }
    float noise = (float)(int16_t)nextNoise() / 32768.0f;
    ampEnv -= ampEnv * ((pulseIndex < 3) ? 0.022f : 0.0026f);
    pulseAge++;
    return noise * ampEnv * vel * 12500.0f;
  }

  // Six detuned squares high-passed into metal. Ratios are stretched further
  // apart than the 808's for the 909's brighter, longer cymbals; `tonal`
  // mixes in a pitched partial so ride reads distinctly from crash.
  float renderCymbal(float decayRate, float tonal) {
    static const float ratios[6] = { 1.0f, 1.47f, 1.79f, 2.11f, 2.53f, 2.99f };
    const float fundamental = 318.0f;
    float mix = 0.0f;
    for (uint8_t i = 0; i < 6; i++) {
      hatPhase[i] += (fundamental * ratios[i]) / (float)SAMPLE_RATE;
      if (hatPhase[i] >= 1.0f) hatPhase[i] -= 1.0f;
      mix += (hatPhase[i] < 0.5f) ? 1.0f : -1.0f;
    }
    mix /= 6.0f;
    hpState += (mix - hpState) * 0.55f;
    float hp = mix - hpState;
    if (tonal > 0.0f) {
      phase2 += 1050.0f / (float)SAMPLE_RATE;
      if (phase2 >= 1.0f) phase2 -= 1.0f;
      hp = hp * (1.0f - tonal) + r338_sin(phase2) * tonal;
    }
    ampEnv -= ampEnv * decayRate;
    return hp * ampEnv * vel * 9000.0f;
  }

  Type type = BD;
  bool active = false;
  float vel = 1.0f;
  float phase = 0.0f, phase2 = 0.0f;
  float hatPhase[6] = { 0, 0, 0, 0, 0, 0 };
  float hpState = 0.0f;
  float ampEnv = 0.0f, noiseEnv = 0.0f, pitchEnv = 1.0f;
  uint16_t lfsr = 0x1234u;
  uint32_t age = 0;
  uint8_t pulseIndex = 0;
  uint16_t pulseAge = 0;
};

// ------------------------------------------------------------ Rebirth338Engine
class Rebirth338Engine {
public:
  static const uint8_t ACID_A_CHANNEL = 3;
  static const uint8_t ACID_B_CHANNEL = 4;
  static const uint8_t DRUM_CHANNEL = 10;     // 808 kit, GM-style drum notes
  static const uint8_t DRUM909_CHANNEL = 11;  // 909 kit, same note map

  void begin() {
    r338_init_lut();
    acidA.begin();
    acidB.begin();
    acidA.setWave(Acid303Voice::WAVE_SAW);
    acidB.setWave(Acid303Voice::WAVE_SQUARE);
    for (uint8_t i = 0; i < Drum808Voice::TYPE_COUNT; i++) {
      drums[i].begin((Drum808Voice::Type)i);
    }
    for (uint8_t i = 0; i < Drum909Voice::TYPE_COUNT; i++) {
      drums909[i].begin((Drum909Voice::Type)i);
    }
    seedDemoPattern();
  }

  // Called once per 16th-note step from mySEQ() so ReBirth338 stays in sync
  // with the main transport (play/stop, BPM) without touching pattern[]/
  // melodic[] which belong to the 16 sample/synth tracks.
  void onStep(uint8_t step) {
    step &= 15;
    if (bitRead(patA, step)) acidA.noteOn(noteA[step], bitRead(accA, step), bitRead(slideA, step));
    if (bitRead(patB, step)) acidB.noteOn(noteB[step], bitRead(accB, step), bitRead(slideB, step));
    for (uint8_t d = 0; d < Drum808Voice::TYPE_COUNT; d++) {
      if (bitRead(patDrum[d], step)) drums[d].trigger(drumLevels[d]);
    }
    for (uint8_t d = 0; d < Drum909Voice::TYPE_COUNT; d++) {
      if (bitRead(patDrum909[d], step)) drums909[d].trigger(drumLevels909[d]);
    }
  }

  // Called once per output sample from write_buffer(); sums straight into
  // the master dry bus (this engine doesn't route through the per-track FX
  // sends used by the 16 sample/synth tracks).
  void renderInto(int32_t &accL, int32_t &accR) {
    int32_t mix = acidA.render() + acidB.render();
    for (uint8_t d = 0; d < Drum808Voice::TYPE_COUNT; d++) mix += drums[d].render();
    for (uint8_t d = 0; d < Drum909Voice::TYPE_COUNT; d++) mix += drums909[d].render();

    // Soft-clip the engine's own bus rather than handing an oversized sum to
    // the master. Headroom comes from the per-voice gains below: they're set
    // so roughly four simultaneous hits reach full scale, which is what a
    // busy pattern actually looks like.
    mix = (mix * (int32_t)masterLevel) >> 8;
    mix = soft_clip(mix);
    accL += mix;
    accR += mix;
  }

  // Driven by the USB MIDI host (USB_tools.ino).
  void noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (channel == ACID_A_CHANNEL) acidA.noteOn(note, velocity >= 100, false);
    else if (channel == ACID_B_CHANNEL) acidB.noteOn(note, velocity >= 100, false);
    else if (channel == DRUM_CHANNEL) drumNoteOn(note, velocity);
    else if (channel == DRUM909_CHANNEL) drum909NoteOn(note, velocity);
  }

  void noteOff(uint8_t channel, uint8_t note) {
    (void)note;
    if (channel == ACID_A_CHANNEL) acidA.noteOff();
    else if (channel == ACID_B_CHANNEL) acidB.noteOff();
  }

  void controlChange(uint8_t channel, uint8_t cc, uint8_t value) {
    Acid303Voice *v = nullptr;
    if (channel == ACID_A_CHANNEL) v = &acidA;
    else if (channel == ACID_B_CHANNEL) v = &acidB;
    if (!v) return;
    switch (cc) {
      case 74: v->setCutoff((uint8_t)constrain((int)value * 2, 0, 255)); break;     // filter cutoff
      case 71: v->setResonance((uint8_t)constrain((int)value * 2, 0, 255)); break;  // resonance
      case 73: v->setEnvMod(value); break;                                          // env mod amount
      case 72: v->setDecay(value); break;                                           // decay
      case 91: v->setAccentAmount(value); break;                                    // accent amount
      default: break;
    }
  }

  // ---- API for the on-screen ReBirth panels (rebirth_ui.h) ----

  Acid303Voice &acid(uint8_t which) { return which ? acidB : acidA; }
  Drum808Voice &drum(uint8_t d) { return drums[d < Drum808Voice::TYPE_COUNT ? d : 0]; }
  static uint8_t drumCount() { return Drum808Voice::TYPE_COUNT; }
  static uint8_t drum909Count() { return Drum909Voice::TYPE_COUNT; }

  // 909 kit, mirroring the 808 accessors above.
  bool drum909Step(uint8_t d, uint8_t step) const {
    return d < Drum909Voice::TYPE_COUNT ? bitRead(patDrum909[d], step & 15) : false;
  }
  void toggleDrum909Step(uint8_t d, uint8_t step) {
    if (d >= Drum909Voice::TYPE_COUNT) return;
    step &= 15;
    bitWrite(patDrum909[d], step, !bitRead(patDrum909[d], step));
  }
  uint8_t drum909Level(uint8_t d) const { return d < Drum909Voice::TYPE_COUNT ? drumLevels909[d] : 0; }
  void setDrum909Level(uint8_t d, uint8_t v) {
    if (d < Drum909Voice::TYPE_COUNT) drumLevels909[d] = v;
  }
  void auditionDrum909(uint8_t d) {
    if (d < Drum909Voice::TYPE_COUNT) drums909[d].trigger(drumLevels909[d]);
  }

  // Acid step data: on/off, accent, slide and the note per step.
  bool acidStep(uint8_t which, uint8_t step) const { return bitRead(which ? patB : patA, step & 15); }
  bool acidAccent(uint8_t which, uint8_t step) const { return bitRead(which ? accB : accA, step & 15); }
  bool acidSlide(uint8_t which, uint8_t step) const { return bitRead(which ? slideB : slideA, step & 15); }
  uint8_t acidNote(uint8_t which, uint8_t step) const { return which ? noteB[step & 15] : noteA[step & 15]; }

  void toggleAcidStep(uint8_t which, uint8_t step) {
    uint16_t &pat = which ? patB : patA;
    step &= 15;
    bitWrite(pat, step, !bitRead(pat, step));
  }
  void toggleAcidAccent(uint8_t which, uint8_t step) {
    uint16_t &acc = which ? accB : accA;
    step &= 15;
    bitWrite(acc, step, !bitRead(acc, step));
  }
  void toggleAcidSlide(uint8_t which, uint8_t step) {
    uint16_t &sl = which ? slideB : slideA;
    step &= 15;
    bitWrite(sl, step, !bitRead(sl, step));
  }
  void setAcidNote(uint8_t which, uint8_t step, uint8_t note) {
    if (which) noteB[step & 15] = note;
    else noteA[step & 15] = note;
  }

  // Drum step data and per-voice mix, as on the 808's own front panel.
  bool drumStep(uint8_t d, uint8_t step) const {
    return d < Drum808Voice::TYPE_COUNT ? bitRead(patDrum[d], step & 15) : false;
  }
  void toggleDrumStep(uint8_t d, uint8_t step) {
    if (d >= Drum808Voice::TYPE_COUNT) return;
    step &= 15;
    bitWrite(patDrum[d], step, !bitRead(patDrum[d], step));
  }
  uint8_t drumLevel(uint8_t d) const { return d < Drum808Voice::TYPE_COUNT ? drumLevels[d] : 0; }
  void setDrumLevel(uint8_t d, uint8_t v) {
    if (d < Drum808Voice::TYPE_COUNT) drumLevels[d] = v;
  }
  void auditionDrum(uint8_t d) {
    if (d < Drum808Voice::TYPE_COUNT) drums[d].trigger(drumLevels[d]);
  }
  void auditionAcid(uint8_t which, uint8_t note) {
    (which ? acidB : acidA).noteOn(note, false, false);
  }

  uint8_t masterLevelGet() const { return masterLevel; }
  void masterLevelSet(uint8_t v) { masterLevel = v; }

private:
  void drumNoteOn(uint8_t note, uint8_t velocity) {
    int8_t idx = gmNoteToDrum(note);
    if (idx >= 0) drums[idx].trigger(velocity);
  }

  void drum909NoteOn(uint8_t note, uint8_t velocity) {
    int8_t idx = gmNoteToDrum909(note);
    if (idx >= 0) drums909[idx].trigger(velocity);
  }

  static int8_t gmNoteToDrum909(uint8_t note) {
    switch (note) {
      case 36: return Drum909Voice::BD;
      case 38:
      case 40: return Drum909Voice::SD;
      case 39: return Drum909Voice::CLAP;
      case 37: return Drum909Voice::RIM;
      case 41:
      case 43: return Drum909Voice::LOW_TOM;
      case 45:
      case 47: return Drum909Voice::MID_TOM;
      case 48:
      case 50: return Drum909Voice::HI_TOM;
      case 42:
      case 44: return Drum909Voice::CH;
      case 46: return Drum909Voice::OH;
      case 49: return Drum909Voice::CRASH;
      case 51: return Drum909Voice::RIDE;
      default: return -1;
    }
  }

  static int8_t gmNoteToDrum(uint8_t note) {
    switch (note) {
      case 36: return Drum808Voice::KICK;
      case 38:
      case 40: return Drum808Voice::SNARE;
      case 39: return Drum808Voice::CLAP;
      case 42:
      case 44: return Drum808Voice::CLOSED_HAT;
      case 46: return Drum808Voice::OPEN_HAT;
      case 41:
      case 43: return Drum808Voice::LOW_TOM;
      case 45:
      case 47:
      case 48:
      case 50: return Drum808Voice::HI_TOM;
      case 56: return Drum808Voice::COWBELL;
      case 37: return Drum808Voice::RIMSHOT;
      default: return -1;
    }
  }

  // Simple four-on-the-floor + acid riff demo groove, audible with no MIDI
  // gear attached. Live MIDI/pattern edits overwrite it instantly.
  void seedDemoPattern() {
    patDrum[Drum808Voice::KICK] = 0b0001000100010001;
    patDrum[Drum808Voice::CLOSED_HAT] = 0b0101010101010101;
    patDrum[Drum808Voice::OPEN_HAT] = 0b0010000000100000;
    patDrum[Drum808Voice::SNARE] = 0b0000000100000001;

    patA = 0b1111101111111011;
    accA = 0b1000000010000000;
    slideA = 0b0000100000001000;
    const uint8_t riffA[16] = { 45, 45, 57, 45, 48, 45, 45, 52, 45, 45, 57, 45, 48, 45, 53, 45 };
    memcpy(noteA, riffA, 16);

    patB = 0;
    accB = 0;
    slideB = 0;
    for (uint8_t i = 0; i < 16; i++) noteB[i] = 33;
  }

  Acid303Voice acidA, acidB;
  Drum808Voice drums[Drum808Voice::TYPE_COUNT];

  uint16_t patA = 0, accA = 0, slideA = 0;
  uint16_t patB = 0, accB = 0, slideB = 0;
  uint8_t noteA[16] = { 0 }, noteB[16] = { 0 };
  uint16_t patDrum[Drum808Voice::TYPE_COUNT] = { 0 };
  uint8_t drumLevels[Drum808Voice::TYPE_COUNT] = { 100, 100, 100, 100, 100, 100, 100, 100, 100 };

  Drum909Voice drums909[Drum909Voice::TYPE_COUNT];
  uint16_t patDrum909[Drum909Voice::TYPE_COUNT] = { 0 };
  uint8_t drumLevels909[Drum909Voice::TYPE_COUNT] = { 110, 110, 110, 110, 110, 110, 110, 110, 110, 110, 110 };

  // Loud, but leaving room for the sampler tracks sharing the master bus.
  uint8_t masterLevel = 200;
};

Rebirth338Engine rebirth338;

inline void rebirth338_begin() {
  rebirth338.begin();
}
inline void rebirth338_onStep(uint8_t step) {
  rebirth338.onStep(step);
}
inline void rebirth338_renderInto(int32_t &accL, int32_t &accR) {
  rebirth338.renderInto(accL, accR);
}
inline void rebirth338_noteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  rebirth338.noteOn(channel, note, velocity);
}
inline void rebirth338_noteOff(uint8_t channel, uint8_t note) {
  rebirth338.noteOff(channel, note);
}
inline void rebirth338_controlChange(uint8_t channel, uint8_t cc, uint8_t value) {
  rebirth338.controlChange(channel, cc, value);
}
