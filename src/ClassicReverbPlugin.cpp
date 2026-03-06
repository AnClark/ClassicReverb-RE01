#include "ClassicReverbPlugin.h"
#include "Defines.h"

// ─────────────────────────────────────────────────────────────────────────────
// Classic Reverb plugin class
// ─────────────────────────────────────────────────────────────────────────────

ClassicReverbPlugin::ClassicReverbPlugin()
    : DISTRHO::Plugin(kParamCount, 0, 3)  // 3 states: preset_name, preset_modified, preset_type
{
    // Default parameter values – physical units matching initParameter() ranges
    for (uint32_t i = 0; i < kParamCount; ++i)
        fParams[i] = kParamRanges[i].def;

    // Call sampleRateChanged() to allocate buffers based on the initial sample rate.
    // This ensures that the plugin is ready to process audio immediately after construction.
    sampleRateChanged(getSampleRate());
}

// ── Parameters ────────────────────────────────────────────────────────
void ClassicReverbPlugin::initParameter(uint32_t index, Parameter& param)
{
    param.hints = kParameterIsAutomatable;
    param.ranges = DISTRHO::ParameterRanges(kParamRanges[index]);

    switch (index)
    {
    case kParamRoomSize:
        param.name = "Room Size";
        param.symbol = "room_size";
        param.unit  = "m^2";
        param.hints |= kParameterIsLogarithmic;
        // Override: use actual physical unit
        break;
    case kParamDamping:
        param.name = "Damping";
        param.symbol = "damping";
        param.unit  = "%";
        break;
    case kParamPreDelay:
        param.name = "Pre-delay";
        param.symbol = "pre_delay";
        param.unit  = "ms";
        break;
    case kParamHiDamp:
        param.name = "Hi-Damp.";
        param.symbol = "hi_damp";
        param.unit  = "%";
        param.ranges.max = 100.0f;
        param.ranges.def = 30.0f;
        break;
    case kParamLoCut:
        param.name = "Lo-Cut";
        param.symbol = "lo_cut";
        param.unit  = "Hz";
        param.hints |= kParameterIsLogarithmic;
        break;
    case kParamEarlyRef:
        param.name = "Early Ref.";
        param.symbol = "early_ref";
        param.unit  = "dB";
        break;
    case kParamMix:
        param.name = "Mix";
        param.symbol = "mix";
        break;
    case kParamLevel:
        param.name = "Level";
        param.symbol = "level";
        param.unit  = "dB";
        break;
    }
}

float ClassicReverbPlugin::getParameterValue(uint32_t index) const
{
    return fParams[index];
}

void ClassicReverbPlugin::setParameterValue(uint32_t index, float value)
{
    fParams[index] = std::clamp(value, kParamRanges[index].min,
                                        kParamRanges[index].max);
    updateCoefficients();
}


// ── State ────────────────────────────────────────────────────────────
void ClassicReverbPlugin::initState(uint32_t index, State& state)
{
    state.hints = kStateIsHostWritable;

    switch (index)
    {
    case 0:
        state.key          = "preset_name";
        state.defaultValue = "";
        state.label        = "Current Preset Name";
        break;
    case 1:
        state.key          = "preset_modified";
        state.defaultValue = "false";
        state.label        = "Preset Modified";
        break;
    case 2:
        state.key          = "preset_type";
        state.defaultValue = "Factory";
        state.label        = "Preset Type";
        break;
    default:
        break;
    }
}

void ClassicReverbPlugin::setState(const char* /*key*/, const char* /*value*/)
{
    // Preset state is managed by the UI; the DSP side does not need to act on it.
    // DPF will forward state changes to the UI via stateChanged() automatically.
}

// ── Audio processing ──────────────────────────────────────────────────
void ClassicReverbPlugin::activate()
{
    // NOTICE: No need to invoke sampleRateChanged() here to allocate buffers.
    //         Hosts are expected to call sampleRateChanged() before processing and on sample rate changes.
    //         If we invoked sampleRateChanged() here, we may hear a click on play start in hosts (for example, REAPER).
}

void ClassicReverbPlugin::sampleRateChanged(double newSampleRate)
{
    fSampleRate = (float)newSampleRate;
    allocateBuffers();
    updateCoefficients();
}



// ── Buffer allocation ─────────────────────────────────────────────────
void ClassicReverbPlugin::allocateBuffers()
{
    const float sr = fSampleRate;

    // Early reflection buffer: the original ring holds the longest delay at
    // the maximum room scale (delayFactor = 0.968 * 32 * sr).  The tap
    // lengths themselves are computed in updateCoefficients().
    int maxErLen = (int)(kErDelay[6] * (float)(kDelayScale * 32.0 * (double)sr)) + 8;
    fErBufL.reset(maxErLen + 1);
    fErBufR.reset(maxErLen + 1);

    // Allpass buffers: allocated at the maximum room scale;
    // updateCoefficients() shrinks size to the current room (the original
    // only rewrites the wrap length and keeps the ring contents).
    int maxApLen = (int)(kApDelay[2] * (float)(kDelayScale * 32.0 * (double)sr)) + 4;
    if (maxApLen > kMaxApSamples) maxApLen = kMaxApSamples;
    for (int a = 0; a < 3; ++a)
        fAP[a].reset(std::max(1, maxApLen));

    // Comb filter buffers: sized for the maximum delay (room scale 32,
    // including the 0.968 delay scale):  maxLen = kCombDelay[c]*0.968*32*sr + 8
    for (int c = 0; c < 16; ++c)
    {
        int maxLen = (int)(kCombDelay[c] * (float)(kDelayScale * 32.0 * (double)sr)) + 8;
        if (maxLen > kMaxCombSamples) maxLen = kMaxCombSamples;
        fCombBuf[c].reset(maxLen);
        fCombFiltZ[c] = 0.0f;
    }

    // Pre-delay: always allocate the full buffer; delay length is stored
    // separately in fPreDelaySamples and passed per-call in run().
    fPreDelay.reset();
    fPreDelaySamples = 0;

    // Filter states (the original's reset routine at 0x4841FB zeroes the ER
    // ring, the ER/Lo-Cut states and all comb indices).
    fErAzL = fErAzR = 0.0f;
    fErBz1L = fErBz1R = fErBz2L = fErBz2R = 0.0f;
    fLoCutZL = fLoCutZR = 0.0f;
}

#if CLASSIC_REVERB_SILENCE_GATE
// ── Silence gate: tank clear ─────────────────────────────────────────────
// Invoked once when the silence gate arms.  At that point the PRE-wet output
// is already below -120 dBFS, so zeroing the buffers is inaudible; it mainly
// guarantees a click-free restart (no stale tail) and drops any denormal
// residue while the gate holds the early-out.
// reset(n) with n = current size only zeroes the used span and rewinds the
// write pointer, so delay lengths are preserved.
void ClassicReverbPlugin::clearReverbBuffers()
{
    for (int c = 0; c < 16; ++c)
    {
        fCombBuf[c].reset(fCombBuf[c].size);
        fCombFiltZ[c] = 0.0f;
    }

    for (int a = 0; a < 3; ++a)
        fAP[a].reset(fAP[a].size);

    fErBufL.reset(fErBufL.size);
    fErBufR.reset(fErBufR.size);
    fPreDelay.reset();

    fErAzL = fErAzR = 0.0f;
    fErBz1L = fErBz1R = fErBz2L = fErBz2R = 0.0f;
    fLoCutZL = fLoCutZR = 0.0f;
}
#endif // CLASSIC_REVERB_SILENCE_GATE

// ─────────────────────────────────────────────────────────────────────────────
// Entry point
// ─────────────────────────────────────────────────────────────────────────────

START_NAMESPACE_DISTRHO

Plugin* createPlugin()
{
    return new ClassicReverbPlugin();
}

END_NAMESPACE_DISTRHO
