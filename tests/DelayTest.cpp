#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaDelayProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
struct Head : juce::AudioPlayHead { juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120); return i; } };

// run an impulse through, return the output (both channels)
static juce::AudioBuffer<float> run (VegaDelayProcessor& p, int total, float impulseAmp = 1.0f, float sine = 0)
{
    const int blk = 512; juce::AudioBuffer<float> out (2, total), b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < total; pos += blk)
    {
        const int n = std::min (blk, total - pos); b.setSize (2, n, false, false, true); b.clear();
        for (int i = 0; i < n; ++i)
        {
            float x = (pos + i == 0) ? impulseAmp : 0.0f;
            if (sine > 0 && pos + i < 2400) x = 0.5f * std::sin (juce::MathConstants<float>::twoPi * sine * (pos + i) / 48000.0f);
            b.setSample (0, i, x); b.setSample (1, i, x);
        }
        p.processBlock (b, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n);
    }
    return out;
}
static int peakIndex (const juce::AudioBuffer<float>& b, int ch, int from, int to, float* val = nullptr)
{
    int best = from; float v = 0;
    for (int i = from; i < std::min (to, b.getNumSamples()); ++i) if (std::abs (b.getSample (ch, i)) > v) { v = std::abs (b.getSample (ch, i)); best = i; }
    if (val) *val = v; return best;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double fs = 48000;
    auto make = [&] { auto* p = new VegaDelayProcessor(); p->setPlayConfigDetails (2, 2, fs, 512); p->prepareToPlay (fs, 512); return std::unique_ptr<VegaDelayProcessor> (p); };
    Head head;
    {   // digital, stereo, 1/4 at 120 = 500 ms
        auto p = make(); p->setPlayHead (&head);
        set (*p, "character", 0); set (*p, "mode", 0); set (*p, "noteL", 8); set (*p, "noteR", 8); set (*p, "feedback", 50); set (*p, "mix", 50);
        set (*p, "wow", 0); set (*p, "flutter", 0); set (*p, "lowCut", 20); set (*p, "highCut", 20000); set (*p, "drive", 0);
        auto o = run (*p, 96000);
        float e1, e2; const int i1 = peakIndex (o, 0, 1000, 36000, &e1), i2 = peakIndex (o, 0, 36000, 60000, &e2);
        check (std::abs (i1 - 24000) <= 1, "1/4 note at 120 BPM lands at 500 ms (sample " + juce::String (i1) + ")");
        check (std::abs (i2 - 48000) <= 1, "second repeat at 1000 ms (sample " + juce::String (i2) + ")");
        check (std::abs (e2 / e1 - 0.5f) < 0.06f, "50% feedback halves each repeat (" + juce::String (e2 / e1, 3) + ")");
        float dry; peakIndex (o, 0, 0, 10, &dry);
        check (dry > 0.99f, "dry signal fully present at 50% mix (" + juce::String (dry, 3) + ")");
        p->setPlayHead (nullptr);
    }
    {   // ping-pong alternates sides
        auto p = make(); p->setPlayHead (&head);
        set (*p, "character", 0); set (*p, "mode", 1); set (*p, "link", 1); set (*p, "noteL", 5); set (*p, "feedback", 70); set (*p, "mix", 50);
        set (*p, "wow", 0); set (*p, "flutter", 0); set (*p, "lowCut", 20); set (*p, "highCut", 20000);
        auto o = run (*p, 48000);
        float l1, r1, l2, r2;
        peakIndex (o, 0, 11000, 13000, &l1); peakIndex (o, 1, 11000, 13000, &r1);
        peakIndex (o, 0, 23000, 25000, &l2); peakIndex (o, 1, 23000, 25000, &r2);
        check (l1 > 0.2f && r1 < 0.02f && r2 > 0.1f && l2 < 0.02f, "ping-pong: 1st echo left (" + juce::String (l1, 2) + "), 2nd echo right (" + juce::String (r2, 2) + ")");
        p->setPlayHead (nullptr);
    }
    {   // high cut darkens repeats
        auto bright = make(), dark = make();
        for (auto* p : { bright.get(), dark.get() })
        { p->setPlayHead (&head); set (*p, "character", 0); set (*p, "mode", 0); set (*p, "noteL", 2); set (*p, "noteR", 2); set (*p, "feedback", 80); set (*p, "wow", 0); set (*p, "flutter", 0); set (*p, "lowCut", 20); set (*p, "mix", 50); }
        set (*bright, "highCut", 20000); set (*dark, "highCut", 1500);
        auto ob = run (*bright, 48000, 0, 6000), od = run (*dark, 48000, 0, 6000);
        double eb = 0, ed = 0; for (int i = 20000; i < 48000; ++i) { eb += ob.getSample (0, i) * ob.getSample (0, i); ed += od.getSample (0, i) * od.getSample (0, i); }
        check (ed < eb * 0.05, "high cut removes 6 kHz from the repeats (" + juce::String (10 * std::log10 (ed / eb + 1e-12), 1) + " dB)");
        bright->setPlayHead (nullptr); dark->setPlayHead (nullptr);
    }
    {   // stability: every character at max feedback + drive stays bounded and finite
        bool ok = true; float worst = 0;
        for (int c = 0; c < 4; ++c)
        {
            auto p = make(); p->setPlayHead (&head);
            set (*p, "character", (float) c); set (*p, "feedback", 100); set (*p, "drive", 100); set (*p, "mix", 100); set (*p, "noteL", 0); set (*p, "noteR", 1);
            auto o = run (*p, 48000 * 6, 1.0f);
            for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < o.getNumSamples(); ++i) { const float v = o.getSample (ch, i); if (! std::isfinite (v)) ok = false; worst = std::max (worst, std::abs (v)); }
            p->setPlayHead (nullptr);
        }
        check (ok && worst < 8.0f, "max feedback + drive stays stable on all characters (peak " + juce::String (worst, 2) + ")");
    }
    {   // freeze holds the loop
        auto p = make(); p->setPlayHead (&head);
        set (*p, "character", 0); set (*p, "noteL", 8); set (*p, "noteR", 8); set (*p, "feedback", 50); set (*p, "wow", 0); set (*p, "flutter", 0); set (*p, "mix", 50);
        run (*p, 30000);
        set (*p, "freeze", 1);
        auto o = run (*p, 48000 * 3, 0.0f);
        float late; peakIndex (o, 0, 48000 * 2, 48000 * 3, &late);
        check (late > 0.2f, "freeze keeps the loop alive (" + juce::String (late, 2) + " after 3 s)");
        p->setPlayHead (nullptr);
    }
    {   // ducking
        auto plain = make(), ducked = make();
        for (auto* p : { plain.get(), ducked.get() }) { p->setPlayHead (&head); set (*p, "character", 0); set (*p, "noteL", 2); set (*p, "noteR", 2); set (*p, "feedback", 60); set (*p, "mix", 50); }
        set (*ducked, "duck", 100);
        auto a = run (*plain, 9000, 0, 300), b = run (*ducked, 9000, 0, 300);   // sine for 50 ms with a 125 ms delay -> echoes over the tail
        float ea, eb; peakIndex (a, 0, 6200, 8400, &ea); peakIndex (b, 0, 6200, 8400, &eb);
        check (ea > 0.1f && eb < ea * 0.7f, "ducking lowers echoes right after playing (" + juce::String (eb, 3) + " vs " + juce::String (ea, 3) + ")");
        plain->setPlayHead (nullptr); ducked->setPlayHead (nullptr);
    }
    {
        auto p = make(); set (*p, "feedback", 77); juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q->apvts.getRawParameterValue ("feedback")->load() - 77) < 0.5f, "state round trip");
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
