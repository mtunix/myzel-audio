#pragma once

#include "dsp/chain/Match.h"
#include "support/ChainGates.h"
#include "support/Loudness.h"

#include <array>
#include <cmath>

/**
    Match's measurement (PLAN.md, Match), shared by its calibration (tools/calibrate), its gate in
    InkcapMeasure and the chain suite: pink noise at -18 dBFS RMS (the same in both channels: a
    centred mix) through the real chain, the K-weighted loudness out against in (BS.1770, the
    channels summed) after a second of settling. JUCE-free.
*/
namespace inkcap::gates
{

constexpr double matchSeconds = 4.0;
constexpr double matchSettleSeconds = 1.0;      // the room fills, the crackle's first turn passes

inline const analysis::Stereo& matchProgramme (bool longer = false)
{
    static const auto pink = loudness::pinkNoise (loudness::calibrationRate, matchSeconds);
    static const auto pinkLonger = loudness::pinkNoise (loudness::calibrationRate, 3.0 * matchSeconds);
    return longer ? pinkLonger : pink;
}

inline double matchProgrammeDb (bool longer = false)
{
    static const auto db = loudness::loudnessDb (matchProgramme(), (size_t) (matchSettleSeconds * loudness::calibrationRate));
    static const auto dbLonger = loudness::loudnessDb (matchProgramme (true), (size_t) (matchSettleSeconds * loudness::calibrationRate));
    return longer ? dbLonger : db;
}

inline float fromBipolarPosition (float p) noexcept { return 2.0f * p - 1.0f; }

/** One module alone at an Amount and Damage, a type and its Match table's two controls (positions 0 .. 1). */
inline dsp::ChainSettings alone (Module m, int type, float a, float b, float amount, float damage, bool matchOn, float chaos = 0.0f)
{
    dsp::ChainSettings s;
    s.damage = damage;
    s.chaos = chaos;
    s.match = matchOn;
    s.mix = 1.0f;
    s.glitch.on = s.warble.on = s.crush.on = s.bits.on = s.hiss.on = s.room.on = false;
    switch (m)
    {
        case Module::glitch: s.glitch = { true, amount, (GlitchType) type, (int) std::lround (b * (float) (numGlitchRates - 1)), a, 0.5f }; break;
        case Module::warble: s.warble = { true, amount, (WarbleType) type, 0.5f, false, 0.0f }; break;
        case Module::crush:  s.crush = { true, amount, (CrushType) type, a, fromBipolarPosition (b) }; break;
        case Module::bits:   s.bits = { true, amount, (BitsType) type, a, b, 0.0f }; break;
        case Module::hiss:   s.hiss = { true, amount, (HissType) type, fromBipolarPosition (a), fromBipolarPosition (b) }; break;
        case Module::room:   s.room = { true, amount, (RoomType) type, a, b }; break;
        default: break;
    }
    return s;
}

/** A module's Match controls at their parameter defaults (Glitch's Rate 1/16, Room's Duck 0). */
inline dsp::match::Controls defaultControls (Module m)
{
    switch (m)
    {
        case Module::glitch: return dsp::match::controlsOf (dsp::GlitchSettings {});
        case Module::crush:  return dsp::match::controlsOf (dsp::CrushSettings {});
        case Module::bits:   return dsp::match::controlsOf (dsp::BitsSettings {});
        case Module::hiss:   return dsp::match::controlsOf (dsp::HissSettings {});
        case Module::room:   return dsp::match::controlsOf (dsp::RoomSettings {});
        case Module::warble:
        default:             return {};
    }
}

/**
    Loudness out minus in (dB) through the real chain. Glitch alone comes and goes where its seed
    puts its events: its mean over four seeds, three times as long.
*/
inline double gainDb (const dsp::ChainSettings& s)
{
    constexpr auto fs = loudness::calibrationRate;
    const auto settle = (size_t) (matchSettleSeconds * fs);
    const auto glitchOnly = s.glitch.on && ! s.crush.on && ! s.bits.on && ! s.hiss.on && ! s.room.on;
    if (! glitchOnly)
        return loudness::loudnessDb (render (s, matchProgramme(), 512, fs), settle) - matchProgrammeDb();

    double power = 0.0;
    constexpr std::array<uint32_t, 4> seeds { 0x4393'0A3Du, 0x8719'8E02u, 0x0000'0001u, 0xC0FF'EE00u };
    for (const auto seed : seeds)
    {
        auto seeded = s;
        seeded.seed = seed;
        power += std::pow (10.0, loudness::loudnessDb (render (seeded, matchProgramme (true), 512, fs), settle) / 10.0) / (double) seeds.size();
    }
    return 10.0 * std::log10 (power) - matchProgrammeDb (true);
}

} // namespace inkcap::gates
