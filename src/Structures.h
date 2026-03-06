#ifndef CLASSIC_REVERB_STRUCTURES_H_INCLUDED
#define CLASSIC_REVERB_STRUCTURES_H_INCLUDED

#include <cstring>
#include "DistrhoDetails.hpp"   // For ParameterRanges

// ─────────────────────────────────────────────────────────────────────────────
// Parameter indices
// ─────────────────────────────────────────────────────────────────────────────
enum Parameters
{
    kParamRoomSize = 0,
    kParamDamping,
    kParamPreDelay,
    kParamHiDamp,
    kParamLoCut,
    kParamEarlyRef,
    kParamMix,
    kParamLevel,
    kParamCount
};

constexpr DISTRHO::ParameterRanges kParamRanges[kParamCount] = {
    // def, min, max
    {  80.0f, 0.625f, 640.0f   },   // Room Size, m²
    {  40.0f, 0.0f,   100.0f   },   // Damping, %
    {  0.0f,  -150.0f, 150.0f, },   // Pre-delay, ms
    {  30.0f, 0.0f,   100.0f,  },   // Hi Damping, %
    {  80.0f, 20.0f,  1000.0f, },   // Lo-Cut, Hz
    {  -6.0f, -40.0f, 6.0f,    },   // Early Reflections, dB
    {  35.0f, 0.0f,   100.0f,  },   // Mix, %
    {  0.0f,  -10.0f, 10.0f,   },   // Level, dB
};

// ─────────────────────────────────────────────────────────────────────────────
// Circular buffer helpers (fixed maximum size)
// ─────────────────────────────────────────────────────────────────────────────
// In the original DLL every delay length is  round(table * 0.968 * 32^room * sr),
// i.e. early reflections and allpasses ALSO scale with Room Size (RE-04 only
// scaled the comb delays).  Buffer sizes below are derived for 192 kHz at the
// maximum room scale (32):
//   kMaxCombSamples : ceil(0.0040067 * 0.968 * 32 * 192000) + 8  = 23 838 -> 25 000
//   kMaxErSamples   : ceil(0.0035200 * 0.968 * 32 * 192000) + 8  = 20 943 -> 21 200
//   kMaxApSamples   : ceil(0.0003175 * 0.968 * 32 * 192000) + 4  =  1 893 ->  2 000
//   kMaxPdSamples   : ceil(0.151 * 192000) + 4                   = 28 996 -> 29 200
static constexpr int kMaxCombSamples = 25000;
static constexpr int kMaxApSamples   = 2000;
static constexpr int kMaxErSamples   = 21200;
static constexpr int kMaxPdSamples   = 29200;


struct CircBuf
{
    float  buf[kMaxCombSamples];
    int    pos  = 0;
    int    size = 1;

    void reset(int n)
    {
        size = (n < 1 ? 1 : (n > kMaxCombSamples ? kMaxCombSamples : n));
        pos  = 0;
        std::memset(buf, 0, sizeof(float) * (unsigned)size);
    }

    float read(int delayInSamples) const
    {
        int idx = pos - delayInSamples;
        if (idx < 0) idx += size;
        return buf[idx];
    }

    void write(float v)
    {
        buf[pos] = v;
        if (++pos >= size) pos = 0;
    }

    // read then advance write pointer
    float readAndWrite(float v)
    {
        float out = buf[pos];
        buf[pos]  = v;
        if (++pos >= size) pos = 0;
        return out;
    }
};

struct SmallCircBuf
{
    float  buf[kMaxApSamples];
    int    pos  = 0;
    int    size = 1;

    void reset(int n)
    {
        size = (n < 1 ? 1 : (n > kMaxApSamples ? kMaxApSamples : n));
        pos  = 0;
        std::memset(buf, 0, sizeof(float) * (unsigned)size);
    }

    float readAndWrite(float v)
    {
        float out = buf[pos];
        buf[pos]  = v;
        if (++pos >= size) pos = 0;
        return out;
    }
};

// Pre-delay stores stereo pairs.
// Uses a fixed-size ring buffer (always kMaxPdSamples).  The desired delay
// length is passed per-call so that changing it mid-stream never invalidates
// the write pointer and never requires a buffer clear.
struct PreDelayBuf
{
    float bufL[kMaxPdSamples];
    float bufR[kMaxPdSamples];
    int   writePos = 0;

    void reset()
    {
        writePos = 0;
        std::memset(bufL, 0, sizeof(bufL));
        std::memset(bufR, 0, sizeof(bufR));
    }

    // Original semantics (FUN_0048490c, ring at 0x10B91C):
    //   tmp = buf[idx]; buf[idx] = in; out = tmp; idx++;
    //   if (len <= idx) idx = 0;                  // wrap AT THE DELAY LENGTH
    // So the ring holds exactly `delaySamples` slots; for delaySamples == 0
    // the index is pinned to 0 and the output is the previous sample (a
    // 1-sample delay) — reproducing the DLL's behaviour at p == 0.5 exactly.
    void process(float inL, float inR, float& outL, float& outR, int delaySamples)
    {
        outL = bufL[writePos];
        outR = bufR[writePos];
        bufL[writePos] = inL;
        bufR[writePos] = inR;
        ++writePos;
        if (delaySamples <= writePos) writePos = 0;   // `if (len <= idx) idx = 0`
    }
};

// Small ER delay buffer (mono, used per-channel)
struct ErBuf
{
    float  buf[kMaxErSamples];
    int    pos  = 0;
    int    size = 1;

    void reset(int n)
    {
        size = (n < 1 ? 1 : (n > kMaxErSamples ? kMaxErSamples : n));
        pos  = 0;
        std::memset(buf, 0, sizeof(float) * (unsigned)size);
    }

    float read(int delay) const
    {
        int idx = pos - delay;
        if (idx < 0) idx += size;
        return buf[idx];
    }

    void write(float v)
    {
        buf[pos] = v;
        if (++pos >= size) pos = 0;
    }
};

#endif // CLASSIC_REVERB_STRUCTURES_H_INCLUDED
