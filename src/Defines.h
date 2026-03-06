#ifndef CLASSIC_REVERB_DEFINES_H_INCLUDED
#define CLASSIC_REVERB_DEFINES_H_INCLUDED

// ─────────────────────────────────────────────────────────────────────────────
// Constants extracted from the original Classic Reverb.dll (PE32, 2013).
//
// All numeric tables below are written as C++ hex float literals so that they
// round-trip bit-exactly to the float32 values stored in the DLL's data
// section (0x488CF0 ...).  Scalar constants marked [f80] were stored as
// x87 80-bit extended values inside the code section and are reproduced here
// as doubles (the closest IEEE type on every target; the original's 64-bit
// mantissa rounds to 53 bits, a ~1e-17 relative difference that vanishes
// after the final float32 store).
//
// Verified against the machine code of FUN_004845b8 (parameter -> coefficient)
// and FUN_0048490c (per-sample DSP core), plus FUN_00483F24 (fixed filters).
// ─────────────────────────────────────────────────────────────────────────────

// ── FUN_004845b8 scalars ─────────────────────────────────────────────────────
// g = DAMP_B + DAMP_A * (1 - dampingNorm)          [f80 @ 0x4848A8 / 0x4848B4]
static constexpr double kDampCoeffA = 0x1.28f5c28f5c28fp-1;   // 0.58
static constexpr double kDampCoeffB = 0x1.999999999999ap-2;   // 0.40
// Delay-time scale: delayFactor = kDelayScale * 32^roomNorm * sr
// [f80 @ 0x4848DC] — RE-04 previously missed the 0.968 factor entirely.
static constexpr double kDelayScale  = 0x1.ef9db22d0e560p-1;  // 0.968
// Room-size base: roomScale = exp(ln(32) * roomNorm) = 32^roomNorm [f80 @0x4848D0]
static constexpr double kRoomBase    = 0x1.0p+5;              // 32
// Pre-delay length = round(|p - 0.5| * kPreDelayScale * sr), p in [0,1]
// covering the panel's -150..+150 ms  =>  0.3 s full scale   [f80 @0x4848E8]
static constexpr double kPreDelayScale = 0x1.3333333333333p-2; // 0.3
// Level gain = 10^(p - 0.5), p = dB/20 + 0.5  =>  10^(dB/20)  [f80 @0x4848C0]
static constexpr double kLevelBase   = 0x1.4p+3;               // 10

// ── ProcessSamples (FUN_0048490c) scalars ────────────────────────────────────
// Input dither: noise = kModAmp * (rand - 0.5), one sample per frame, the
// same value added to both channels.  [f80 @ 0x485F5C, code section]
static constexpr double kModAmp       = 0x1.01b2b29a4692bp-24; // 6e-8
// Schroeder allpass coefficient  [f80 @ 0x485F68]
static constexpr double kAllpassCoeff = 0x1.3333333333333p-1;  // 0.6
// Global comb-input term (literal in FUN_0048490c):
//   combIn = apOut - (sumY * 0.125) * g

// ── RandExt (0x402C08): state = state * 0x08088405 + 1; rand = state * 2^-32
static constexpr unsigned kRngMul   = 0x08088405u;
static constexpr double   kRngScale = 0x1.0p-32;   // double @ 0x487068

// ── Fixed filter frequencies (FUN_00483F24; 2*pi*f immediates) ───────────────
static constexpr double kCombFiltHz = 2000.0;  // 0x4644597C = 4000*pi
static constexpr double kErAHertz   = 8000.0;  // 0x4744597C = 16000*pi
static constexpr double kErBHertz   = 1000.0;  // 0x45C4597C = 2000*pi
static constexpr float  kErBQ       = 0.707f;  // 0x3F34FDF4 (literal 0.707)
// Lo-Cut: f = kLocutBaseHz * kLocutScale^p  (0x484900 = 40*pi = 2*pi*20,
// 0x4848F4 = 50)  =>  20 Hz .. 1000 Hz across p in [0,1], matching the panel.
static constexpr double kLocutBaseHz = 20.0;
static constexpr double kLocutScale  = 50.0;

// ── Delay tables (DLL data section, float32) ─────────────────────────────────

// Early-reflection tap delays (seconds) — 0x00488CF0, float32[7]
static constexpr float kErDelay[7] = {
    0x1.38fffc0000000p-11f, 0x1.5f45e00000000p-10f, 0x1.a60d460000000p-10f,
    0x1.1b1d920000000p-9f,  0x1.41205c0000000p-9f,  0x1.719f800000000p-9f,
    0x1.cd5f9a0000000p-9f
};
// Early-reflection tap gains — 0x00488D0C, float32[7] (shared by L and R)
static constexpr float kErGain[7] = {
    0x1.4ccccc0000000p-1f, 0x1.cccccc0000000p-2f, 0x1.a3d70a0000000p-2f,
    0x1.5c28f60000000p-2f, 0x1.3d70a40000000p-2f, 0x1.1eb8520000000p-2f,
    0x1.eb851e0000000p-3f
};

// Comb filter base delay times (seconds) — 0x00488D28, float32[16]
static constexpr float kCombDelay[16] = {
    0x1.12358c0000000p-9f, 0x1.1e595c0000000p-9f, 0x1.2b07140000000p-9f,
    0x1.3844140000000p-9f, 0x1.4617660000000p-9f, 0x1.54876c0000000p-9f,
    0x1.639ada0000000p-9f, 0x1.7359680000000p-9f, 0x1.83ca7a0000000p-9f,
    0x1.94f5700000000p-9f, 0x1.a6e35a0000000p-9f, 0x1.b99c9a0000000p-9f,
    0x1.cd29ea0000000p-9f, 0x1.e194b00000000p-9f, 0x1.f6e6fc0000000p-9f,
    0x1.0695700000000p-8f
};

// Input-diffusion allpass base delay times (seconds) — 0x00488D68, float32[3]
static constexpr float kApDelay[3] = {
    0x1.a36e2e0000000p-13f, 0x1.0839700000000p-12f, 0x1.4ce6e40000000p-12f
};

// Comb output mix weights — 0x00488D74, interleaved L,R float32[32]
static constexpr float kCombMixL[16] = {
    0x1.96872c0000000p-3f,  -0x1.f9db220000000p-4f,  0x1.34bc6a0000000p-2f,
   -0x1.0ef34e0000000p-3f,   0x1.54fdf40000000p-3f,   0x1.5dcc640000000p-2f,
    0x1.de9e1c0000000p-2f,  -0x1.15e9e20000000p-4f,  -0x1.80d1b80000000p-3f,
    0x1.7b2fec0000000p-2f,   0x1.7f8ca80000000p-6f,  -0x1.9205bc0000000p-2f,
    0x1.34d6a20000000p-3f,   0x1.c3c9ee0000000p-3f,  -0x1.56abde0000000p-4f,
   -0x1.02f8380000000p-2f
};
static constexpr float kCombMixR[16] = {
    0x1.6f00680000000p-3f,   0x1.65aee60000000p-2f,   0x1.2f1aa00000000p-2f,
   -0x1.9b3d080000000p-6f,  -0x1.5d4e900000000p-4f,  -0x1.e809d40000000p-3f,
    0x1.6b851e0000000p-2f,   0x1.7e0dee0000000p-2f,   0x1.8ce7040000000p-5f,
   -0x1.7d6b660000000p-6f,  -0x1.6a161e0000000p-2f,   0x1.2dfa440000000p-2f,
   -0x1.18d25e0000000p-5f,   0x1.4af4f00000000p-2f,  -0x1.0dd2f20000000p-2f,
   -0x1.9134040000000p-3f
};

// ─────────────────────────────────────────────────────────────────────────────
// Feature flags
// ─────────────────────────────────────────────────────────────────────────────
// RE-01's DSP follows the original binary unconditionally: the linear
// damping curve (g = 0.40 + 0.58 * (1 - dampingNorm)) and the logarithmic
// room-size mapping (roomNorm = log2(size/0.625)/10, i.e. the panel's
// doubling SIZE marks) are the original formulas and are no longer
// toggleable — RE-04 keeps those switches for its variant sound.
//
// Only two deliberate improvements over the original remain as flags,
// and both are enabled:

// Level protection: tanh soft clip at +5 dBFS.  The original has no output
// limiter, so hot material can trip the DAW's overload protection and mute
// the track; the clip bounds the output while staying transparent at normal
// levels (slope 1 at 0, knee around 0 dBFS).
#define CLASSIC_REVERB_OUTPUT_SOFT_CLIP 1

// Silence gate: once both the dry input and the pre-wet tank stay below the
// gate threshold for 0.5 s, skip processing and clear the tank — saves CPU
// on idle tracks.  The original always processes every sample.
#define CLASSIC_REVERB_SILENCE_GATE 1

// The original's input dither (kModAmp = 6e-8, same value on both channels,
// added before the ER write and the allpass/comb network) — matches
// FUN_0048490c exactly, so keep it enabled.
#define CLASSIC_REVERB_INPUT_NOISE_MODULATION 1

// Soft-clip ceiling in linear scale (= +5 dBFS), used only when enabled.
static constexpr float kSoftClipCeiling = 1.77827941f;
static constexpr float kSoftClipCeilingInv = 1.0f / kSoftClipCeiling;

#endif // CLASSIC_REVERB_DEFINES_H_INCLUDED
