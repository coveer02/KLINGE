#include "Circuits.h"

namespace klinge::circuits
{
// ===========================================================================
// Koren 12AX7
// ===========================================================================
namespace
{
    constexpr double mu = 100.0, ex = 1.4, kg1 = 1060.0, kp = 600.0, kvb = 300.0;

    double softplus (double x) noexcept { return x > 30.0 ? x : std::log1p (std::exp (x)); }

    double koren (double vgk, double vpk) noexcept
    {
        if (vpk <= 0.0) return 0.0;
        const double e1 = vpk / kp * softplus (kp * (1.0 / mu + vgk / std::sqrt (kvb + vpk * vpk)));
        return e1 > 0.0 ? std::pow (e1, ex) / kg1 : 0.0;
    }
}

TriodeTable::TriodeTable()
{
    constexpr double vkNominal = 1.2;     // typical cathode voltage; the load line barely depends on it
    for (int i = 0; i <= N; ++i)
    {
        const double vgk = vMin + (vMax - vMin) * i / N;
        double lo = 0.0, hi = (Bplus - vkNominal) / Rp;
        for (int it = 0; it < 80; ++it)                      // f(Ip) = Ip - K(vgk, B - Rp Ip) is increasing
        {
            const double mid = 0.5 * (lo + hi);
            if (mid - koren (vgk, Bplus - vkNominal - Rp * mid) > 0.0) hi = mid; else lo = mid;
        }
        ip[(size_t) i] = 0.5 * (lo + hi);
    }
}

const TriodeTable& TriodeTable::get()
{
    static const TriodeTable t;
    return t;
}

double TriodeTable::plateCurrent (double vgk) const noexcept
{
    const double pos = (std::min (std::max (vgk, vMin), vMax) - vMin) / (vMax - vMin) * N;
    const int i = std::min (N - 1, (int) pos);
    const double f = pos - i;
    return ip[(size_t) i] + f * (ip[(size_t) i + 1] - ip[(size_t) i]);
}

double TriodeTable::slopeAt (double vgk) const noexcept
{
    const double h = (vMax - vMin) / N;
    return (plateCurrent (vgk + h) - plateCurrent (vgk - h)) / (2.0 * h);
}

// ===========================================================================
// Triode stage
// ===========================================================================
void TriodeStage::prepare (double sampleRate) noexcept
{
    fs = sampleRate;
    T = 1.0 / fs;
    miller.lowpass (fs, 14000.0);
    dc.setup (fs, 6.0);
    setColdBias (0.0);
    reset();
}

void TriodeStage::setDrift (double d) noexcept
{
    d = std::min (std::max (d, 0.0), 1.0);
    Rs = 220000.0 * std::pow (0.05, d);          // grid stopper: low value = strong grid current
    Cc = 22.0e-9 * (1.0 + 2.0 * d);              // longer recovery with more drift
}

void TriodeStage::setColdBias (double c) noexcept
{
    c = std::min (std::max (c, 0.0), 1.0);
    Rk = 1500.0 * (1.0 + 2.5 * c);
    G1 = 2.0 * Ck / T;
    g = 1.0 / Rk;
    c1 = 1.0 / (G1 + g);
    // quiescent point: Vk = Rk * Ip(-Vk)
    const auto& tab = TriodeTable::get();
    double vk = 1.0;
    for (int i = 0; i < 60; ++i) vk = 0.5 * vk + 0.5 * Rk * tab.plateCurrent (-vk);
    const double ipq = tab.plateCurrent (-vk);
    vpq = TriodeTable::Bplus - TriodeTable::Rp * ipq;
    const double av = std::max (tab.slopeAt (-vk) * TriodeTable::Rp, 1.0);
    norm = 1.0 / av;
    Vk = vk; Ik1 = ipq;
}

void TriodeStage::reset() noexcept
{
    Vc = 0;
    miller.reset();
    dc.reset();
    setColdBias ((Rk / 1500.0 - 1.0) / 2.5);      // restores the quiescent cathode state
}

double TriodeStage::process (double vin, double biasVolts) noexcept
{
    const auto& tab = TriodeTable::get();
    const double vgNode = miller.process ((float) vin) - Vc + biasVolts;
    const double vgk = vgNode - Vk;

    // grid conduction: the grid-cathode junction acts like a diode behind the grid stopper
    const double ig = vgk > 0.0 ? vgk / Rs : 0.0;
    const double vgkEff = vgk > 0.0 ? vgk / (1.0 + vgk * 2.0) : vgk;    // grid voltage cannot run away

    const double ip = tab.plateCurrent (vgkEff);

    // coupling capacitor charges through grid leak and grid current (explicit, well within stability)
    Vc += T / Cc * (vgNode / Rg + ig);
    Vc = std::min (std::max (Vc, -60.0), 60.0);

    // bypassed cathode (trapezoidal)
    const double ik = ip + ig;
    Vk = c1 * (Vk * (G1 - g) + ik + Ik1);
    Ik1 = ik;

    const double vp = TriodeTable::Bplus - TriodeTable::Rp * ip;
    const double out = -(vp - vpq) * norm;                      // invert back to input polarity
    return dc.process ((float) out);
}

// ===========================================================================
// Diode clipper (WDF)
// ===========================================================================
namespace
{
    // Wright omega function approximations (D'Angelo et al.)
    inline double omega3 (double x) noexcept
    {
        constexpr double x1 = -3.341459552768620, x2 = 8.0;
        constexpr double a = -1.314293149877800e-3, b = 4.775931364975583e-2, c = 3.631952663804445e-1, d = 6.313183464296682e-1;
        if (x < x1) return 0.0;
        if (x < x2) return d + x * (c + x * (b + x * a));
        return x - std::log (x);
    }
    inline double omega4 (double x) noexcept
    {
        const double y = omega3 (x);
        return y - (y - std::exp (x - y)) / (y + 1.0);
    }
}

void DiodeClipper::prepare (double sampleRate) noexcept
{
    fs = sampleRate;
    update();
    reset();
}

void DiodeClipper::setTone (double t) noexcept
{
    t = std::min (std::max (t, 0.0), 1.0);
    const double fc = 700.0 * std::pow (16000.0 / 700.0, t);
    const double newC = 1.0 / (2.0 * kPiD * R * fc);
    if (std::abs (newC - C) > 1.0e-15) { C = newC; update(); }
}

void DiodeClipper::update() noexcept
{
    const double Rc = 1.0 / (2.0 * fs * C);
    const double G1 = 1.0 / R, G2 = 1.0 / Rc;
    gamma1 = G1 / (G1 + G2);
    Rport = 1.0 / (G1 + G2);
    logRIsVt = std::log (Rport * Is / nVt);
}

double DiodeClipper::process (double vin) noexcept
{
    const double b1 = vin;          // resistive voltage source, adapted
    const double b2 = z;            // capacitor
    const double aRoot = gamma1 * b1 + (1.0 - gamma1) * b2;

    // anti-parallel diode pair at the root
    const double lambda = aRoot >= 0.0 ? 1.0 : -1.0;
    const double la = lambda * aRoot / nVt;
    const double bRoot = aRoot - 2.0 * nVt * lambda * (omega4 (logRIsVt + la) - omega4 (logRIsVt - la));

    z = bRoot + aRoot - b2;         // wave into the capacitor, reflected next sample
    if (! std::isfinite (z)) z = 0;
    return 0.5 * (aRoot + bRoot);
}

// ===========================================================================
// Jiles-Atherton hysteresis
// ===========================================================================
void Hysteresis::setWidth (double w) noexcept
{
    w = std::min (std::max (w, 0.0), 1.0);
    c = 0.9 - 0.8 * w;                    // reversibility: (1 - c) stays >= 0.1, keeps the solver well-conditioned
}

double Hysteresis::dMdH (double m, double h, double delta) const noexcept
{
    const double Q = (h + alpha * m) / a;
    double L, Lp;
    const double aq = std::abs (Q);
    if (aq < 1.0e-3)      { L = Q / 3.0;  Lp = 1.0 / 3.0 - Q * Q / 15.0; }
    else if (aq > 20.0)   { L = (Q > 0 ? 1.0 : -1.0) - 1.0 / Q; Lp = 1.0 / (Q * Q); }
    else
    {
        const double coth = 1.0 / std::tanh (Q);
        L = coth - 1.0 / Q;
        Lp = 1.0 / (Q * Q) - (coth * coth - 1.0);
    }
    const double man = Ms * L;
    const double diff = man - m;
    const double deltaM = ((delta > 0) == (diff > 0)) ? 1.0 : 0.0;
    const double num1 = (1.0 - c) * deltaM * diff / ((1.0 - c) * delta * k - alpha * diff);
    const double num2 = c * (Ms / a) * Lp;
    const double den = 1.0 - c * alpha * (Ms / a) * Lp;
    return (num1 + num2) / den;
}

double Hysteresis::process (double H) noexcept
{
    const double dHtotal = H - Hprev;
    if (std::abs (dHtotal) > 1.0e-12)
    {
        const double delta = dHtotal > 0 ? 1.0 : -1.0;
        const int steps = std::min (16, 1 + (int) (std::abs (dHtotal) / (0.25 * a)));
        const double dH = dHtotal / steps;
        double h = Hprev;
        for (int s = 0; s < steps; ++s)
        {
            const double k1 = dMdH (M, h, delta);
            const double k2 = dMdH (M + 0.5 * dH * k1, h + 0.5 * dH, delta);
            const double k3 = dMdH (M + 0.5 * dH * k2, h + 0.5 * dH, delta);
            const double k4 = dMdH (M + dH * k3, h + dH, delta);
            M += dH / 6.0 * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
            h += dH;
        }
        M = std::min (std::max (M, -1.05 * Ms), 1.05 * Ms);
        if (! std::isfinite (M)) M = 0;
        Hprev = H;
    }
    return M * (3.0 * a / Ms);            // small-signal gain ~1
}

// ===========================================================================
// Transformer
// ===========================================================================
void IronCore::prepare (double sampleRate) noexcept
{
    fs = sampleRate;
    rho = std::exp (-2.0 * kPiD * 3.0 / fs);   // flux leaks away below ~3 Hz, so DC cannot pin the core
    setCore (0.5);
    reset();
}

void IronCore::setCore (double s) noexcept
{
    s = std::min (std::max (s, 0.0), 1.0);
    const double f0 = 25.0 * std::pow (16.0, s);   // a sine at f0 swings the flux as far as its amplitude
    w = 2.0 * kPiD * f0 / fs;
}

double IronCore::process (double x, double fluxBias) noexcept
{
    phi = rho * phi + w * 0.5 * (x + x1);
    x1 = x;
    if (! std::isfinite (phi)) phi = 0;
    const double t = std::tanh (phi + fluxBias);
    const double y = x * (1.0 - 0.92 * t * t);     // secondary follows dB/dt, which collapses in saturation
    return std::tanh (0.35 * y) / 0.35;            // winding/drive limit, only reached at extreme settings
}

// ===========================================================================
// Stack amp
// ===========================================================================
void StackAmp::prepare (double sampleRate) noexcept
{
    fs = sampleRate;
    t1.prepare (fs); t2.prepare (fs);
    t1.setDrift (0.35); t1.setColdBias (0.0);
    t2.setDrift (0.7);  t2.setColdBias (0.6);
    bright.highShelf (fs, 2500.0, 4.0, 0.7);
    scoop.peak (fs, 650.0, 0.7, -3.0);
    presence.highShelf (fs, 3500.0, 2.0, 0.7);
    cab[0].highpass (fs, 85.0, 0.7);
    cab[1].peak (fs, 120.0, 1.0, 3.0);
    cab[2].peak (fs, 2800.0, 1.2, 2.0);
    cab[3].peak (fs, 4500.0, 2.0, -4.0);
    cab[4].lowpass (fs, 5200.0, 0.8);
    cab[5].lowpass (fs, 7500.0, 0.6);
    sag.setup (fs, 10.0, 150.0);
    voice = -1.0;
    setVoice (0.5);
    reset();
}

void StackAmp::setVoice (double v) noexcept
{
    v = std::min (std::max (v, 0.0), 1.0);
    if (std::abs (v - voice) < 1.0e-6) return;
    voice = v;
    tight.highpass (fs, 380.0 * std::pow (60.0 / 380.0, v));
    mid.peak (fs, 1800.0 * std::pow (800.0 / 1800.0, v), 0.8, 6.0);
}

void StackAmp::reset() noexcept
{
    tight.reset(); bright.reset(); mid.reset(); scoop.reset(); presence.reset();
    for (auto& c : cab) c.reset();
    t1.reset(); t2.reset();
    sag.reset();
}

double StackAmp::process (double vin, double biasVolts) noexcept
{
    double x = tight.process ((float) vin);
    x = t1.process (x, 0.0);
    x = bright.process ((float) x);
    x = mid.process ((float) x) * 4.0;                   // interstage gain: the second stage clips earlier
    const double s = sag.process ((float) std::abs (x));
    x = t2.process (x * 1.5 / (1.0 + 0.15 * std::min (s, 8.0)), biasVolts);
    x = scoop.process ((float) x);
    x = presence.process ((float) x);
    x = std::tanh (0.35 * x) / 0.35;                     // push-pull power stage
    for (auto& c : cab) x = c.process ((float) x);
    return x;
}

} // namespace klinge::circuits
