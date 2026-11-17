#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class VegaDelayProcessor : public juce::AudioProcessor
{
public:
    VegaDelayProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static juce::StringArray noteNames();
    static double noteBeats (int);
    juce::AudioProcessorValueTreeState apvts;

    // UI feedback
    std::atomic<float> uiTimeL { 500 }, uiTimeR { 500 }, inL { 0 }, inR { 0 }, outL { 0 }, outR { 0 }, duckGain { 1 };
    std::atomic<float> uiScale { 1.0f };
    double currentBpm() const { return bpm; }

private:
    struct Line
    {
        std::vector<float> buf; int w = 0;
        void init (int n) { buf.assign ((size_t) n, 0.0f); w = 0; }
        void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
        float read (float d) const       // d samples behind the last write, Hermite interpolation
        {
            const int n = (int) buf.size();
            float pos = (float) w - d; while (pos < 0) pos += (float) n;
            const int i = (int) pos; const float f = pos - (float) i;
            auto at = [&] (int k) { k %= n; if (k < 0) k += n; return buf[(size_t) k]; };
            const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            return ((c3 * f + c2) * f + c1) * f + y1;
        }
    };
    Line line[2];
    double sr = 48000, bpm = 120;
    float dSmooth[2] { 24000, 24000 };
    float lpState[2] {}, hpState[2] {}, toneLp[2] {}, holdV[2] {}; int holdCnt[2] {};
    float wowPh = 0, flutPh = 0, env = 0, noiseLp[2] {};
    juce::Random rng;
    std::atomic<float>* p[20] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaDelayProcessor)
};
