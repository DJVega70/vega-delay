#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kSync, kNoteL, kNoteR, kMsL, kMsR, kLink, kMode, kChar, kFeedback, kLowCut, kHighCut, kDrive, kWow, kFlutter, kDuck, kDuckRel, kWidth, kMix, kOut, kFreeze };
const char* ids[] = { "sync", "noteL", "noteR", "msL", "msR", "link", "mode", "character", "feedback", "lowCut", "highCut", "drive", "wow", "flutter", "duck", "duckRelease", "width", "mix", "output", "freeze" };
}

juce::StringArray VegaDelayProcessor::noteNames() { return { "1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2", "1/2D", "1 Bar" }; }
double VegaDelayProcessor::noteBeats (int i)
{
    static const double b[] { 0.125, 1.0 / 6, 0.25, 0.375, 1.0 / 3, 0.5, 0.75, 2.0 / 3, 1, 1.5, 4.0 / 3, 2, 3, 4 };
    return b[juce::jlimit (0, 13, i)];
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaDelayProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return String (roundToInt (x)) + "%"; };
    auto ms = [] (float x, int) { return x < 100 ? String (x, 1) + " ms" : String (roundToInt (x)) + " ms"; };
    auto hz = [] (float x, int) { return x >= 1000 ? String (x / 1000, 1) + " kHz" : String (roundToInt (x)) + " Hz"; };
    auto db = [] (float x, int) { return String (x, 1) + " dB"; };
    auto F = [&] (int id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (f))); };
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kSync], 1 }, "Sync", true));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kNoteL], 1 }, "Time L", noteNames(), 6));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kNoteR], 1 }, "Time R", noteNames(), 8));
    F (kMsL, "Time L (ms)", NormalisableRange<float> (1, 2000, 0, 0.4f), 375, ms);
    F (kMsR, "Time R (ms)", NormalisableRange<float> (1, 2000, 0, 0.4f), 500, ms);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kLink], 1 }, "Link L/R", false));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kMode], 1 }, "Mode", StringArray { "Stereo", "Ping-Pong", "Mono" }, 1));
    v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[kChar], 1 }, "Character", StringArray { "Digital", "Analog", "Tape", "Lo-Fi" }, 2));
    F (kFeedback, "Feedback", NormalisableRange<float> (0, 100), 45, pct);
    F (kLowCut, "Low Cut", NormalisableRange<float> (20, 2000, 0, 0.35f), 180, hz);
    F (kHighCut, "High Cut", NormalisableRange<float> (1000, 20000, 0, 0.35f), 7500, hz);
    F (kDrive, "Drive", NormalisableRange<float> (0, 100), 25, pct);
    F (kWow, "Wow", NormalisableRange<float> (0, 100), 20, pct);
    F (kFlutter, "Flutter", NormalisableRange<float> (0, 100), 15, pct);
    F (kDuck, "Ducking", NormalisableRange<float> (0, 100), 0, pct);
    F (kDuckRel, "Duck Release", NormalisableRange<float> (20, 1000, 0, 0.4f), 250, ms);
    F (kWidth, "Width", NormalisableRange<float> (0, 200), 100, pct);
    F (kMix, "Mix", NormalisableRange<float> (0, 100), 25, pct);
    F (kOut, "Output", NormalisableRange<float> (-24, 12), 0, db);
    v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[kFreeze], 1 }, "Freeze", false));
    return { v.begin(), v.end() };
}

VegaDelayProcessor::VegaDelayProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < 20; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

bool VegaDelayProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet(), i = l.getMainInputChannelSet();
    return (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono()) && (i == o || (i == juce::AudioChannelSet::mono() && o == juce::AudioChannelSet::stereo()));
}

void VegaDelayProcessor::prepareToPlay (double s, int)
{
    sr = s;
    for (auto& l : line) l.init ((int) (s * 4.2) + 8);
    for (int c = 0; c < 2; ++c) { dSmooth[c] = -1; lpState[c] = hpState[c] = toneLp[c] = holdV[c] = noiseLp[c] = 0; holdCnt[c] = 0; }
    env = 0;
}

void VegaDelayProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n == 0 || outCh == 0) return;
    if (inCh == 1 && outCh == 2) buffer.copyFrom (1, 0, buffer, 0, 0, n);

    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
    const bool sync = p[kSync]->load() > 0.5f, link = p[kLink]->load() > 0.5f;
    float tMs[2];
    for (int c = 0; c < 2; ++c)
    {
        const int src = link ? 0 : c;
        tMs[c] = sync ? (float) (noteBeats ((int) p[src == 0 ? kNoteL : kNoteR]->load()) * 60000.0 / bpm) : p[src == 0 ? kMsL : kMsR]->load();
        tMs[c] = juce::jlimit (1.0f, 4000.0f, tMs[c]);
    }
    uiTimeL.store (tMs[0]); uiTimeR.store (tMs[1]);

    const int mode = (int) p[kMode]->load(), character = (int) p[kChar]->load();
    const bool freeze = p[kFreeze]->load() > 0.5f;
    float fb = freeze ? 1.0f : std::min (0.995f, p[kFeedback]->load() * 0.01f);
    const float drive = p[kDrive]->load() * 0.01f;
    const float wow = p[kWow]->load() * 0.01f, flut = p[kFlutter]->load() * 0.01f;
    const float duck = p[kDuck]->load() * 0.01f;
    const float duckRel = std::exp (-1.0f / (float) (sr * p[kDuckRel]->load() * 0.001));
    const float width = p[kWidth]->load() * 0.01f, mix = p[kMix]->load() * 0.01f, outG = juce::Decibels::decibelsToGain (p[kOut]->load());
    float hc = p[kHighCut]->load();
    if (character == 1) hc = std::min (hc, 6500.0f); else if (character == 3) hc = std::min (hc, 3800.0f);
    const float lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * std::min (hc, (float) sr * 0.45f) / (float) sr);
    const float hpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * p[kLowCut]->load() / (float) sr);
    const float glide = character == 0 ? 1.0f - std::exp (-1.0f / (float) (sr * 0.03)) : 1.0f - std::exp (-1.0f / (float) (sr * 0.25));   // tape slews (pitchy), digital quickly
    const float modScale = character == 2 ? 1.0f : character == 1 ? 0.45f : character == 3 ? 0.7f : 0.15f;
    const float wowInc = juce::MathConstants<float>::twoPi * 0.55f / (float) sr, flutInc = juce::MathConstants<float>::twoPi * 6.3f / (float) sr;
    const float holdN = character == 3 ? std::max (1.0f, (float) sr / 11025.0f) : 1.0f;

    auto sat = [character, drive] (float x)
    {
        const float g = 1.0f + drive * 3.0f;
        switch (character)
        {
            case 1: return std::tanh (x * g) / g;                                         // analog (BBD): soft, symmetric
            case 2: return std::tanh (x * g + 0.12f * drive * x * x) / g;                 // tape: asymmetric (even harmonics)
            case 3: { const float q = 1.0f / (48.0f - drive * 40.0f); return std::round (std::tanh (x * g) / g / q) * q; }   // lo-fi: crushed
            default: return x / (1.0f + std::abs (x) * drive * 0.5f);                    // digital: clean unless driven
        }
    };

    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (outCh > 1 ? 1 : 0);
    float pin[2] {}, pout[2] {}, minDuck = 1;
    for (int i = 0; i < n; ++i)
    {
        const float dryL = L[i], dryR = outCh > 1 ? R[i] : L[i];
        pin[0] = std::max (pin[0], std::abs (dryL)); pin[1] = std::max (pin[1], std::abs (dryR));

        // modulation (ms)
        wowPh += wowInc; if (wowPh > juce::MathConstants<float>::twoPi) wowPh -= juce::MathConstants<float>::twoPi;
        flutPh += flutInc; if (flutPh > juce::MathConstants<float>::twoPi) flutPh -= juce::MathConstants<float>::twoPi;
        const float rnd = rng.nextFloat() * 2.0f - 1.0f;

        float wet[2];
        for (int c = 0; c < 2; ++c)
        {
            const float target = tMs[c] * 0.001f * (float) sr;
            if (dSmooth[c] < 0) dSmooth[c] = target;
            dSmooth[c] += glide * (target - dSmooth[c]);
            noiseLp[c] += 0.002f * (rnd - noiseLp[c]);
            const float modMs = modScale * (wow * 2.2f * std::sin (wowPh + c * 0.9f) + flut * (0.25f * std::sin (flutPh + c * 1.7f) + 6.0f * noiseLp[c]));
            const float d = juce::jlimit (2.0f, (float) line[c].buf.size() - 4.0f, dSmooth[c] + modMs * 0.001f * (float) sr);
            wet[c] = line[c].read (d);
        }

        // feedback path: tone + saturation
        float fbs[2];
        for (int c = 0; c < 2; ++c)
        {
            float y = wet[c];
            if (! freeze)
            {
                lpState[c] += lpA * (y - lpState[c]); y = lpState[c];
                hpState[c] += hpA * (y - hpState[c]); y -= hpState[c];
                y = sat (y);
                if (character == 3)
                {
                    if (--holdCnt[c] <= 0) { holdV[c] = y; holdCnt[c] = (int) holdN; }
                    y = holdV[c];
                }
            }
            fbs[c] = y * fb;
        }

        const float inMono = 0.5f * (dryL + dryR), inGain = freeze ? 0.0f : 1.0f;
        if (mode == 1)            // ping-pong: input enters left, repeats bounce
        {
            line[0].push (juce::jlimit (-4.0f, 4.0f, inMono * inGain + fbs[1]));
            line[1].push (juce::jlimit (-4.0f, 4.0f, fbs[0]));
        }
        else if (mode == 2)       // mono
        {
            line[0].push (juce::jlimit (-4.0f, 4.0f, inMono * inGain + fbs[0]));
            line[1].push (juce::jlimit (-4.0f, 4.0f, inMono * inGain + fbs[0]));
            wet[1] = wet[0];
        }
        else
        {
            line[0].push (juce::jlimit (-4.0f, 4.0f, dryL * inGain + fbs[0]));
            line[1].push (juce::jlimit (-4.0f, 4.0f, dryR * inGain + fbs[1]));
        }

        // wet output tone (repeats are coloured even on the first echo)
        float wl = wet[0], wr = wet[1];
        if (character != 0)
        {
            toneLp[0] += lpA * (wl - toneLp[0]); toneLp[1] += lpA * (wr - toneLp[1]);
            wl = 0.5f * (wl + toneLp[0]); wr = 0.5f * (wr + toneLp[1]);
        }
        // width
        const float m = 0.5f * (wl + wr), s = 0.5f * (wl - wr) * width;
        wl = m + s; wr = m - s;
        // ducking: echoes drop under the dry signal
        const float lvl = std::max (std::abs (dryL), std::abs (dryR));
        env = lvl > env ? lvl : env * duckRel;
        const float dg = 1.0f / (1.0f + duck * 12.0f * env);
        minDuck = std::min (minDuck, dg);
        wl *= dg; wr *= dg;

        // keep the dry fully present at moderate mix (delay is usually used as an insert)
        const float dryKeep = std::min (1.0f, 2.0f * (1.0f - mix));
        const float fL = (dryL * dryKeep + wl * std::min (1.0f, 2.0f * mix)) * outG, fR = (dryR * dryKeep + wr * std::min (1.0f, 2.0f * mix)) * outG;
        L[i] = fL; if (outCh > 1) R[i] = fR;
        pout[0] = std::max (pout[0], std::abs (fL)); pout[1] = std::max (pout[1], std::abs (fR));
    }
    inL.store (std::max (inL.load(), pin[0])); inR.store (std::max (inR.load(), pin[1]));
    outL.store (std::max (outL.load(), pout[0])); outR.store (std::max (outR.load(), pout[1]));
    duckGain.store (minDuck);
}

void VegaDelayProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaDelayProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        apvts.replaceState (s);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaDelayProcessor(); }
