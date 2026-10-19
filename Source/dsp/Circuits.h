#pragma once

#include "Primitives.h"
#include <array>
#include <cstdint>

// ---------------------------------------------------------------------------
// Stateful circuit models. Unlike the memoryless curves in Shapers.h these have
// internal energy storage (capacitors, magnetisation, flux), so how they distort
// depends on frequency and on what happened just before. All run at the
// oversampled rate and work in double precision internally.
// ---------------------------------------------------------------------------
namespace klinge::circuits
{
// ---------------------------------------------------------------------------
// 12AX7 plate current on a 250 V / 100 k load line, as a function of grid-cathode
// voltage. Koren's triode equations are solved once at startup (bisection), so the
// audio path only interpolates a table.
// ---------------------------------------------------------------------------
class TriodeTable
{
public:
    static const TriodeTable& get();
    double plateCurrent (double vgk) const noexcept;
    double slopeAt (double vgk) const noexcept;    // dIp/dVgk

    static constexpr double Bplus = 250.0, Rp = 100000.0;
    static constexpr double vMin = -12.0, vMax = 2.0;

private:
    TriodeTable();
    static constexpr int N = 4096;
    std::array<double, N + 1> ip {};
};

// Common-cathode triode stage with coupling capacitor, grid conduction and a
// bypassed cathode resistor. Grid current charges the coupling capacitor on
// loud peaks, which shifts the bias colder until it leaks away again: the
// "blooming" of a real amp.
struct TriodeStage
{
    void prepare (double fs) noexcept;
    void setDrift (double d) noexcept;       // 0 = stiff, 1 = strong bias shift
    void setColdBias (double c) noexcept;    // 0 = normal, 1 = cold (more asymmetric)
    void reset() noexcept;
    // vin in volts at the grid, returns normalised output (small-signal gain ~1, inverted back)
    double process (double vin, double biasVolts) noexcept;

    double fs = 48000.0, T = 1.0 / 48000.0;
    double Rk = 1500.0, Ck = 22.0e-6, Cc = 22.0e-9, Rg = 1.0e6, Rs = 68000.0;
    double c1 = 0, G1 = 0, g = 0;
    double Vc = 0, Vk = 0, Ik1 = 0, vpq = 0, norm = 1.0;
    OnePole miller;
    DCBlocker dc;
};

// ---------------------------------------------------------------------------
// Diode clipper: source resistor into a capacitor parallel to an anti-parallel
// diode pair, solved as a wave digital filter with the Wright-omega closed form.
// Above the RC corner the capacitor takes over, so lows clip harder than highs.
// ---------------------------------------------------------------------------
struct DiodeClipper
{
    void prepare (double fs) noexcept;
    void setTone (double t) noexcept;        // 0 = dark corner, 1 = bright corner
    void reset() noexcept { z = 0; }
    double process (double vin) noexcept;

    double fs = 48000.0, R = 2200.0, C = 47.0e-9;
    double gamma1 = 0.5, Rport = 1000.0, logRIsVt = 0.0;
    double z = 0;
    static constexpr double Is = 2.52e-9, nVt = 1.752 * 0.02585;
    void update() noexcept;
};

// ---------------------------------------------------------------------------
// Jiles-Atherton magnetic hysteresis, integrated over the field H with RK4
// (sample-rate independent: the step is the change in H, not time).
// ---------------------------------------------------------------------------
struct Hysteresis
{
    void setWidth (double w) noexcept;       // 0 = thin loop, 1 = wide loop
    void reset() noexcept { M = 0; Hprev = 0; }
    double process (double H) noexcept;

    double Ms = 1.0, a = 0.3, alpha = 1.6e-3, k = 0.25, c = 0.5;
    double M = 0, Hprev = 0;
private:
    double dMdH (double m, double h, double delta) const noexcept;
};

// ---------------------------------------------------------------------------
// Transformer core: flux is the integral of the voltage, so low frequencies
// drive the core into saturation first. The secondary voltage follows dB/dt,
// which collapses when the core saturates.
// ---------------------------------------------------------------------------
struct IronCore
{
    void prepare (double fs) noexcept;
    void setCore (double s) noexcept;        // 0 = big core (only deep lows), 1 = small core
    void reset() noexcept { phi = 0; x1 = 0; }
    double process (double x, double fluxBias) noexcept;

    double fs = 48000.0, w = 0.0, rho = 1.0, phi = 0, x1 = 0;
};

// ---------------------------------------------------------------------------
// Two-stage amp: tight filter, triode, bright voicing, mid push, second (colder)
// triode, tone stack, power-supply sag, push-pull power stage and cabinet.
// ---------------------------------------------------------------------------
struct StackAmp
{
    void prepare (double fs) noexcept;
    void setVoice (double v) noexcept;       // 0 = tight and bright, 1 = loose and dark
    void reset() noexcept;
    double process (double vin, double biasVolts) noexcept;

    double fs = 48000.0, voice = -1.0;
    OnePole tight;
    Biquad bright, mid, scoop, presence;
    std::array<Biquad, 6> cab;
    TriodeStage t1, t2;
    EnvFollower sag;
};

} // namespace klinge::circuits
