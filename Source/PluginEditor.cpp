#include "PluginEditor.h"

namespace
{
// Echo display: shows where each repeat lands and how fast it fades
class EchoView : public juce::Component
{
public:
    explicit EchoView (VegaDelayProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto& a = proc.apvts;
        auto r = getLocalBounds().toFloat().reduced (14.0f, 10.0f);
        auto text = r.removeFromTop (20.0f);
        const bool sync = a.getRawParameterValue ("sync")->load() > 0.5f, link = a.getRawParameterValue ("link")->load() > 0.5f;
        auto timeOf = [&] (bool left)
        {
            const bool useL = left || link;
            return sync ? (float) (VegaDelayProcessor::noteBeats ((int) a.getRawParameterValue (useL ? "noteL" : "noteR")->load()) * 60000.0 / proc.currentBpm())
                        : a.getRawParameterValue (useL ? "msL" : "msR")->load();
        };
        const float tL = timeOf (true), tR = timeOf (false);
        const int mode = (int) a.getRawParameterValue ("mode")->load();
        const float fb = a.getRawParameterValue ("freeze")->load() > 0.5f ? 1.0f : a.getRawParameterValue ("feedback")->load() * 0.01f;
        auto names = VegaDelayProcessor::noteNames();
        auto lbl = [&] (int c, float ms)
        {
            const int idx = (int) a.getRawParameterValue (c == 0 || link ? "noteL" : "noteR")->load();
            return (sync ? names[idx] + "  " : juce::String()) + juce::String (juce::roundToInt (ms)) + " ms";
        };
        g.setFont (VegaFonts::mono (12.0f));
        g.setColour (VegaColours::accent);
        g.drawText (mode == 2 ? "MONO  " + lbl (0, tL) : "L  " + lbl (0, tL) + "     R  " + lbl (1, tR), text, juce::Justification::centredLeft);
        g.setColour (VegaColours::textDim);
        g.drawText (juce::String (proc.currentBpm(), 1) + " BPM", text, juce::Justification::centredRight);

        const float span = std::max (1000.0f, std::max (tL, tR) * 8.0f);
        r.removeFromTop (6.0f);
        const float mid = r.getCentreY();
        g.setColour (juce::Colour (0xff262626)); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
        for (int k = 0; k <= 8; ++k) { const float x = r.getX() + r.getWidth() * k / 8; g.drawVerticalLine ((int) x, r.getY(), r.getBottom()); }
        // dry hit
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillRect (r.getX() - 1, r.getY(), 3.0f, r.getHeight());
        auto tap = [&] (float t, float amp, bool top)
        {
            if (t > span || amp < 0.01f) return;
            const float x = r.getX() + r.getWidth() * t / span, h = (r.getHeight() * 0.5f - 4) * amp;
            g.setColour (VegaColours::accent.withAlpha (0.25f + 0.75f * amp));
            if (top) g.fillRect (x - 1.5f, mid - h, 3.0f, h); else g.fillRect (x - 1.5f, mid, 3.0f, h);
        };
        if (mode == 1)
        {   // ping-pong: L, R, L, R ... each time = previous + that side's time
            float t = 0, amp = 1;
            for (int k = 0; k < 64; ++k) { const bool left = k % 2 == 0; t += left ? tL : tR; tap (t, amp, left); amp *= fb; }
        }
        else
        {
            float amp = 1; for (int k = 1; k < 64; ++k) { tap (tL * k, amp, true); amp *= fb; }
            amp = 1;       for (int k = 1; k < 64; ++k) { tap ((mode == 2 ? tL : tR) * k, amp, false); amp *= fb; }
        }
        g.setFont (VegaFonts::ui (9.5f)); g.setColour (VegaColours::textDim.withAlpha (0.6f));
        g.drawText ("L", (int) r.getX() + 4, (int) r.getY(), 12, 12, juce::Justification::left);
        g.drawText ("R", (int) r.getX() + 4, (int) r.getBottom() - 12, 12, 12, juce::Justification::left);
        g.drawText (juce::String (span / 1000.0f, 1) + " s", (int) r.getRight() - 50, (int) r.getBottom() - 12, 48, 12, juce::Justification::right);
    }
private:
    VegaDelayProcessor& proc;
};
}

class VegaDelayEditor::Main : public juce::Component
{
public:
    explicit Main (VegaDelayProcessor& p) : proc (p), a (p.apvts), view (p),
        sync (a, "sync", "Sync", "Free|Sync"), link (a, "link", "Link L/R"), mode (a, "mode", "Mode"),
        noteL (a, "noteL", "Time L"), noteR (a, "noteR", "Time R"), msL (a, "msL", "Time L"), msR (a, "msR", "Time R"),
        character (a, "character", "Character"), drive (a, "drive", "Drive"), wow (a, "wow", "Wow"), flutter (a, "flutter", "Flutter"),
        feedback (a, "feedback", "FEEDBACK", true), lowCut (a, "lowCut", "Low Cut"), highCut (a, "highCut", "High Cut"), freeze (a, "freeze", "Freeze"),
        duck (a, "duck", "Ducking"), duckRel (a, "duckRelease", "Duck Release"), width (a, "width", "Width"), mix (a, "mix", "Mix"), output (a, "output", "Output")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &view, &sync, &link, &mode, &noteL, &noteR, &msL, &msR, &character, &drive, &wow, &flutter,
                                                                 &feedback, &lowCut, &highCut, &freeze, &duck, &duckRel, &width, &mix, &output, &meter, &info, &gr })
            addAndMakeVisible (c);
        gr.title = "DUCK"; gr.maxDb = 18.0f;
        tapBtn.setButtonText ("TAP"); tapBtn.onClick = [this] { tap(); }; addAndMakeVisible (tapBtn);
    }
    void refresh()
    {
        view.repaint();
        auto take = [] (std::atomic<float>& v) { return v.exchange (0.0f); };
        meter.setLevels (take (proc.outL), take (proc.outR)); take (proc.inL); take (proc.inR);
        gr.setGr (-juce::Decibels::gainToDecibels (proc.duckGain.load(), -18.0f));
        const bool s = a.getRawParameterValue ("sync")->load() > 0.5f, l = a.getRawParameterValue ("link")->load() > 0.5f;
        noteL.setVisible (s); noteR.setVisible (s); msL.setVisible (! s); msR.setVisible (! s);
        noteR.setEnabled (! l); msR.setEnabled (! l); tapBtn.setVisible (! s);
        const int c = (int) a.getRawParameterValue ("character")->load();
        if (c != lastChar)
        {
            lastChar = c;
            static const char* d[][3] { { "DIGITAL", "clean, exact repeats", "pristine highs" },
                                        { "ANALOG", "bucket-brigade warmth", "dark, soft repeats" },
                                        { "TAPE", "wow, flutter, saturation", "repeats wobble & glide" },
                                        { "LO-FI", "crushed and band-limited", "dusty 8-bit echoes" } };
            info.setLines ("CHARACTER", d[c][0], "SOUND", d[c][1], "REPEATS", d[c][2]);
        }
        duckRel.setEnabled (a.getRawParameterValue ("duck")->load() > 0.5f);
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "DELAY  VP-11");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        vegaui::drawSection (g, { 620, 40, W - 620, 200 }, "TIME");
        vegaui::drawSection (g, { 0, 244, 400, 282 }, "CHARACTER", false);
        vegaui::drawSection (g, { 400, 244, 290, 282 }, "FEEDBACK");
        vegaui::drawSection (g, { 690, 244, W - 690, 282 }, "OUTPUT");
        g.setColour (VegaColours::line); g.drawHorizontalLine (242, 0, (float) W);
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "DUCKING DROPS THE ECHOES WHILE YOU PLAY AND LETS THEM BLOOM IN THE GAPS");
    }
    void resized() override
    {
        view.setBounds (12, 44, 600, 192);
        sync.setBounds (630, 56, 100, 60); link.setBounds (734, 56, 70, 60); mode.setBounds (812, 62, 176, 46);
        noteL.setBounds (630, 140, 170, 46); noteR.setBounds (812, 140, 176, 46);
        msL.setBounds (650, 120, 100, 112); msR.setBounds (830, 120, 100, 112); tapBtn.setBounds (754, 166, 70, 24);
        character.setBounds (14, 270, 150, 46); drive.setBounds (166, 262, 76, 112); wow.setBounds (242, 262, 76, 112); flutter.setBounds (318, 262, 76, 112);
        info.setBounds (18, 392, 370, 112);
        feedback.setBounds (410, 262, 130, 160); lowCut.setBounds (540, 262, 70, 112); highCut.setBounds (612, 262, 74, 112);
        freeze.setBounds (566, 392, 80, 80);
        duck.setBounds (700, 262, 74, 112); duckRel.setBounds (774, 262, 80, 112);
        width.setBounds (700, 392, 74, 112); mix.setBounds (774, 392, 80, 112); output.setBounds (854, 392, 74, 112);
        gr.setBounds (862, 268, 44, 110); meter.setBounds (930, 262, 60, 244);
    }
private:
    void tap()
    {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double dt = now - lastTap; lastTap = now;
        if (dt > 60 && dt < 2000)
            for (auto* id : { "msL", "msR" }) if (auto* q = a.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 ((float) dt));
    }
    VegaDelayProcessor& proc; juce::AudioProcessorValueTreeState& a;
    EchoView view;
    vegaui::Toggle sync, link; vegaui::Choice mode, noteL, noteR; vegaui::Knob msL, msR;
    vegaui::Choice character; vegaui::Knob drive, wow, flutter, feedback, lowCut, highCut; vegaui::Toggle freeze;
    vegaui::Knob duck, duckRel, width, mix, output;
    vegaui::StereoMeter meter; vegaui::Oled info; vegaui::GrColumn gr;
    juce::TextButton tapBtn; double lastTap = 0; int lastChar = -1;
};

VegaDelayEditor::VegaDelayEditor (VegaDelayProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaDelayEditor::~VegaDelayEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaDelayEditor::timerCallback() { main->refresh(); }
void VegaDelayEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaDelayProcessor::createEditor() { return new VegaDelayEditor (*this); }
