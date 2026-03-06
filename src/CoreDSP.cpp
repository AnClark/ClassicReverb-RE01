#include "ClassicReverbPlugin.h"
#include "Defines.h"
#include "extra/ScopedDenormalDisable.hpp"

#include <algorithm>
#include <cmath>

static constexpr double kPi = 3.14159265358979323846;

// ═════════════════════════════════════════════════════════════════════════════
// Per-sample DSP — faithful transcription of FUN_0048490c ("ProcessSamples")
// from the original Classic Reverb.dll.
//
// Every statement mirrors one float32 store (x87 fstps) of the original, and
// multi-term expressions are evaluated in double so that rounding happens
// once at the store, exactly like the x87 register chain (a plain float chain
// rounds at every step and diverges roughly a quarter of the time).
// True bit-exactness against the x87 build is impossible off-x86 — the
// original's fsin/fcos and 80-bit intermediates have no IEEE double
// equivalent — but formulas, constants, operation order and rounding points
// all match the binary.
// Two deliberate improvements are enabled on top (see Defines.h):
// OUTPUT_SOFT_CLIP (output level protection) and SILENCE_GATE (CPU saving).
// ═════════════════════════════════════════════════════════════════════════════

void ClassicReverbPlugin::run(const float** inputs, float** outputs, uint32_t frames)
{
    const ScopedDenormalDisable denormalDisable;

    const float* inL  = inputs[0];
    const float* inR  = inputs[1];
    float*       outL = outputs[0];
    float*       outR = outputs[1];

    // Cache the coefficient block (the original re-reads these float32
    // fields from the state struct for every operation).
    const float  g   = fRoomCoeff;     // comb feedback            (0x127BAC)
    const float  gsq = fRoomCoeffSq;   // sqrt(1 - g)              (0x127BA8)
    const double hiK = fHiDampK;       // 0.6 * hiDampNorm — register-resident
    const float  mix = fMix;           // param[0xD0]
    const float  lvl = fLevelGain;     // 0x127BE8
    const float  erN = fEarlyRefNorm;  // param[0xCC]
    const float  pdN = fPreDelayNorm;  // param[0xC0]
    const int    pdLen = fPreDelaySamples;

    for (uint32_t i = 0; i < frames; ++i)
    {
        const float dryL = inL[i];
        const float dryR = inR[i];

#if CLASSIC_REVERB_SILENCE_GATE
        if (fSilenceGate.shouldSkipProcessing(dryL, dryR))
        {
            outL[i] = outR[i] = 0.0f;
            continue;
        }
#endif // CLASSIC_REVERB_SILENCE_GATE

        // ── 1. Input dither: noise = kModAmp * (rand - 0.5) ─────────────────
        //     RandExt runs once per frame; the same value is added to both
        //     channels (0x127BA4 -> 0x127B64/0x127B68).
#if CLASSIC_REVERB_INPUT_NOISE_MODULATION
        const float noise = nextNoise();
        const float sL = dryL + noise;
        const float sR = dryR + noise;
#else
        const float sL = dryL;
        const float sR = dryR;
#endif // CLASSIC_REVERB_INPUT_NOISE_MODULATION

        // ── 2. Early reflections: 7 taps per channel ────────────────────────
        // The original writes the dithered input at idx, reads taps at
        // idx - delay[t] (ushort wrap), then increments idx.  Reading before
        // writing yields the identical idx - delay reference because every
        // delay is >= 1 sample.
        float tapL[7], tapR[7];
        for (int t = 0; t < 7; ++t)
        {
            const int d = fErDelayLen[t];
            tapL[t] = fErBufL.read(d) * kErGain[t];
            tapR[t] = fErBufR.read(d) * kErGain[t];
        }
        fErBufL.write(sL);
        fErBufR.write(sR);

        // Cross-channel partial sums — exact term order of FUN_0048490c:
        //   sumL = L0 + L1 + R2 + R3 + R4 + L6
        //   sumR = R0 + R1 + L2 + L3 + L4 + R6      (tap 5 excluded here)
        const float erSumL = (float)((double)tapL[0] + tapL[1] + tapR[2]
                                           + tapR[3] + tapR[4] + tapL[6]);
        const float erSumR = (float)((double)tapR[0] + tapR[1] + tapL[2]
                                           + tapL[3] + tapL[4] + tapR[6]);

        // ── 3. ER filter A: one-pole LP @ 8 kHz (0x127BC8/BCC/BD0) ──────────
        //     w = x - z*a1;  y = z*b + w*b;  z = w     (b = 1/(1+cot(u)))
        const float wAL = (float)((double)erSumL - (double)fErAzL * fErA_A1);
        const float wAR = (float)((double)erSumR - (double)fErAzR * fErA_A1);
        const float yAL = (float)((double)fErAzL * fErA_B + (double)wAL * fErA_B);
        const float yAR = (float)((double)fErAzR * fErA_B + (double)wAR * fErA_B);
        fErAzL = wAL;
        fErAzR = wAR;

        // ── 4. ER filter B: biquad LP @ 1 kHz, Q = 0.707 ────────────────────
        // Input = tap5 (both channels) + tap6 (crossed).  Direct-form II
        // transposed; states 0x127B84/88 (z1) and 0x127B8C/90 (z2).
        const float w1L = (float)(((double)tapL[5] + tapR[5] + tapR[6])
                                       - (double)fErBz1L * fErB_A1
                                       - (double)fErBz2L * fErB_A2);
        const float w1R = (float)(((double)tapL[5] + tapR[5] + tapL[6])
                                       - (double)fErBz1R * fErB_A1
                                       - (double)fErBz2R * fErB_A2);
        const float yBL = (float)((double)fErBz2L * fErB_B2
                                       + (double)fErBz1L * fErB_B1
                                       + (double)w1L  * fErB_B0);
        const float yBR = (float)((double)fErBz2R * fErB_B2
                                       + (double)fErBz1R * fErB_B1
                                       + (double)w1R  * fErB_B0);
        fErBz2L = fErBz1L;  fErBz2R = fErBz1R;
        fErBz1L = w1L;      fErBz1R = w1R;

        // ER contribution = biquad + one-pole (0x127B94/0x127B98)
        const float erL = yBL + yAL;
        const float erR = yBR + yAR;

        // ── 5. Mono sum + 3-stage input diffusion (Schroeder allpass) ───────
        float apx = sL + sR;                       // 0x127B64 = sL + sR
        for (int a = 0; a < 3; ++a)
            apx = schroederAllpass(apx, fAP[a], kAllpassCoeff);

        // ── 6. 16 parallel comb filters ─────────────────────────────────────
        float y[16];
        double sumD = 0.0;                          // one 16-term chain
        for (int c = 0; c < 16; ++c)
        {
            y[c] = fCombBuf[c].read(fCombLen[c]);
            sumD += (double)y[c];
        }
        const float sumY = (float)sumD;

        // Global feedback term: combIn = apOut - (sumY * 0.125) * g
        const float combIn = (float)((double)apx - ((double)sumY * 0.125) * (double)g);

        // Stereo mix of comb outputs — the original sums comb15 first, then
        // descending to comb0 (weights 0x488DEC ... 0x488D74).
        double wLd = (double)y[15] * (double)kCombMixL[15];
        double wRd = (double)y[15] * (double)kCombMixR[15];
        for (int c = 14; c >= 0; --c)
        {
            wLd += (double)y[c] * (double)kCombMixL[c];
            wRd += (double)y[c] * (double)kCombMixR[c];
        }
        float wetL = (float)wLd * gsq;              // 0x127B74/78 *= sqrt(1-g)
        float wetR = (float)wRd * gsq;

        // ── 7. Comb writes + in-place damping filter (fixed 2 kHz one-pole) ─
        // Original: buf = buf*g + combIn for all 16 combs, then
        //   w = x - z*a1; f = z*b1 + w*b0; x = x - (0.6*hiDamp)*f; z = w
        // for all 16 combs.  Each filter only touches its own slot and state,
        // so the per-comb interleaving below is equivalent.
        for (int c = 0; c < 16; ++c)
        {
            float x = (float)((double)y[c] * (double)g + (double)combIn);
            const float z = fCombFiltZ[c];
            const float w = (float)((double)x - (double)z * fCombFiltA1);
            const float f = (float)((double)z * fCombFiltB1 + (double)w * fCombFiltB0);
            x = (float)((double)x - hiK * (double)f);
            fCombFiltZ[c] = w;
            fCombBuf[c].write(x);
        }

        // ── 8. Lo-Cut: one-pole highpass on the comb sum ────────────────────
        //     w = x - z*bc4;  y = z*(-bbc) + w*bbc;  z = w   (0x127BBC/C0/C4)
        {
            const float wL = (float)((double)wetL - (double)fLoCutZL * fLoCutBc4);
            const float yL = (float)((double)fLoCutZL * (double)fLoCutBc0
                                          + (double)wL * (double)fLoCutBbc);
            fLoCutZL = wL;
            wetL = yL;

            const float wR = (float)((double)wetR - (double)fLoCutZR * fLoCutBc4);
            const float yR = (float)((double)fLoCutZR * (double)fLoCutBc0
                                          + (double)wR * (double)fLoCutBbc);
            fLoCutZR = wR;
            wetR = yR;
        }

        // ── 9. Pre-delay — original branch on param[0xC0] <= 0.5 ────────────
        //   p <= 0.5 (panel: <= 0 ms): the DRY is delayed by |p-0.5|*0.3*sr
        //                            (wet leads dry = negative pre-delay)
        //   p >  0.5 (panel: > 0 ms): the WET is delayed (classic pre-delay)
        // The ER contribution is added AFTER this stage, so it is never
        // delayed — matching FUN_0048490c exactly.
        float mixDryL = dryL;
        float mixDryR = dryR;
        if (pdN <= 0.5f)
        {
            float dL, dR;
            fPreDelay.process(dryL, dryR, dL, dR, pdLen);
            mixDryL = dL;
            mixDryR = dR;
        }
        else
        {
            float dL, dR;
            fPreDelay.process(wetL, wetR, dL, dR, pdLen);
            wetL = dL;
            wetR = dR;
        }

        // ── 10. Add early reflections: wet += er * 2 * p_cc ─────────────────
        wetL = (float)((double)erL * 2.0 * (double)erN + (double)wetL);
        wetR = (float)((double)erR * 2.0 * (double)erN + (double)wetR);

        // ── 11. Dry/wet mix, then output level ──────────────────────────────
        const float mL = (float)(((double)1.0 - (double)mix) * (double)mixDryL
                                      + (double)wetL * (double)mix);
        const float mR = (float)(((double)1.0 - (double)mix) * (double)mixDryR
                                      + (double)wetR * (double)mix);
        float oL = mL * lvl;
        float oR = mR * lvl;

#if CLASSIC_REVERB_OUTPUT_SOFT_CLIP
        oL = kSoftClipCeiling * std::tanh(oL * kSoftClipCeilingInv);
        oR = kSoftClipCeiling * std::tanh(oR * kSoftClipCeilingInv);
#endif // CLASSIC_REVERB_OUTPUT_SOFT_CLIP

        outL[i] = oL;
        outR[i] = oR;

#if CLASSIC_REVERB_SILENCE_GATE
        // Feed the gate the dry input plus the PRE-wet reverb output (taken
        // before Mix/Level so a LEVEL=0 setting cannot mask a ringing tank).
        fSilenceGate.updateInputSignal(dryL, dryR, wetL, wetR, fSampleRate, [&](){
            this->clearReverbBuffers();
        });
#endif // CLASSIC_REVERB_SILENCE_GATE
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Coefficient update — faithful transcription of FUN_004845b8
// (parameters -> coefficients/delay lengths) plus FUN_00483F24 (the fixed
// per-sample-rate filters, guarded there by the 0xE1 "sample rate changed"
// flag; recomputing them here on every call yields the same values).
// ═════════════════════════════════════════════════════════════════════════════
void ClassicReverbPlugin::updateCoefficients()
{
    const float sr = fSampleRate;
    if (sr <= 0.0f) return;
    const double dsr = (double)sr;

    // ── Damping -> comb feedback (0x4845C6) ─────────────────────────────────
    //   g = kDampCoeffB + kDampCoeffA * (1.0 - p_damping)   -> [0.40, 0.98]
    const float dampNorm = std::clamp(fParams[kParamDamping] * 0.01f, 0.0f, 1.0f);
    fRoomCoeff = (float)(kDampCoeffB + kDampCoeffA * (1.0 - (double)dampNorm));
    // sqrt(1 - g) scales the comb output sum (0x127BA8) — the original
    // computes it with FSQRT on the float32 value of g.
    fRoomCoeffSq = (float)std::sqrt((double)(1.0f - fRoomCoeff));

    // ── Level (0x4845FE): kLevelBase^(p - 0.5), p = dB/20 + 0.5 ────────────
    fLevelGain = (float)std::exp(std::log(kLevelBase)
                                 * ((double)fParams[kParamLevel] / 20.0));

    // ── Mix (param 0xD0) ────────────────────────────────────────────────────
    fMix = std::clamp(fParams[kParamMix], 0.0f, 100.0f) * 0.01f;

    // ── Early reflections (param 0xCC): the original stores the normalised
    //    parameter and computes  wet += er * 2 * p_cc  every sample.  With
    //    p_cc = 10^(dB/20) / 2 this is identical to er * 10^(dB/20). ────────
    {
        const float g = (float)std::exp(std::log(kLevelBase)
                                        * ((double)fParams[kParamEarlyRef] / 20.0));
        fEarlyRefNorm = g * 0.5f;
    }

    // ── Room scale + common delay factor (0x484624) ────────────────────────
    //   roomScale = exp(ln(32) * roomNorm) = 32^roomNorm
    //   delayFactor = (float)(0.968 * roomScale * sr)
    // ER, comb AND allpass delays all scale with room size (RE-04 only
    // scaled the combs, and missed the 0.968 factor entirely).
    // Original room-size mapping (no flag — it IS the original behaviour):
    // the panel's SIZE marks double every step across 0.625..640 m^2, so the
    // normalised parameter is linear in log2(size):
    //   roomNorm = log2(size / 0.625) / 10        (10 doublings)
    // and the delay scale is 32^roomNorm = sqrt(size / 0.625).
    float roomNorm = std::log2(fParams[kParamRoomSize] / 0.625f) / 10.0f;
    roomNorm = std::clamp(roomNorm, 0.0f, 1.0f);
    const double roomScale = std::exp(std::log(kRoomBase) * (double)roomNorm);
    const float  delayFactor = (float)(kDelayScale * roomScale * dsr);

    // ── Delay lengths: round-half-to-even (x87 FISTP with the default
    //    rounding mode; std::lrint uses the same current mode) ──────────────
    for (int t = 0; t < 7; ++t)
    {
        const int d = (int)std::lrint((double)kErDelay[t] * (double)delayFactor);
        fErDelayLen[t] = std::max(1, std::min(d, fErBufL.size - 1));
    }
    for (int c = 0; c < 16; ++c)
    {
        const int len = (int)std::lrint((double)kCombDelay[c] * (double)delayFactor);
        fCombLen[c] = std::max(1, std::min(len, fCombBuf[c].size - 1));
    }
    for (int a = 0; a < 3; ++a)
    {
        int len = (int)std::lrint((double)kApDelay[a] * (double)delayFactor);
        len = std::max(1, std::min(len, kMaxApSamples - 1));
        if (fAP[a].size != len)
        {
            // The original only rewrites the wrap length (0x8016E/72/76);
            // the ring content is NOT cleared on a parameter change.
            fAP[a].size = len;
            if (fAP[a].pos >= len) fAP[a].pos = 0;   // `if (len <= idx) idx = 0`
        }
    }

    // ── Pre-delay (0x4847C9) ────────────────────────────────────────────────
    //   len = round(|p - 0.5| * 0.3 * sr)      (0.3 s full scale = +/-150 ms)
    fPreDelayNorm = fParams[kParamPreDelay] / 300.0f + 0.5f;
    {
        const double pd = std::fabs((double)fPreDelayNorm - 0.5)
                          * (kPreDelayScale * dsr);
        const int len = (int)std::lrint(pd);
        fPreDelaySamples = std::max(0, std::min(len, kMaxPdSamples - 1));
    }

    // ── Hi-Damp strength (param 0xC4): k = 0.6 * p, applied per sample in
    //    the comb memory filter.  Kept in double because the original keeps
    //    the 0.6 * p product in an x87 register (never stored as float). ────
    fHiDampK = 0.6 * (double)std::clamp(fParams[kParamHiDamp] * 0.01f, 0.0f, 1.0f);

    // ── Lo-Cut one-pole highpass (0x4847F1 tail) ───────────────────────────
    //   angle = (float)(2*pi * f)          // stored as float32 in the DLL
    //   u     = angle * 0.5 / sr
    //   v     = cot(u) = cos(u)/sin(u)     // FCOS/FSIN + FDIVRP
    //   bbc   = v/(1+v);  bc0 = -bbc;  bc4 = (1-v)/(1+v)
    // f = clamp(Hz, 20, 1000); the original derives it as 20 * 50^p.
    {
        const double f = std::clamp((double)fParams[kParamLoCut],
                                    kLocutBaseHz, kLocutBaseHz * kLocutScale);
        const float  angle = (float)(2.0 * kPi * f);
        const double u = ((double)angle * 0.5) / dsr;
        const double v = std::cos(u) / std::sin(u);
        const double bb = v / (1.0 + v);
        fLoCutBbc = (float)bb;
        fLoCutBc0 = (float)(-bb);
        fLoCutBc4 = (float)((1.0 - v) / (1.0 + v));
    }

    // ── Fixed filters (FUN_00483F24) ────────────────────────────────────────
    {
        // Comb memory filter @ 2 kHz (one-pole LP, depth = 0.6*HiDamp)
        {
            const float  angle = (float)(2.0 * kPi * kCombFiltHz);
            const double u = ((double)angle * 0.5) / dsr;
            const double v = std::cos(u) / std::sin(u);
            const double b = v / (1.0 + v);
            fCombFiltB0 = (float)b;
            fCombFiltB1 = (float)(-b);
            fCombFiltA1 = (float)((1.0 - v) / (1.0 + v));
        }
        // ER filter A @ 8 kHz: one-pole LP, y = (z + w) * b
        {
            const float  angle = (float)(2.0 * kPi * kErAHertz);
            const double u = ((double)angle * 0.5) / dsr;
            const double v = std::cos(u) / std::sin(u);
            fErA_B  = (float)(1.0 / (1.0 + v));
            fErA_A1 = (float)((1.0 - v) / (1.0 + v));
        }
        // ER filter B @ 1 kHz, Q = 0.707: RBJ-style lowpass evaluated with
        // T = cot(u) (algebraically identical to the T = tan(u) form):
        //   b = [1, 2, 1] / a0 ; a1 = 2(1 - T^2)/a0 ; a2 = (1 - T/Q + T^2)/a0
        {
            const float  angle = (float)(2.0 * kPi * kErBHertz);
            const double u = ((double)angle * 0.5) / dsr;
            const double X = std::cos(u) / std::sin(u);
            const double Q = (double)kErBQ;
            const double X2 = std::exp(2.0 * std::log(X));  // original: exp(2*ln X)
            const double a0 = 1.0 + X / Q + X2;
            fErB_B0 = (float)(1.0 / a0);
            fErB_B1 = (float)(2.0 / a0);
            fErB_B2 = (float)(1.0 / a0);
            fErB_A1 = (float)(2.0 * (1.0 - X2) / a0);
            fErB_A2 = (float)((1.0 - X / Q + X2) / a0);
        }
    }
}
