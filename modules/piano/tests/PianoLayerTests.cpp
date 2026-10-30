// SPDX-License-Identifier: MIT
// PianoLayer behaviour: the contract (silence, release, pedal, polyphony,
// block-size independence), the sounds (levels, velocity, tone, tuning,
// stand-ins, sampled grands) and the piano bus. Ported from the original
// product's piano tests to the module API.
#include "PianoTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>

using namespace kraftonic::piano;
using namespace kraftonic::piano::test;

namespace
{
    constexpr std::array<int, 4> kModelled { 0, 1, 2, 3 };

    void prepared (PianoLayer& layer, const Parameters& p = {}, double rate = kRate, int block = kBlock)
    {
        layer.setParameters (p);
        layer.prepare (rate, block);
    }

    std::vector<Hit> triad (float velocity, int length)
    {
        return { on (60, velocity), on (64, velocity), on (67, velocity), off (60, length), off (64, length), off (67, length) };
    }

    /** Power of the left channel at `hz` over [begin, end) (Goertzel). */
    double power (const Out& o, double hz, int begin, int end)
    {
        const double w = 2.0 * kPi * hz / kRate;
        const double coefficient = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (int i = begin; i < end; ++i)
        {
            const double s0 = static_cast<double> (o.l[static_cast<std::size_t> (i)]) + coefficient * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    }

    /** Level of a normal-playing layer: a typical product output trim (-6 dB). */
    Parameters staged (Parameters p)
    {
        p.level = 0.5f;
        return p;
    }
} // namespace

TEST_CASE ("Piano layer: silent and fully overwritten with no notes", "[contract]")
{
    PianoLayer layer;
    prepared (layer);
    const auto o = run (layer, 4096, {});
    CHECK (o.peak() == 0.0f);
    CHECK (layer.activeVoiceCount() == 0);
    CHECK_FALSE (layer.isSounding());
}

TEST_CASE ("Piano layer: every model is finite and bounded at all rates and block sizes", "[contract]")
{
    for (int index = 0; index < kNumModels; ++index)
        for (double rate : { 22050.0, 44100.0, 96000.0, 384000.0 })
            for (int block : { 1, 64, 512 })
            {
                PianoLayer layer;
                prepared (layer, sound (index), rate, block);
                const int n = static_cast<int> (rate * 0.25);
                const auto o = run (layer, n, { on (21, 1.0f), on (60, 0.7f), on (108, 1.0f), off (60, n / 2) }, block);
                INFO ("model " << index << " rate " << rate << " block " << block);
                CHECK (o.finite());
                CHECK (o.peak() > 1.0e-4f);
                CHECK (o.peak() < 1.0f);
            }
}

TEST_CASE ("Piano layer: note-off releases to exact silence and frees voices", "[contract]")
{
    for (int index = 0; index < kNumModels; ++index)
    {
        PianoLayer layer;
        prepared (layer, sound (index));
        const auto o = run (layer, kRate * 4, { on (60, 0.8f), off (60, kRate / 4) });
        INFO ("model " << index);
        CHECK (o.peak (kRate * 3, kRate * 4) == 0.0f);
        CHECK (layer.activeVoiceCount() == 0);
        CHECK_FALSE (layer.isSounding());
    }
}

TEST_CASE ("Piano layer: velocity 0 note-on is a note-off", "[contract]")
{
    PianoLayer a, b;
    prepared (a);
    prepared (b);
    const auto x = run (a, kRate, { on (60, 0.8f), on (60, 0.0f, 4800) });
    const auto y = run (b, kRate, { on (60, 0.8f), off (60, 4800) });
    CHECK (x.l == y.l);
    CHECK (x.r == y.r);
}

TEST_CASE ("Piano layer: sustain pedal holds released notes until lifted", "[contract]")
{
    for (int index : kModelled)
    {
        PianoLayer layer;
        prepared (layer, sound (index));
        const auto o = run (layer, kRate * 3, { pedal (1.0f), on (60, 0.8f), off (60, 2400), pedal (0.0f, kRate) });
        INFO ("model " << index);
        CHECK (o.rms (kRate / 2, kRate) > 1.0e-3);   // held by the pedal
        CHECK (o.peak (kRate * 5 / 2, kRate * 3) < 1.0e-3f); // released after the pedal lifts
    }
}

TEST_CASE ("Piano layer: half pedal extends released model tails monotonically", "[contract]")
{
    std::array<double, 5> tailEnergy {};
    constexpr std::array<float, 5> depths { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    for (std::size_t d = 0; d < depths.size(); ++d)
    {
        PianoLayer layer;
        auto p = sound (1);
        p.release = 0.15f;
        prepared (layer, p);
        std::vector<float> l (kRate / 2), r (l.size());
        layer.noteOn (60, 0.8f);
        layer.process (l.data(), r.data(), kRate / 10);
        layer.setPedalDepth (depths[d]);
        layer.noteOff (60);
        layer.process (l.data(), r.data(), static_cast<int> (l.size()));
        for (int i = kRate / 10; i < kRate / 5; ++i)
            tailEnergy[d] += static_cast<double> (l[static_cast<std::size_t> (i)]) * l[static_cast<std::size_t> (i)];
    }
    for (std::size_t i = 1; i < tailEnergy.size(); ++i)
        CHECK (tailEnergy[i] >= tailEnergy[i - 1]);
}

TEST_CASE ("Piano layer: half pedal depth sweeps remain finite", "[realtime]")
{
    PianoLayer layer;
    prepared (layer, sound (1));
    std::array<float, 256> l {}, r {};
    layer.noteOn (60, 0.8f);
    for (int block = 0; block < 100; ++block)
    {
        const float depth = static_cast<float> (block % 20) / 19.0f;
        layer.setPedalDepth (depth);
        layer.process (l.data(), r.data(), static_cast<int> (l.size()));
        for (float x : l) CHECK (std::isfinite (x));
    }
}

TEST_CASE ("Piano layer: polyphony is capped at kMaxVoices", "[contract]")
{
    PianoLayer layer;
    prepared (layer);
    std::vector<Hit> hits;
    for (int i = 0; i < 24; ++i)
        hits.push_back (on (36 + 2 * i, 0.7f, i));
    run (layer, kRate / 2, hits);
    CHECK (layer.activeVoiceCount() == kMaxVoices);
}

TEST_CASE ("Piano layer: reset and allNotesOff", "[contract]")
{
    PianoLayer layer;
    prepared (layer);
    auto o = run (layer, kRate, { on (60, 0.8f), resetAt (4800) });
    CHECK (o.peak (4800, kRate) == 0.0f);
    o = run (layer, kRate * 4, { pedal (1.0f), on (60, 0.8f), on (64, 0.8f), allNotesOff (4800) });
    CHECK (o.peak (kRate * 3, kRate * 4) == 0.0f);
    CHECK (layer.activeVoiceCount() == 0);
}

TEST_CASE ("Piano layer: calls before prepare are safe and silent", "[contract]")
{
    PianoLayer layer;
    layer.reset();
    layer.setParameters (sound (3));
    layer.noteOn (60, 1.0f);
    layer.sustain (1.0f);
    layer.noteOff (60);
    layer.allNotesOff();
    std::vector<float> l (64, 0.5f), r (64, 0.5f);
    layer.process (l.data(), r.data(), 64);
    CHECK (layer.activeVoiceCount() == 0);
    CHECK_FALSE (layer.isSounding());
    prepared (layer, sound (3));
    CHECK (run (layer, 4800, {}).peak() == 0.0f);
}

TEST_CASE ("Piano layer: output is independent of block size and of the process split", "[contract]")
{
    GrandFolder folder;
    Parameters p = sound (2);
    p.dynamicCompression = true;
    p.doubling = true;
    p.stringResonance = 0.6f;
    p.pedalNoise = 0.7f;
    Parameters later = sound (4, p);
    later.tone = 0.9f;
    later.level = 0.6f;
    const std::vector<Hit> hits { pedal (1.0f, 100), on (48, 0.9f, 300), on (60, 0.5f, 1000), change (later, 7000),
                                  on (64, 0.8f, 9000), off (48, 12000), pedal (0.0f, 20000), off (60, 21000),
                                  off (64, 23000), on (72, 1.0f, 24000) };
    std::vector<Out> outs;
    for (int block : { 1, 37, 256, 4096 })
    {
        PianoLayer layer;
        layer.setGrandBank (PianoLayer::loadGrandBank (folder.str()));
        prepared (layer, p, kRate, 256);
        outs.push_back (run (layer, kRate, hits, block));
    }
    for (std::size_t i = 1; i < outs.size(); ++i)
    {
        CHECK (outs[i].l == outs[0].l);
        CHECK (outs[i].r == outs[0].r);
    }

    // process() is renderVoices() followed by processBus().
    PianoLayer split;
    split.setGrandBank (PianoLayer::loadGrandBank (folder.str()));
    prepared (split, p);
    std::vector<float> l (kBlock), r (kBlock);
    split.noteOn (60, 0.9f);
    split.sustain (1.0f);
    PianoLayer whole;
    whole.setGrandBank (PianoLayer::loadGrandBank (folder.str()));
    prepared (whole, p);
    std::vector<float> wl (kBlock), wr (kBlock);
    whole.noteOn (60, 0.9f);
    whole.sustain (1.0f);
    for (int b = 0; b < 40; ++b)
    {
        split.renderVoices (l.data(), r.data(), kBlock);
        split.processBus (l.data(), r.data(), kBlock);
        whole.process (wl.data(), wr.data(), kBlock);
        REQUIRE (l == wl);
        REQUIRE (r == wr);
    }
}

TEST_CASE ("Piano layer: calls longer than the prepared block size render", "[contract]")
{
    PianoLayer a, b;
    prepared (a, sound (2), kRate, 64);
    prepared (b, sound (2), kRate, 4096);
    a.noteOn (60, 0.8f);
    b.noteOn (60, 0.8f);
    std::vector<float> al (4096), ar (4096), bl (4096), br (4096);
    a.process (al.data(), ar.data(), 4096);
    b.process (bl.data(), br.data(), 4096);
    CHECK (al == bl);
    CHECK (ar == br);
}

TEST_CASE ("Piano layer: every model gets louder and brighter with velocity", "[dsp]")
{
    for (int index : kModelled)
    {
        PianoLayer soft, hard;
        prepared (soft, sound (index));
        prepared (hard, sound (index));
        const auto s = run (soft, kRate / 2, { on (60, 0.25f) });
        const auto h = run (hard, kRate / 2, { on (60, 1.0f) });
        INFO ("model " << index);
        CHECK (h.rms (480, kRate / 4) > 2.0 * s.rms (480, kRate / 4));
        CHECK (h.brightness (480, kRate / 4) > s.brightness (480, kRate / 4));
    }
}

TEST_CASE ("Piano layer: tone and timbre brighten every model", "[dsp]")
{
    for (int index : kModelled)
    {
        Parameters dark = sound (index), bright = sound (index);
        dark.tone = 0.1f;
        bright.tone = 0.9f;
        Parameters soft = sound (index), brightTimbre = sound (index);
        soft.timbre = Timbre::soft;
        brightTimbre.timbre = Timbre::bright;
        PianoLayer a, b, c, d;
        prepared (a, dark);
        prepared (b, bright);
        prepared (c, soft);
        prepared (d, brightTimbre);
        INFO ("model " << index);
        CHECK (run (b, kRate / 2, { on (60, 0.8f) }).brightness (480, kRate / 4)
               > run (a, kRate / 2, { on (60, 0.8f) }).brightness (480, kRate / 4));
        CHECK (run (d, kRate / 2, { on (60, 0.8f) }).brightness (480, kRate / 4)
               > run (c, kRate / 2, { on (60, 0.8f) }).brightness (480, kRate / 4));
    }
}

TEST_CASE ("Piano layer: zero velocity sensitivity makes velocity level identical", "[dsp]")
{
    Parameters p;
    p.velocitySensitivity = 0.0f;
    PianoLayer a, b;
    prepared (a, p);
    prepared (b, p);
    const auto x = run (a, kRate / 2, { on (60, 0.2f) });
    const auto y = run (b, kRate / 2, { on (60, 1.0f) });
    CHECK (x.l == y.l);
}

TEST_CASE ("Piano layer: touch curves change the level of the same velocity", "[dsp]")
{
    Parameters light, heavy, fixed;
    light.touch = Touch::light;
    heavy.touch = Touch::heavy;
    fixed.touch = Touch::fixed;
    PianoLayer a, b, c, d;
    prepared (a, light);
    prepared (b, heavy);
    prepared (c, fixed);
    prepared (d, fixed);
    CHECK (run (a, kRate / 2, { on (60, 0.5f) }).rms() > 1.5 * run (b, kRate / 2, { on (60, 0.5f) }).rms());
    // Fixed: every velocity plays alike.
    CHECK (run (c, kRate / 2, { on (60, 0.2f) }).l == run (d, kRate / 2, { on (60, 1.0f) }).l);
}

TEST_CASE ("Piano layer: piano models are in tune", "[dsp]")
{
    for (int index : kModelled)
        for (int note : { 36, 60, 84 })
        {
            PianoLayer layer;
            prepared (layer, sound (index));
            const auto o = run (layer, kRate, { on (note, 0.7f) });
            const double hz = 440.0 * std::exp2 ((note - 69) / 12.0);
            double best = 0.0, bestCents = 0.0;
            for (double cents = -30.0; cents <= 30.0; cents += 1.0)
            {
                const double pw = power (o, hz * std::exp2 (cents / 1200.0), 4800, 4800 + 32768);
                if (pw > best)
                {
                    best = pw;
                    bestCents = cents;
                }
            }
            INFO ("model " << index << " note " << note << ": " << bestCents << " cents");
            CHECK (std::abs (bestCents) <= 4.0);
        }
}

TEST_CASE ("Piano layer: models balance with the Tine EP and keep headroom", "[dsp]")
{
    // Measured at a typical product output trim: the layer itself has no peak
    // protector, the product adds its own (for example -6 dB and a soft knee).
    PianoLayer tine;
    prepared (tine, staged (sound (0)));
    const auto tineOut = run (tine, kRate / 2, triad (100.0f / 127.0f, kRate / 2));
    const double reference = tineOut.rms (960, 19200);
    for (int index : kModelled)
    {
        PianoLayer layer;
        prepared (layer, staged (sound (index)));
        const auto o = run (layer, kRate / 2, triad (100.0f / 127.0f, kRate / 2));
        const double db = 20.0 * std::log10 (o.rms (960, 19200) / reference);
        INFO ("model " << index << " " << db << " dB against the Tine EP, peak " << o.peak());
        // Within 5 dB RMS; the models' higher crest factor sets their trims.
        CHECK (std::abs (db) < 5.0);
        CHECK (o.peak() < 0.708f); // -3 dBFS
    }
    // A ten-note fortissimo chord keeps 3 dB of headroom on every model
    // (the Tine EP's own limit is its velocity curve; products test it whole).
    for (int index : { 1, 2, 3 })
    {
        PianoLayer layer;
        prepared (layer, staged (sound (index)));
        std::vector<Hit> hits;
        for (int key : { 48, 52, 55, 59, 62, 64, 67, 71, 74, 76 })
            hits.push_back (on (key, 1.0f));
        INFO ("model " << index);
        CHECK (run (layer, kRate, hits).peak() < 0.708f);
    }
}

TEST_CASE ("Piano layer: switching the sound leaves held notes on the old sound", "[dsp]")
{
    PianoLayer a, b;
    prepared (a, sound (1));
    prepared (b, sound (1));
    const auto x = run (a, kRate / 2, { on (60, 0.8f) });
    const auto y = run (b, kRate / 2, { on (60, 0.8f), change (sound (3), 2400) });
    CHECK (x.l == y.l);
    CHECK (x.r == y.r);
}

TEST_CASE ("Piano layer: stealing a voice across sounds never clicks", "[dsp]")
{
    // 16 held notes on one sound; the 17th, on another sound, steals a slot.
    // The stolen slot's last sample fades out inside the new voice over
    // 5 ms, so the boundary is as smooth as the music around it.
    for (int from = 0; from < kNumModels; ++from)
        for (int to = 0; to < kNumModels; ++to)
        {
            INFO ("from " << kModels[static_cast<std::size_t> (from)].name << " to "
                          << kModels[static_cast<std::size_t> (to)].name);
            PianoLayer layer;
            prepared (layer, sound (from));
            for (int i = 0; i < kMaxVoices; ++i)
                layer.noteOn (48 + i, 0.6f);
            std::array<float, kBlock> before {}, after {}, right {};
            for (int block = 0; block < 9; ++block)
                layer.process (before.data(), right.data(), kBlock);
            layer.setParameters (sound (to));
            layer.noteOn (80, 0.8f);
            layer.process (after.data(), right.data(), kBlock);
            const float boundary = std::abs (after[0] - before[kBlock - 1]);
            float neighborhood = 0.0f;
            for (int i = kBlock - 32; i < kBlock; ++i)
                neighborhood = std::max (neighborhood, std::abs (before[static_cast<std::size_t> (i)]
                    - before[static_cast<std::size_t> (i - 1)]));
            REQUIRE (boundary < neighborhood * 1.5f + 0.001f);
            REQUIRE (layer.activeVoiceCount() == kMaxVoices);
        }
}

TEST_CASE ("Piano layer: missing grand samples play a quieter stand-in and are flagged", "[dsp]")
{
    PianoLayer missing;
    prepared (missing, sound (4));
    CHECK_FALSE (missing.hasGrand());
    CHECK (missing.samplesMissing());
    const auto standIn = run (missing, kRate / 2, triad (0.8f, kRate / 2));
    CHECK (standIn.rms() > 1.0e-4);

    GrandFolder folder;
    PianoLayer sampled;
    sampled.setGrandBank (PianoLayer::loadGrandBank (folder.str()));
    prepared (sampled, sound (4));
    CHECK (sampled.hasGrand());
    CHECK_FALSE (sampled.samplesMissing());
    const auto played = run (sampled, kRate / 2, triad (0.8f, kRate / 2));
    CHECK (played.rms() > 1.0e-3);
    CHECK (played.l != standIn.l);

    // A modelled sound never reports missing samples, and a bad folder loads nothing.
    PianoLayer modelled;
    prepared (modelled, sound (2));
    CHECK_FALSE (modelled.samplesMissing());
    CHECK (PianoLayer::loadGrandBank ("") == nullptr);
    CHECK (PianoLayer::loadGrandBank ((folder.root / "nowhere").string()) == nullptr);
}

TEST_CASE ("Piano layer: Harpsichord, Vibes and Marimba play their own folder, or a quieter stand-in", "[dsp]")
{
    CHECK (PianoLayer::modelFolder (4) == nullptr);
    CHECK (PianoLayer::modelFolder (10) == nullptr);
    CHECK (PianoLayer::loadModelBank ("", 11) == nullptr);
    for (int model = 11; model < kNumModels; ++model)
    {
        INFO ("model " << kModels[static_cast<std::size_t> (model)].name);
        REQUIRE (kModels[static_cast<std::size_t> (model)].source == Source::sampled);
        REQUIRE (PianoLayer::modelFolder (model) != nullptr);
        PianoLayer missing;
        prepared (missing, sound (model));
        CHECK (missing.samplesMissing());
        const auto standIn = run (missing, kRate / 2, triad (0.8f, kRate / 2));
        CHECK (standIn.rms() > 1.0e-4);

        // The grand's recordings are not this model's: still missing.
        GrandFolder grand;
        PianoLayer grandOnly;
        grandOnly.setGrandBank (PianoLayer::loadGrandBank (grand.str()));
        prepared (grandOnly, sound (model));
        CHECK (grandOnly.samplesMissing());

        GrandFolder folder (PianoLayer::modelFolder (model));
        CHECK (PianoLayer::loadModelBank (folder.str(), model) != nullptr);
        PianoLayer sampled;
        sampled.setModelBank (model, PianoLayer::loadModelBank (folder.str(), model));
        prepared (sampled, sound (model));
        CHECK_FALSE (sampled.samplesMissing());
        const auto played = run (sampled, kRate / 2, triad (0.8f, kRate / 2));
        CHECK (played.rms() > 1.0e-3);
        CHECK (played.l != standIn.l);
        CHECK (played.finite());

        // Block-size independent.
        const std::vector<Hit> hits { on (60, 0.8f), on (64, 0.6f, 4000), off (60, 9000), on (67, 0.9f, 12001),
                                      off (64, 20000), off (67, 30011) };
        PianoLayer a, b;
        for (auto* layer : { &a, &b })
            layer->setModelBank (model, PianoLayer::loadModelBank (folder.str(), model));
        prepared (a, sound (model), kRate, 64);
        prepared (b, sound (model), kRate, 1024);
        const auto small = run (a, kRate, hits, 64);
        const auto large = run (b, kRate, hits, 1024);
        CHECK (small.l == large.l);
        CHECK (small.r == large.r);
    }
}

TEST_CASE ("Piano layer: the sampled grand follows the pedal and releases, in any block size", "[dsp]")
{
    GrandFolder folder;
    auto play = [&folder] (const std::vector<Hit>& hits, int block)
    {
        PianoLayer layer;
        layer.setGrandBank (PianoLayer::loadGrandBank (folder.str()));
        prepared (layer, sound (5), kRate, kBlock);
        auto o = run (layer, kRate * 2, hits, block);
        return std::make_pair (o, layer.activeVoiceCount());
    };
    const auto [held, heldVoices] = play ({ pedal (1.0f), on (60, 0.8f), off (60, 2400) }, kBlock);
    CHECK (held.rms (kRate, kRate * 2) > 1.0e-3); // the 2 s test sample ends by itself
    (void) heldVoices;
    const auto [released, releasedVoices] = play ({ on (60, 0.8f), off (60, 2400) }, kBlock);
    CHECK (released.peak (kRate, kRate * 2) < 1.0e-4f);
    CHECK (releasedVoices == 0);
    const auto [other, otherVoices] = play ({ pedal (1.0f), on (60, 0.8f), off (60, 2400) }, 37);
    CHECK (other.l == held.l);
    (void) otherVoices;
}

TEST_CASE ("Piano layer: Studio Grand is wider than Bright Grand", "[dsp]")
{
    GrandFolder folder;
    auto width = [&folder] (int index)
    {
        PianoLayer layer;
        layer.setGrandBank (PianoLayer::loadGrandBank (folder.str()));
        prepared (layer, sound (index));
        const auto o = run (layer, kRate / 2, { on (60, 0.8f) });
        double side = 0.0, mid = 0.0;
        for (std::size_t i = 0; i < o.l.size(); ++i)
        {
            side += (o.l[i] - o.r[i]) * (o.l[i] - o.r[i]);
            mid += (o.l[i] + o.r[i]) * (o.l[i] + o.r[i]);
        }
        return side / mid;
    };
    CHECK (width (5) > width (4));
}

TEST_CASE ("Piano bus: soft release shortens the tail", "[dsp]")
{
    Parameters normal, soft;
    normal.release = 1.0f;
    soft = normal;
    soft.softRelease = true;
    PianoLayer a, b;
    prepared (a, normal);
    prepared (b, soft);
    const auto x = run (a, kRate * 2, { on (60, 0.8f), off (60, kRate / 4) });
    const auto y = run (b, kRate * 2, { on (60, 0.8f), off (60, kRate / 4) });
    CHECK (y.rms (kRate / 2, kRate) < 0.5 * x.rms (kRate / 2, kRate));
}

TEST_CASE ("Piano bus: dynamic compression lowers a hot chord's peak without silencing it", "[dsp]")
{
    Parameters dry = sound (2), comp = sound (2);
    comp.dynamicCompression = true;
    std::vector<Hit> hits;
    for (int key : { 48, 55, 60, 64, 67, 72 })
        hits.push_back (on (key, 1.0f));
    PianoLayer a, b;
    prepared (a, dry);
    prepared (b, comp);
    const auto x = run (a, kRate / 2, hits);
    const auto y = run (b, kRate / 2, hits);
    CHECK (y.peak() < 0.9f * x.peak());
    CHECK (y.rms() > 0.3 * x.rms());
}

TEST_CASE ("Piano bus: every feature off is bit-exact and each feature changes the sound", "[dsp]")
{
    const std::vector<Hit> hits { pedal (1.0f, 0), on (48, 0.9f), on (60, 0.8f), off (48, 9600), off (60, 9600),
                                  pedal (0.0f, kRate / 2) };
    PianoLayer dryLayer;
    prepared (dryLayer, sound (2));
    const auto dry = run (dryLayer, kRate, hits);
    // Turning features on and back off leaves the bus exact once they settle.
    PianoLayer toggled;
    Parameters on2 = sound (2);
    on2.doubling = true;
    on2.dynamicCompression = true;
    prepared (toggled, sound (2));
    auto hits2 = hits;
    hits2.push_back (change (on2, 0));
    hits2.push_back (change (sound (2), 10));
    const auto back = run (toggled, kRate, hits2);
    // Once the doubling mix has faded to exactly zero (about 90 ms).
    CHECK (std::vector<float> (back.l.begin() + 9600, back.l.end()) == std::vector<float> (dry.l.begin() + 9600, dry.l.end()));
    CHECK (std::vector<float> (back.r.begin() + 9600, back.r.end()) == std::vector<float> (dry.r.begin() + 9600, dry.r.end()));

    auto differs = [&] (Parameters p)
    {
        PianoLayer layer;
        prepared (layer, p);
        const auto o = run (layer, kRate, hits);
        CHECK (o.finite());
        CHECK (o.peak() < 2.0f * dry.peak());
        return o.l != dry.l || o.r != dry.r;
    };
    Parameters p = sound (2);
    p.doubling = true;
    CHECK (differs (p));
    p = sound (2);
    p.stringResonance = 0.8f;
    CHECK (differs (p));
    p = sound (2);
    p.pedalNoise = 0.8f;
    CHECK (differs (p));
}

TEST_CASE ("Piano bus: string resonance with the pedal down lengthens the bloom", "[dsp]")
{
    Parameters dry;
    dry.release = 1.0f;
    Parameters reso = dry;
    reso.stringResonance = 0.8f;
    const std::vector<Hit> hits { pedal (1.0f), on (60, 0.85f), on (64, 0.85f), on (67, 0.85f), off (60, kRate / 2),
                                  off (64, kRate / 2), off (67, kRate / 2), pedal (0.0f, kRate) };
    PianoLayer a, b;
    prepared (a, dry);
    prepared (b, reso);
    // After the pedal lifts both release; the tank keeps blooming behind the dry tail.
    const double dryTail = run (a, kRate * 3, hits).rms (kRate * 3 / 2, kRate * 2);
    const double resoTail = run (b, kRate * 3, hits).rms (kRate * 3 / 2, kRate * 2);
    INFO ("dry tail=" << dryTail << " reso tail=" << resoTail);
    CHECK (dryTail > 1.0e-5);
    CHECK (resoTail > dryTail * 1.5);
}

TEST_CASE ("Piano bus: a pedal press with no notes is heard only with pedal noise on", "[dsp]")
{
    Parameters quiet, noisy;
    noisy.pedalNoise = 1.0f;
    PianoLayer a, b;
    prepared (a, quiet);
    prepared (b, noisy);
    CHECK (run (a, kRate / 2, { pedal (1.0f, 100), pedal (0.0f, 12000) }).peak() == 0.0f);
    const auto o = run (b, kRate / 2, { pedal (1.0f, 100), pedal (0.0f, 12000) });
    CHECK (o.peak (100, 4000) > 1.0e-3f);
    CHECK (o.peak (12000, 16000) > 1.0e-3f);
    CHECK (o.peak() < 1.0f);
    // A half-pedal below 0.5 keeps the dampers down: no noise, notes release.
    PianoLayer c;
    prepared (c, noisy);
    CHECK (run (c, kRate / 2, { pedal (0.4f, 100) }).peak() == 0.0f);
}

TEST_CASE ("Piano layer: level scales the output smoothly", "[dsp]")
{
    Parameters half;
    half.level = 0.5f;
    PianoLayer a, b;
    prepared (a);
    prepared (b, half);
    const auto x = run (a, kRate / 2, { on (60, 0.8f) });
    const auto y = run (b, kRate / 2, { on (60, 0.8f) });
    for (std::size_t i = 0; i < x.l.size(); ++i)
        REQUIRE (y.l[i] == 0.5f * x.l[i]);
    // A level change ramps over 20 ms instead of jumping.
    PianoLayer c;
    prepared (c);
    Parameters silent;
    silent.level = 0.0f;
    const auto z = run (c, kRate / 2, { on (60, 0.8f), change (silent, 4800) });
    CHECK (z.maxStep (4800, 4810) < 0.05f);
    CHECK (z.peak (4800 + 960, kRate / 2) == 0.0f);
}

TEST_CASE ("Piano layer: octave shifts notes and releases the note it started", "[dsp]")
{
    Parameters up;
    up.octave = 1;
    PianoLayer a, b;
    prepared (a);
    prepared (b, up);
    const auto x = run (a, kRate, { on (60, 0.8f), off (60, 9600) });
    const auto y = run (b, kRate, { on (48, 0.8f), change (Parameters {}, 4800), off (48, 9600) });
    CHECK (x.l == y.l);
    CHECK (b.activeVoiceCount() == 0);
    // Out-of-range notes are ignored.
    Parameters top;
    top.octave = 3;
    PianoLayer c;
    prepared (c, top);
    CHECK (run (c, 4800, { on (100, 0.8f) }).peak() == 0.0f);
}

TEST_CASE ("Piano layer: invalid parameters and events stay finite", "[dsp]")
{
    constexpr float nan = std::numeric_limits<float>::quiet_NaN();
    constexpr float inf = std::numeric_limits<float>::infinity();
    Parameters p;
    p.tone = nan;
    p.strike = inf;
    p.velocitySensitivity = -inf;
    p.attack = nan;
    p.decay = -1.0f;
    p.sustain = 7.0f;
    p.release = inf;
    p.stringResonance = nan;
    p.pedalNoise = 9.0f;
    p.level = nan;
    p.octave = 99;
    p.model = 99;
    p.type = static_cast<Type> (200);
    p.touch = static_cast<Touch> (200);
    p.timbre = static_cast<Timbre> (200);
    PianoLayer layer;
    prepared (layer, p);
    const auto o = run (layer, kRate, { on (60, nan), on (-5, 1.0f), on (200, 1.0f), on (64, 2.0f), pedal (nan),
                                        pedal (1.0f, 100), off (300, 200), off (64, 4800) });
    CHECK (o.finite());
    CHECK (o.peak() < 1.0f);
    // Every model at its parameter extremes.
    for (int index = 0; index < kNumModels; ++index)
        for (float extreme : { 0.0f, 1.0f })
        {
            Parameters q = sound (index);
            q.tone = q.strike = q.velocitySensitivity = q.sustain = extreme;
            q.attack = extreme > 0.5f ? 0.5f : 0.001f;
            q.decay = extreme > 0.5f ? 20.0f : 0.2f;
            q.release = extreme > 0.5f ? 5.0f : 0.01f;
            q.timbre = extreme > 0.5f ? Timbre::bright : Timbre::soft;
            q.stringResonance = q.pedalNoise = extreme;
            q.dynamicCompression = q.doubling = extreme > 0.5f;
            q.level = 0.5f;
            PianoLayer l2;
            prepared (l2, q);
            std::vector<Hit> hits { pedal (1.0f) };
            for (int key : { 36, 48, 60, 64, 67, 72, 84 })
                hits.push_back (on (key, 0.8f));
            const auto e = run (l2, kRate, hits);
            INFO ("model " << index << " extreme " << extreme << " peak " << e.peak());
            CHECK (e.finite());
            // Extremes (velocity sensitivity 0 plays every note at full
            // strike) may pass 0 dBFS by a few dB: the product's peak
            // protector catches that (for example a soft knee above -1 dBFS).
            CHECK (e.peak() < 1.5f);
        }
}

TEST_CASE ("Piano sounds: the catalogue maps types and models both ways", "[contract]")
{
    // Appended later: Vibes and Marimba (electric), Harpsichord (clav).
    CHECK (modelCount (Type::electric) == 9);
    CHECK (modelCount (Type::clav) == 3);
    CHECK (modelCount (Type::grand) == 2);
    for (int i = 0; i < kNumModels; ++i)
        CHECK (modelIndex (kModels[static_cast<std::size_t> (i)].type, modelWithinType (i)) == i);
    CHECK (modelIndex (Type::grand, 7) == 5);
    CHECK (modelIndex (Type::electric, -1) == 0);
}

TEST_CASE ("Piano sounds: original models render as they did before the appended sounds", "[dsp]")
{
    // Piano null test. Every original model (Tine, Reed and
    // Electric Grand, Clav, both grand stand-ins) at 44.1 and 96 kHz, with
    // Tone/Bell at 0, 0.5 and 1 and the bus (Dyn Comp, doubling, resonance)
    // off and on: an 8-note chord, note-offs, pedal, a re-strike and
    // all-notes-off. The goldens were rendered from abed51b (before the
    // appended models) and matched this tree bit for bit, 72/72.
    struct Golden
    {
        std::uint64_t hash;
        double rms;
    };
    // clang-format off
    constexpr std::array<Golden, 72> kBefore {{
    { 0x355bb0dcc91e6087ULL, 0.175413283 },
    { 0xbbf5a49ac2eefeb3ULL, 0.17202426 },
    { 0x32a763db29f09219ULL, 0.140075959 },
    { 0x2c9ed193512272a3ULL, 0.0927960658 },
    { 0xf1c0ad0a0dcef7a2ULL, 0.121054579 },
    { 0x979642f48c6bf5f3ULL, 0.0923807696 },
    { 0xf5e1b0681bee9332ULL, 0.135122521 },
    { 0x5e5071fc033dfe8dULL, 0.142708804 },
    { 0x458d684b91ff5574ULL, 0.156691202 },
    { 0x175c5d1018d36d15ULL, 0.0783228133 },
    { 0xd2c412705c684567ULL, 0.0803883433 },
    { 0x3f4f660fb007acc9ULL, 0.0851955125 },
    { 0x09129653ed9252c7ULL, 0.0650948237 },
    { 0xf67b43b4ed430fecULL, 0.0850824862 },
    { 0x82ed986fe18a8f35ULL, 0.174633718 },
    { 0x2c276a6d04fb432bULL, 0.0443142873 },
    { 0x01a706e44ec1f6c4ULL, 0.0530937866 },
    { 0x82d4f96349ee5052ULL, 0.0889988547 },
    { 0x251ca761cfd0f254ULL, 0.079840993 },
    { 0xed2336dcc0740771ULL, 0.0911547014 },
    { 0x8a3b3bf577ad6211ULL, 0.0917021374 },
    { 0x67206a2493a1285cULL, 0.0509329348 },
    { 0xcbde0497bbae88beULL, 0.0549359653 },
    { 0x0b70e88aa731a822ULL, 0.0551276089 },
    { 0xdd66934be987cfe2ULL, 0.0391857781 },
    { 0x674accb4486e78d9ULL, 0.0461132608 },
    { 0x4313fb35d5df98eeULL, 0.0749197993 },
    { 0xc3e52f269fdda8ceULL, 0.0326448493 },
    { 0xc9f4b083031e2ee2ULL, 0.0362802036 },
    { 0x64add15a595661d1ULL, 0.0504422446 },
    { 0x864044b8d3887b22ULL, 0.0513151564 },
    { 0x70657ae5913cef3cULL, 0.053611099 },
    { 0xeb072fdc79bd7ff4ULL, 0.0657714942 },
    { 0x17a1633fcc7e0873ULL, 0.0402081185 },
    { 0xf391ba1ff0a2eb03ULL, 0.0409820296 },
    { 0x21a5022ddf9f5f5dULL, 0.0464046462 },
    { 0x71f005426a4d396fULL, 0.146107354 },
    { 0xa9185a4d393fba19ULL, 0.13781747 },
    { 0xf5ef40efd7c85dcfULL, 0.117172845 },
    { 0xf1630f2847f90369ULL, 0.0747685851 },
    { 0xca68d1abe1e50cb7ULL, 0.0908442438 },
    { 0x34cd45e19fd232bfULL, 0.0720404668 },
    { 0x67d1aeed229d2d1cULL, 0.117942556 },
    { 0xacad5b2c64aa0f61ULL, 0.125481436 },
    { 0x80815a71f75dadb9ULL, 0.13983884 },
    { 0x200c2a4fc7d96246ULL, 0.0640801567 },
    { 0x9506734943230743ULL, 0.06652563 },
    { 0xd8322812cdc7e511ULL, 0.0710095892 },
    { 0xa8d7029bf6815881ULL, 0.0627846449 },
    { 0xfe87ca75686454caULL, 0.0825333035 },
    { 0x73098d9db01e28c7ULL, 0.170988024 },
    { 0xd8a24fdb1a352044ULL, 0.0411687945 },
    { 0x3c1291efde78b489ULL, 0.0493768915 },
    { 0xeacabe4c1dac80a9ULL, 0.0840360602 },
    { 0x570de47296f005d1ULL, 0.0744031533 },
    { 0x8bf1c5a7ee4ba959ULL, 0.0884227392 },
    { 0x1e7651b021812e9fULL, 0.0907000844 },
    { 0x62aff1a348d9b54bULL, 0.0440838431 },
    { 0x54756434a92e086aULL, 0.0491697065 },
    { 0x500128677b4042d1ULL, 0.0501464658 },
    { 0x6d5da65ff8185cefULL, 0.0365737638 },
    { 0x869d13a03517b0a1ULL, 0.0433016648 },
    { 0xea17610ab0082eecULL, 0.0710008134 },
    { 0xfdc93a76402287acULL, 0.0294803377 },
    { 0x880f4d10a0a0a203ULL, 0.0327231808 },
    { 0x01b69296d0550e17ULL, 0.0447015232 },
    { 0x5cb056316325297eULL, 0.0474145996 },
    { 0xcd37a230c9b15a49ULL, 0.0497957363 },
    { 0x9c9dd55a3f0f451cULL, 0.0615415079 },
    { 0xb125a055a76a459bULL, 0.0352400861 },
    { 0x9b9424f57295a9a3ULL, 0.0361013353 },
    { 0x03796028cf6a844dULL, 0.0407944033 }
    }};
    // clang-format on
    struct Model
    {
        Type type;
        int model;
    };
    constexpr std::array<Model, 6> kPianoModels { { { Type::electric, 0 }, { Type::electric, 1 }, { Type::electric, 2 },
                                                    { Type::clav, 0 }, { Type::grand, 0 }, { Type::grand, 1 } } };
    std::size_t index = 0;
    for (double rate : { 44100.0, 96000.0 })
        for (const auto& m : kPianoModels)
            for (bool busOn : { false, true })
                for (float tone : { 0.0f, 0.5f, 1.0f })
                {
                    Parameters p;
                    p.type = m.type;
                    p.model = m.model;
                    p.tone = tone;
                    p.strike = tone;
                    p.dynamicCompression = busOn;
                    p.doubling = busOn;
                    p.stringResonance = busOn ? 0.6f : 0.0f;
                    PianoLayer layer;
                    layer.setParameters (p);
                    layer.prepare (rate, 512);
                    std::uint64_t hash = 1469598103934665603ULL;
                    double energy = 0.0;
                    std::vector<float> l (300), r (300);
                    const int blocks = static_cast<int> (rate * 2.0) / 300;
                    for (int block = 0; block < blocks; ++block)
                    {
                        if (block == 1)
                            for (int key : { 36, 48, 60, 64, 67, 72, 84, 96 })
                                layer.noteOn (key, 0.3f + 0.08f * static_cast<float> (key % 9));
                        if (block == 100)
                        {
                            layer.noteOff (60);
                            layer.noteOff (36);
                        }
                        if (block == 140)
                            layer.sustain (1.0f);
                        if (block == 150)
                            layer.noteOn (60, 1.0f);
                        if (block == 220)
                            layer.allNotesOff();
                        layer.process (l.data(), r.data(), 300);
                        for (std::size_t i = 0; i < l.size(); ++i)
                            for (float x : { l[i], r[i] })
                            {
                                std::uint32_t bits = 0;
                                std::memcpy (&bits, &x, sizeof bits);
                                hash = (hash ^ bits) * 1099511628211ULL;
                                energy += static_cast<double> (x) * x;
                            }
                    }
                    const double rms = std::sqrt (energy / (2.0 * blocks * 300));
                    const auto& before = kBefore[index++];
                    INFO ("rate " << rate << " type " << static_cast<int> (m.type) << " model " << m.model << " bus "
                                  << busOn << " tone " << tone << ": rms " << rms << " (before " << before.rms << ")");
                    // Every toolchain: the level is unchanged to 0.01 dB.
                    REQUIRE (std::abs (rms - before.rms) <= 1.0e-3 * before.rms);
#if defined(__APPLE__) && defined(__aarch64__) && defined(__clang__) && defined(NDEBUG)
                    // Exact bits are toolchain-specific (libm, FMA contraction);
                    // the goldens were captured with Apple Clang, arm64, Release.
                    REQUIRE (hash == before.hash);
#else
                    (void) hash;
#endif
                }
}
