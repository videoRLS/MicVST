#pragma once
#include "audio/BuiltInEffects.h"

// Factories of the built-in effects (one per entry of builtin::effects()).
namespace fx
{
    using Factory = std::unique_ptr<BuiltInEffect> (const BuiltInEffect::Info&);

    // EqEffects.cpp
    Factory createLowCut, createHighCut, createBody, createMud, createNasal, createPresence, createAir,
            createProximity, createTilt, createParamEq, createHum, createDcBlock;
    // ColourEffects.cpp
    Factory createSaturation, createLoFi, createReverb, createEcho, createChorus;
    // LevelEffects.cpp
    Factory createLimiter, createMute, createDeEsser, createSyncDelay, createChannelTool;
    // MeterEffects.cpp
    Factory createLufsMeter, createSpectrum;

    // Static curve of the de-esser (dB of reduction for a detector level), for the unit tests.
    double deEsserReductionDb (double levelDb, double thresholdDb, double rangeDb);
}
