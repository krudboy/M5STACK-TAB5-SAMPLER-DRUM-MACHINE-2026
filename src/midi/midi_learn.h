#pragma once
enum ParameterID { PARAM_303A_CUTOFF, PARAM_303A_RESONANCE, PARAM_MASTER_VOLUME, PARAM_COUNT };
struct MidiMap{bool active; unsigned char channel; unsigned char cc; ParameterID parameter;};
