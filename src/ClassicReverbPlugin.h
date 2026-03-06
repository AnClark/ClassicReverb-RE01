#ifndef CLASSIC_REVERB_PLUGIN_H_INCLUDED
#define CLASSIC_REVERB_PLUGIN_H_INCLUDED

#include "DistrhoPlugin.hpp"
#include "Structures.h"
#include "Defines.h"
#include "config.h"
#if CLASSIC_REVERB_SILENCE_GATE
#include "SilenceGate.h"
#endif // CLASSIC_REVERB_SILENCE_GATE

class ClassicReverbPlugin : public DISTRHO::Plugin
{
public:
    ClassicReverbPlugin();

protected:
    // ── Plugin metadata ────────────────────────────────────────────────────
    const char* getLabel()   const override { return DISTRHO_PLUGIN_NAME; }
    const char* getMaker()   const override { return DISTRHO_PLUGIN_BRAND; }
    const char* getLicense() const override { return "GPLv3+"; }
    uint32_t    getVersion() const override { return d_version(VERSION_MAJOR, VERSION_MINOR, VERSION_PATCH); }

    // ── Parameters ────────────────────────────────────────────────────────
    void initParameter(uint32_t index, Parameter& param) override;
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;

    // ── State ──────────────────────────────────────────────────────────────
    void initState(uint32_t index, State& state) override;
    void setState(const char* key, const char* value) override;

    // ── Audio processing ──────────────────────────────────────────────────
    void activate() override;
    void sampleRateChanged(double newSampleRate) override;
    void run(const float** inputs, float** outputs, uint32_t frames) override;

private:
    // ── Internal helpers ──────────────────────────────────────────────────

    // Schroeder allpass section — identical difference equation to the
    // original (FUN_0048490c):  w = g*delayed + in;  out = delayed - g*w.
    // The DLL multiplies with the x87 80-bit constant kAllpassCoeff, so the
    // arithmetic is done in double to avoid the ~4e-9 error of float 0.6f.
    inline float schroederAllpass(float in, SmallCircBuf& buf, double g)
    {
        float delayed = buf.buf[buf.pos];
        float w       = (float)(g * (double)delayed + (double)in);
        buf.buf[buf.pos] = w;
        if (++buf.pos >= buf.size) buf.pos = 0;
        return (float)((double)delayed - g * (double)w);
    }


    // Normalise room-size m² → 0–1 (VST normalised form used by original plugin)
    static float roomSizeToNorm(float sqm)
    {
        // The original plugin stored a normalised value that scaled the comb
        // delay lengths.  A room area of 0.625 m² → norm=0, 640 m² → norm=1.
        // Using simple log2 mapping: norm = (log2(sqm) - log2(0.625)) / (log2(640)-log2(0.625))
        // log2(0.625)≈-0.678, log2(640)≈9.322 → range ~10.0
        float logMin = std::log2f(0.625f);
        float logMax = std::log2f(640.0f);
        float norm   = (std::log2f(sqm) - logMin) / (logMax - logMin);
        return std::clamp(norm, 0.0f, 1.0f);
    }

    // ── Buffer allocation ─────────────────────────────────────────────────
    void allocateBuffers();

#if CLASSIC_REVERB_SILENCE_GATE
    // Clear all delay/filter state.  Called by the silence gate when it
    // arms, so the sample-by-sample early-out starts from a truly empty tank
    // (and no denormal residue keeps circulating).
    void clearReverbBuffers();
#endif // CLASSIC_REVERB_SILENCE_GATE

    // ── Coefficient update ────────────────────────────────────────────────
    void updateCoefficients();

    // ── State ──────────────────────────────────────────────────────────────
    float fParams[kParamCount];
    float fSampleRate = 44100.0f;

    // Pre-delay
    PreDelayBuf fPreDelay;
    int         fPreDelaySamples = 0;  // current read-offset in samples

    // Early reflections (stereo buffers, matching the original DLL)
    ErBuf fErBufL;
    ErBuf fErBufR;
    int   fErDelayLen[7];

    // Allpass diffusion
    SmallCircBuf fAP[3];

    // Comb filters
    CircBuf fCombBuf[16];
    int     fCombLen[16];

    // ── Computed coefficients ─────────────────────────────────────────────────
    // Comb feedback (Damping): g = 0.40 + 0.58 * (1 - dampingNorm)
    float fRoomCoeff    = 0.6f;
    float fRoomCoeffSq  = 0.8f;   // sqrt(1 - g), scales the comb output sum
    double fHiDampK     = 0.0;    // 0.6 * hiDampNorm, in-place comb filter depth
    float fLevelGain    = 1.0f;
    float fMix          = 0.35f;
    float fEarlyRefNorm = 0.05f;  // p_cc: wet += er * 2 * fEarlyRefNorm
    float fPreDelayNorm = 0.5f;   // raw 0..1 pre-delay param (0.5 = 0 ms)

    // Fixed per-sample-rate filters (FUN_00483F24)
    float fCombFiltB0 = 0.0f, fCombFiltB1 = 0.0f, fCombFiltA1 = 0.0f; // 2 kHz LP
    float fErA_B = 0.0f, fErA_A1 = 0.0f;                             // 8 kHz LP
    float fErB_B0 = 0.0f, fErB_B1 = 0.0f, fErB_B2 = 0.0f;            // 1 kHz LP
    float fErB_A1 = 0.0f, fErB_A2 = 0.0f;

    // Lo-Cut one-pole highpass (param-driven, FUN_004845b8):
    //   w = x - z*bc4;  y = z*bc0 + w*bbc;  z = w     (bc0 == -bbc)
    float fLoCutBbc = 1.0f, fLoCutBc0 = -1.0f, fLoCutBc4 = 0.0f;
    float fLoCutZL = 0.0f, fLoCutZR = 0.0f;

    // Per-channel filter states
    float fErAzL = 0.0f, fErAzR = 0.0f;
    float fErBz1L = 0.0f, fErBz1R = 0.0f, fErBz2L = 0.0f, fErBz2R = 0.0f;
    float fCombFiltZ[16] = {};

#if CLASSIC_REVERB_INPUT_NOISE_MODULATION
    // Faithful re-implementation of RandExt (0x402C08):
    //   state = state * 0x08088405 + 1        (uint32, initial state 0)
    //   rand  = state * 2^-32                 (double @ 0x487068)
    //   noise = kModAmp * (rand - 0.5)         (kModAmp @ 0x485F5C, float80)
    uint32_t fRngState = 0u;

    inline float nextNoise() noexcept
    {
        fRngState = fRngState * kRngMul + 1u;
        double rnd = (double)fRngState * kRngScale;
        return (float)((rnd - 0.5) * kModAmp);
    }
#endif // CLASSIC_REVERB_INPUT_NOISE_MODULATION

#if CLASSIC_REVERB_SILENCE_GATE
    SilenceGate fSilenceGate;
#endif // CLASSIC_REVERB_SILENCE_GATE

    // Preset bank
    String fPresetName[33];
    float  fPresetParam[33][kParamCount];

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClassicReverbPlugin)
};

#endif // CLASSIC_REVERB_PLUGIN_H_INCLUDED
