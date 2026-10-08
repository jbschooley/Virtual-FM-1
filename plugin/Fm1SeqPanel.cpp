#include "Fm1SeqPanel.h"

#include <algorithm>

#include "ChosenFile.h"

using fm1::seq::kSteps;

namespace {
// FM-1_096's Pattern screen, in its default theme: lavender headings, pink notes, near-black
// columns (every fourth a little lighter), the playing column lighter still
const juce::Colour kBg(0xff16131c), kLane(0xff100c18), kBeat(0xff181420), kPlayCol(0xff291c31), kHead(0xffc5a5f6),
    kNote(0xffff5da4), kDim(0xff8a8496), kCursor(0xffffffff), kRecCursor(0xffff3831), kGrid(0xff2a2433);
constexpr int kCols = 16;

juce::String noteName(int n) {
    static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[n % 12]) + juce::String(n / 12 - 1);   // (60 is C4, as the FM-1 names it)
}

const char* const kDrumKeys[12] = {"F0", "F#0", "G0", "G#0", "A0", "A#0", "B0", "C1", "C#1", "D1", "D#1", "E1"};
const char* const kSfx[12] = {"Coin", "Jump", "Laser", "Shoot", "Explosion", "Power-Up", "1-Up", "Hurt", "Blip", "Warp", "Alarm", "Fall"};

// the names a lock's value has (an 8-Bit preset's arpeggios and arp speed), or none
const char* const kArpNames[33] = {"Off", "Major", "Minor", "Major 8", "Minor 8", "Octave", "2 Octaves", "Fifth", "Fifth 8", "Sus 4",
    "Sus 2", "Dim", "Aug", "Major 7", "Minor 7", "Dom 7", "Major 6", "Minor 6", "Major Down", "Minor Down", "Major Up Down",
    "Minor Up Down", "Trill 1", "Trill 2", "Trill 3", "Trill 4", "Fourth", "Octave Down", "Power", "Major Wide", "Minor Wide", "Bounce", "User"};
const char* const kArpSpeed[7] = {"1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T", "1/64T"};
juce::String lockValueText(int what, int v) {
    if ((what == 17 || what == 18) && v >= 0 && v < 33) return kArpNames[v];
    if (what == 20 && v >= 0 && v < 7) return kArpSpeed[v];
    return juce::String(v);
}
bool isBlack(int n) { return ((0x54A >> (n % 12)) & 1) != 0; }
}  // namespace

juce::String Fm1SeqPage::lockName(int what) {
    static const char* const knob[3][8] = {
        {"Brightness", "Feedback", "Attack", "Decay", "Release", "Vibrato", "LFO Speed", "Cutoff"},
        {"Cutoff", "Resonance", "Filter Env", "Filter Decay", "Shape", "Super", "Detune", "LFO to Cutoff"},
        {"Drum Decay", "Bass Arpeggio", "Lead Arpeggio", "Lead Decay", "Lead Arp Speed", "Lead Vibrato", "Lead Release", "Bass Decay"}};
    static const char* const fx[7][4] = {{"Filter", "Filter Type", "Cutoff", "Resonance"}, {"Reverb", "Reverb Type", "Decay", "Mix"},
        {"Delay", "Feedback", "Rate", "Mix"}, {"Distortion", "Gain", "Tone", "Level"}, {"Chorus", "Rate", "Depth", "Mix"},
        {"Phaser", "Rate", "Depth", "Mix"}, {"Bitcrush", "Bits", "Sample Rate", "Mix"}};
    if (lockRange(what).second < 0) return {};
    if (what >= 32) return juce::String(fx[(what - 32) >> 2][0]) + " " + fx[(what - 32) >> 2][1 + (what & 3)];
    return knob[what >> 3][what & 7];
}

std::pair<int, int> Fm1SeqPage::lockRange(int what) {
    static const int chip[8][2] = {{0, 99}, {0, 32}, {0, 32}, {0, 99}, {0, 6}, {0, 99}, {0, 99}, {0, 99}};
    if (what >= 32) {
        const int e = (what - 32) >> 2, k = what & 3;
        if (e > 6 || k > 2 || (e <= 1 && k == 0)) return {0, -1};   // (an effect's Type, its On/Off)
        if (e == 6) return {k == 0 ? 1 : 0, k == 0 ? 16 : 100};
        return {0, e ? 100 : k == 1 ? 107 : 10};
    }
    if (what < 0 || what >= 24) return {0, -1};
    const int kind = what >> 3, c = what & 7;
    if (kind == 0) return {0, c == 7 ? 100 : c == 1 ? 7 : 99};
    if (kind == 1) return {0, 100};
    return {chip[c][0], chip[c][1]};
}

// ---- the lanes -----------------------------------------------------------------------------

class Fm1SeqPage::Lanes : public juce::Component {
public:
    explicit Lanes(Fm1SeqPage& p) : page_(p) {}

    static constexpr int kHeader = 18;
    int labelW() const { return page_.opened_ >= 0 && page_.opened_ == page_.laneCount() - 1 ? 116 : 64; }   // (locks' names are long)
    juce::Rectangle<int> grid() const { return getLocalBounds().withTrimmedLeft(labelW()).withTrimmedTop(kHeader); }
    float colW() const { return float(grid().getWidth()) / kCols; }
    int colAt(int x) const { return juce::jlimit(0, kCols - 1, int((x - grid().getX()) / std::max(1.0f, colW()))); }
    float colX(int c) const { return float(grid().getX()) + float(c) * colW(); }

    // the rows of the opened lane: a pitched lane's notes from the roll's lowest, a drum or SFX lane's
    // twelve keys, the Locks lane's locked settings
    struct Row { int key; juce::String name; bool black; };
    std::vector<Row> rows() const {
        std::vector<Row> out;
        const int lane = page_.opened_;
        if (lane < 0) return out;
        if (lane == page_.laneCount() - 1) {
            std::vector<int> whats;
            const auto& t = page_.snap_.locks;
            for (size_t i = 0; i + 1 < t.size(); i += 2)
                if (t[i] != 0xFF && std::find(whats.begin(), whats.end(), int(t[i])) == whats.end()) whats.push_back(t[i]);
            std::sort(whats.begin(), whats.end());
            for (int w : whats) out.push_back({w, lockName(w).isEmpty() ? "Lock " + juce::String(w) : lockName(w), false});
            return out;
        }
        const Lane l = page_.laneAt(lane);
        if (!l.pitched) {
            for (int i = 11; i >= 0; --i) out.push_back({l.lo + i, l.lo == 17 ? kDrumKeys[i] : kSfx[i], false});
            return out;
        }
        const int n = std::max(12, std::min(36, getHeight() / 14));
        const int low = juce::jlimit(l.lo, std::max(l.lo, l.hi - n + 1), page_.rollLow_);
        for (int k = std::min(l.hi, low + n - 1); k >= low; --k) out.push_back({k, noteName(k), isBlack(k)});
        return out;
    }

    void paint(juce::Graphics& g) override {
        const auto& p = page_.snap_;
        g.fillAll(kBg);
        const auto gr = grid();
        const int first = page_.page_ * kCols, len = std::max(1, p.length);
        const int playing = page_.playingStep();
        // the step numbers
        g.setFont(juce::FontOptions(11.0f));
        for (int c = 0; c < kCols; ++c) {
            const int s = first + c;
            auto head = juce::Rectangle<float>(colX(c), 0.0f, colW(), float(kHeader));
            g.setColour(s == page_.sel_ ? kHead : s < len ? kDim : kDim.withAlpha(0.35f));
            g.drawText(juce::String(s + 1), head, juce::Justification::centred);
        }
        auto column = [&](juce::Rectangle<float> band, int c) {
            const int s = first + c;
            if (s >= len) return;   // (nothing past the pattern's last step, as on the FM-1)
            auto cell = juce::Rectangle<float>(colX(c) + 1.0f, band.getY(), colW() - 2.0f, band.getHeight());
            g.setColour(s == playing ? kPlayCol : c % 4 == 0 ? kBeat : kLane);
            g.fillRect(cell);
        };
        auto noteBars = [&](juce::Rectangle<float> band, int lo, int hi, std::function<float(int)> yOf, float h) {
            for (int s = 0; s < len; ++s)   // (steps past the pattern's length are kept, not drawn)
                for (const auto& n : p.steps[size_t(s)].notes) {
                    if (n.note < lo || n.note > hi) continue;
                    const int a = s - first, b = std::min(s + n.len, len - 1) - first;
                    if (b < 0 || a >= kCols) continue;
                    const float x0 = colX(std::max(0, a)) + 2.0f, x1 = colX(std::min(kCols - 1, b)) + colW() - 2.0f;
                    const float y = yOf(n.note);
                    if (y < band.getY() - 1.0f || y > band.getBottom()) continue;
                    g.setColour(s == playing ? juce::Colours::white : kNote);
                    g.fillRoundedRectangle(x0, y, std::max(3.0f, x1 - x0), h, std::min(2.0f, h / 2));
                    if (p.steps[size_t(s)].accent && a >= 0) { g.setColour(juce::Colours::white); g.fillRect(x0, y, 2.0f, h); }
                    const int hits = p.steps[size_t(s)].ratchet;
                    if (hits > 1 && a >= 0) {   // a ratchet cuts the bar's first step into its hits
                        g.setColour(kLane);
                        for (int k = 1; k < hits; ++k) g.fillRect(colX(a) + colW() * float(k) / float(hits) - 0.5f, y, 1.5f, h);
                    }
                }
        };
        if (page_.opened_ < 0) {   // the overview: every lane, a band each
            const int n = page_.laneCount();
            const float gap = 4.0f, bandH = (float(gr.getHeight()) - gap * float(n - 1)) / float(n);
            for (int lane = 0; lane < n; ++lane) {
                auto band = juce::Rectangle<float>(float(gr.getX()), float(gr.getY()) + float(lane) * (bandH + gap), float(gr.getWidth()), bandH);
                for (int c = 0; c < kCols; ++c) column(band, c);
                g.setColour(kHead);
                g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
                g.drawText(page_.laneName(lane), juce::Rectangle<float>(0.0f, band.getY(), float(labelW() - 6), bandH), juce::Justification::centredRight);
                if (lane == n - 1) {   // the Locks: a block for each lock, stacked from the bottom
                    const auto& t = p.locks;
                    for (int c = 0; c < kCols; ++c) {
                        const int s = first + c;
                        if (s >= len || t.size() != size_t(fm1::seq::kLockBytes)) continue;
                        int k = 0;
                        for (int j = 0; j < 4; ++j) if (t[size_t(8 * s + 2 * j)] != 0xFF) ++k;
                        const float bh = std::min(8.0f, (bandH - 4.0f) / 4.0f - 1.0f);
                        g.setColour(kNote);
                        for (int j = 0; j < k; ++j)
                            g.fillRect(colX(c) + 3.0f, band.getBottom() - 2.0f - float(j + 1) * (bh + 1.0f), colW() - 6.0f, bh);
                    }
                    continue;
                }
                const Lane l = page_.laneAt(lane);
                int lo = 127, hi = 0;   // a pitched lane spans its own lowest to highest note
                for (int k = 0; k < len; ++k)
                    for (const auto& nt : p.steps[size_t(k)].notes) if (nt.note >= l.lo && nt.note <= l.hi) { lo = std::min(lo, nt.note); hi = std::max(hi, nt.note); }
                if (!l.pitched) { lo = l.lo; hi = l.lo + 11; }
                if (lo > hi) continue;
                const float h = 3.0f, span = float(std::max(1, hi - lo));
                noteBars(band, l.lo, l.hi, [&](int note) { return band.getBottom() - 3.0f - h - (band.getHeight() - 6.0f - h) * float(note - lo) / span; }, h);
            }
        } else {   // one lane, opened: a row for each note (or locked setting)
            const auto rs = rows();
            const float rowH = rs.empty() ? 20.0f : float(gr.getHeight()) / float(rs.size());
            auto rowY = [&](size_t i) { return float(gr.getY()) + float(i) * rowH; };
            for (size_t i = 0; i < rs.size(); ++i) {
                auto band = juce::Rectangle<float>(float(gr.getX()), rowY(i), float(gr.getWidth()), rowH - 1.0f);
                for (int c = 0; c < kCols; ++c) column(band, c);
                if (rs[i].black) { g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillRect(band); }
                g.setColour(rs[i].black ? kDim : kHead);
                g.setFont(juce::FontOptions(std::min(12.0f, rowH - 2.0f)));
                g.drawText(rs[i].name, juce::Rectangle<float>(0.0f, rowY(i), float(labelW() - 6), rowH), juce::Justification::centredRight);
            }
            if (page_.opened_ == page_.laneCount() - 1) {   // the Locks: a bar whose height is the value
                const auto& t = p.locks;
                for (size_t i = 0; i < rs.size() && t.size() == size_t(fm1::seq::kLockBytes); ++i) {
                    const auto range = lockRange(rs[i].key);
                    for (int c = 0; c < kCols; ++c) {
                        const int s = first + c;
                        if (s >= len) continue;
                        for (int j = 0; j < 4; ++j) {
                            if (t[size_t(8 * s + 2 * j)] != rs[i].key) continue;
                            const int v = t[size_t(8 * s + 2 * j + 1)];
                            const float f = range.second > range.first ? float(v - range.first) / float(range.second - range.first) : 0.5f;
                            const float bh = std::max(2.0f, (rowH - 4.0f) * juce::jlimit(0.0f, 1.0f, f));
                            g.setColour(kNote);
                            g.fillRect(colX(c) + 3.0f, rowY(i) + rowH - 2.0f - bh, colW() - 6.0f, bh);
                        }
                    }
                }
                if (rs.empty()) {
                    g.setColour(kDim);
                    g.drawText("No locks: add one to a step below (or on the FM-1: SEL and a knob while step recording)", gr.toFloat(), juce::Justification::centred);
                }
            } else {
                std::vector<int> keys;
                for (const auto& r : rs) keys.push_back(r.key);
                const float h = std::max(3.0f, rowH - 5.0f);
                noteBars(gr.toFloat(), keys.empty() ? 0 : keys.back(), keys.empty() ? -1 : keys.front(), [&](int note) {
                    const auto it = std::find(keys.begin(), keys.end(), note);
                    return it == keys.end() ? -1000.0f : rowY(size_t(it - keys.begin())) + 2.0f;
                }, h);
            }
        }
        // the selected step's outline: red while recording
        const int sc = page_.sel_ - first;
        if (sc >= 0 && sc < kCols) {
            g.setColour(page_.rec_.getToggleState() ? kRecCursor : kCursor);
            g.drawRect(juce::Rectangle<float>(colX(sc), float(gr.getY()), colW(), float(gr.getHeight())), 1.5f);
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        dragNote_ = -1;
        page_.grabKeyboardFocus();   // (the arrow keys move the selection)
        if (e.x < labelW()) {   // a lane's name: open it (or go back to all of them)
            if (page_.opened_ >= 0 || e.y < kHeader) return;
            const int n = page_.laneCount();
            const int lane = juce::jlimit(0, n - 1, int(float(e.y - grid().getY()) / (float(grid().getHeight()) / float(n))));
            page_.openLane(lane);
            return;
        }
        const int step = page_.page_ * kCols + colAt(e.x);
        if (step >= std::max(1, page_.snap_.length) || e.y < kHeader || page_.opened_ < 0 || page_.opened_ == page_.laneCount() - 1) {
            page_.selectStep(std::min(step, kSteps - 1));
            return;
        }
        const auto rs = rows();
        if (rs.empty()) return;
        const int i = juce::jlimit(0, int(rs.size()) - 1, int(float(e.y - grid().getY()) / (float(grid().getHeight()) / float(rs.size()))));
        const int note = rs[size_t(i)].key;
        page_.selectStep(step);
        // on a note that starts here: drag to hold it longer (or let go to take it off); elsewhere, add one
        bool there = false;
        for (const auto& n : page_.snap_.steps[size_t(step)].notes) there = there || n.note == note;
        if (there) { dragNote_ = note; dragStep_ = step; dragged_ = false; lastAt_ = -1; return; }
        page_.toggleNote(step, note);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragNote_ < 0) return;
        const int at = page_.page_ * kCols + colAt(e.x);
        if ((!dragged_ && at == dragStep_) || at == lastAt_) return;
        dragged_ = true;
        lastAt_ = at;
        page_.setHold(dragStep_, dragNote_, std::max(0, at - dragStep_));
    }
    void mouseUp(const juce::MouseEvent&) override {
        if (dragNote_ >= 0 && !dragged_) page_.toggleNote(dragStep_, dragNote_);
        dragNote_ = -1;
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override {
        if (page_.opened_ < 0 || page_.opened_ == page_.laneCount() - 1 || !page_.laneAt(page_.opened_).pitched) return;
        page_.rollLow_ = juce::jlimit(0, 120, page_.rollLow_ + (w.deltaY > 0 ? 1 : w.deltaY < 0 ? -1 : 0));
        repaint();
    }

private:
    Fm1SeqPage& page_;
    int dragNote_ = -1, dragStep_ = 0, lastAt_ = -1;
    bool dragged_ = false;
};

// ---- the step's notes and locks, a row each --------------------------------------------------

struct Fm1SeqPage::NoteRow {
    juce::Label name;
    juce::Slider vel{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::Slider hold{juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft};
    juce::TextButton remove{"x"};
    int note = 0;
};
struct Fm1SeqPage::LockRow {
    juce::Label name;
    juce::Slider value{juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight};
    juce::TextButton remove{"x"};
    int what = 0;
};

// ---- the page ----------------------------------------------------------------------------------

Fm1SeqPage::Fm1SeqPage(FM1Processor& p) : proc_(p) {
    lanes_ = std::make_unique<Lanes>(*this);
    addAndMakeVisible(*lanes_);
    setWantsKeyboardFocus(true);   // (the arrow keys: keyPressed)
    auto label = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setFont(juce::FontOptions(12.0f));
        l.setColour(juce::Label::textColourId, kDim);
        addAndMakeVisible(l);
    };
    label(stepsL_, "Steps"); label(rateL_, "Rate"); label(swingL_, "Swing"); label(gateL_, "Gate"); label(chainL_, "Chain");
    label(repeatsL_, "Repeats"); label(tempoL_, "Tempo"); label(transposeL_, "Transpose"); label(stepRateL_, "Length");
    label(ratchetL_, "Ratchet"); label(stepGateL_, "Gate"); label(stepChanceL_, "Chance"); label(stepTransposeL_, "Transpose");
    label(notesTitle_, "Notes"); label(locksTitle_, "Locks"); label(info_, "");
    stepTitle_.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    stepTitle_.setColour(juce::Label::textColourId, kHead);
    addAndMakeVisible(stepTitle_);
    for (auto* l : {&notesTitle_, &locksTitle_}) l->setColour(juce::Label::textColourId, kHead);

    for (auto* c : std::initializer_list<juce::Component*>{&enable_, &play_, &rec_, &pattern_, &sync_, &overdub_, &pull_, &push_, &import_, &export_,
             &steps_, &rate_, &swing_, &gate_, &chain_, &repeats_, &tempo_, &transpose_, &lanesMode_box_, &back_, &octDown_, &octUp_,
             &stepRate_, &ratchet_, &stepGate_, &stepChance_, &stepTranspose_, &accent_, &slide_, &addLock_,
             &copyStep_, &pasteStep_, &clearStep_, &clearPattern_})
        addAndMakeVisible(c);
    for (int i = 0; i < 4; ++i) {
        pageButtons_[i].setButtonText(juce::String(16 * i + 1) + "-" + juce::String(16 * i + 16));
        pageButtons_[i].setClickingTogglesState(true);
        pageButtons_[i].setRadioGroupId(7301);
        pageButtons_[i].setColour(juce::TextButton::buttonOnColourId, kHead.darker(0.5f));
        pageButtons_[i].onClick = [this, i] { follow_ = false; showPage(i); };
        addAndMakeVisible(pageButtons_[i]);
    }
    enable_.setClickingTogglesState(true);
    enable_.setColour(juce::TextButton::buttonOnColourId, kHead.darker(0.4f));
    enable_.setTooltip("The Sequencer on: it plays with the host's transport (Sync to host) or with Play");
    enable_.onClick = [this] { proc_.sequencer.enabled = enable_.getToggleState(); if (!enable_.getToggleState()) proc_.sequencer.stop(); };
    play_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::seagreen);
    play_.setTooltip("Play or stop the pattern");
    play_.onClick = [this] {
        if (proc_.sequencer.isPlaying()) proc_.sequencer.stop();
        else { proc_.sequencer.enabled = true; enable_.setToggleState(true, juce::dontSendNotification); follow_ = true; proc_.sequencer.play(); }
    };
    rec_.setClickingTogglesState(true);
    rec_.setColour(juce::TextButton::buttonOnColourId, kRecCursor.darker(0.2f));
    rec_.setTooltip("Record: stopped, notes you play go into the outlined step and it moves on; playing, they land on the nearest step");
    rec_.onClick = [this] { proc_.sequencer.recording = rec_.getToggleState() && proc_.sequencer.isPlaying(); lanes_->repaint(); };
    for (int i = 1; i <= Sequencer::kPatterns; ++i) pattern_.addItem("Pattern " + juce::String(i), i);
    pattern_.onChange = [this] { proc_.sequencer.selected = pattern_.getSelectedId() - 1; sel_ = 0; readPattern(); loadSettings(); loadStep(); showPage(0); };
    sync_.onClick = [this] { proc_.sequencer.syncToHost = sync_.getToggleState(); };
    overdub_.onClick = [this] { proc_.sequencer.overdub = overdub_.getToggleState(); };
    overdub_.setTooltip("Recording adds to what a step holds (off: the first note in a step replaces it)");
    pull_.onClick = [this] { proc_.pullPatterns(); };
    push_.onClick = [this] { proc_.pushPatterns(true); };
    pull_.setTooltip("Read all 16 patterns from the FM-1");
    push_.setTooltip("Write all 16 patterns to the FM-1 (FM-1_096: every step setting, held notes, Chain and locks)");
    import_.setTooltip("Load patterns from a .json file, or a MIDI file into this pattern");
    export_.setTooltip("Save this pattern or all 16 as .json (every setting) or as a MIDI file");
    import_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import patterns (.json), or a MIDI file into this pattern (.mid)",
                                                       juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json;*.mid;*.midi");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            auto f = fm1ui::openedFile(fc);
            if (!f.existsAsFile()) return;
            if (f.hasFileExtension("mid;midi")) say(proc_.importPatternMidi(f));
            else {
                const auto r = proc_.importJson(f, false, true);
                say(r.summary);
                if (!r.errors.isEmpty()) {   // (what was wrong, line by line)
                    juce::StringArray shown;
                    for (int i = 0; i < std::min(20, r.errors.size()); ++i) shown.add(r.errors[i]);
                    if (r.errors.size() > 20) shown.add("... and " + juce::String(r.errors.size() - 20) + " more");
                    juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Some of the file was not imported").withMessage(shown.joinIntoString("\n")).withButton("OK").withAssociatedComponent(this), nullptr);
                }
            }
            readPattern(); loadSettings(); loadStep();
        });
    };
    export_.onClick = [this] { showExportMenu(); };

    // the pattern's settings
    for (int i = 1; i <= kSteps; ++i) steps_.addItem(juce::String(i), i);
    for (int i = 0; i < 10; ++i) { rate_.addItem(fm1::seq::kNoteValueNames[i], i + 1); stepRate_.addItem(fm1::seq::kNoteValueNames[i], i + 1); }
    chain_.addItem("Repeat", 1);
    for (int i = 1; i <= Sequencer::kPatterns; ++i) chain_.addItem("Pattern " + juce::String(i), i + 1);
    for (int i = 0; i < 8; ++i) repeats_.addItem(juce::String(fm1::seq::kRepeats[i]), i + 1);
    auto slider = [](juce::Slider& s, double lo, double hi) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 42, 18);
        s.setRange(lo, hi, 1.0);
        s.setScrollWheelEnabled(false);
    };
    slider(swing_, 50, 75); slider(gate_, 5, 100); slider(tempo_, 30, 300); slider(transpose_, -24, 24);
    slider(stepGate_, 0, 100); slider(stepChance_, 5, 100); slider(stepTranspose_, -24, 24);
    stepGate_.textFromValueFunction = [](double v) { return v < 5 ? juce::String("Pattern") : juce::String(int(v)); };
    stepChance_.textFromValueFunction = [](double v) { return v >= 100 ? juce::String("Always") : juce::String(int(v)); };
    tempo_.setTooltip("The tempo (on the FM-1, one tempo for every pattern: GLOBE)");
    transpose_.setTooltip("Transposes this pattern here (on the FM-1, Transpose is global)");
    chain_.setTooltip("What plays after this pattern");
    repeats_.setTooltip("With a chain: how many times this pattern plays first");
    auto applyP = [this] { applySettings(); };
    for (auto* c : {&steps_, &rate_, &chain_, &repeats_}) c->onChange = applyP;
    for (auto* s : {&swing_, &gate_, &tempo_, &transpose_}) s->onValueChange = applyP;
    lanesMode_box_.addItem("Lanes: as the notes say", 1);
    lanesMode_box_.addItem("Lanes: FM / VA", 2);
    lanesMode_box_.addItem("Lanes: 8-Bit", 3);
    lanesMode_box_.setSelectedId(1, juce::dontSendNotification);
    lanesMode_box_.setTooltip("A pattern does not say which preset plays it: as on baud girl's app, its notes are taken for an 8-Bit "
                              "preset's when they are all on its parts' keys and some are drums");
    lanesMode_box_.onChange = [this] { lanesMode_ = lanesMode_box_.getSelectedId() - 1; opened_ = -1; resized(); lanes_->repaint(); };
    back_.onClick = [this] { openLane(-1); };
    octDown_.onClick = [this] { rollLow_ = std::max(0, rollLow_ - 12); lanes_->repaint(); };
    octUp_.onClick = [this] { rollLow_ = std::min(120, rollLow_ + 12); lanes_->repaint(); };

    // the step
    ratchet_.addItem("Off", 1); ratchet_.addItem("2", 2); ratchet_.addItem("3", 3); ratchet_.addItem("4", 4);
    auto applyS = [this] { applyStep(); };
    stepRate_.onChange = applyS; ratchet_.onChange = applyS;
    for (auto* s : {&stepGate_, &stepChance_, &stepTranspose_}) s->onValueChange = applyS;
    accent_.onClick = applyS;
    slide_.onClick = applyS;
    slide_.setTooltip("Every note of the step held into the next one");
    accent_.setTooltip("The step's notes at full velocity");
    // every note of the step at one velocity
    allVel_.setSliderStyle(juce::Slider::LinearHorizontal);
    allVel_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
    allVel_.setRange(1, 127, 1);
    allVel_.setScrollWheelEnabled(false);
    allVelL_.setText("All notes", juce::dontSendNotification);
    allVelL_.setFont(juce::FontOptions(12.0f));
    allVelL_.setColour(juce::Label::textColourId, kDim);
    addChildComponent(allVel_);
    addChildComponent(allVelL_);
    allVel_.onValueChange = [this] {
        if (loading_) return;
        const int v = int(allVel_.getValue());
        {
            const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
            for (auto& n : pattern().steps[size_t(sel_)].notes) n.vel = v;
        }
        for (auto& r : noteRows_) r->vel.setValue(v, juce::dontSendNotification);
        readPattern();
        lanes_->repaint();
    };
    addLock_.setTextWhenNothingSelected("Add a lock...");
    {
        const char* const groups[4] = {"FM", "Virtual Analog", "8-Bit", "Effects"};
        for (int g = 0; g < 4; ++g) {
            addLock_.addSectionHeading(groups[g]);
            for (int w = g == 3 ? 32 : g * 8; w < (g == 3 ? 59 : g * 8 + 8); ++w)
                if (lockName(w).isNotEmpty()) addLock_.addItem(lockName(w), w + 1);
        }
    }
    addLock_.setTooltip("Lock a setting to this step (four at most; a lock of one engine's setting plays only on that engine's presets)");
    addLock_.onChange = [this] {
        const int w = addLock_.getSelectedId() - 1;
        addLock_.setSelectedId(0, juce::dontSendNotification);
        if (w >= 0) addLock(sel_, w);
    };
    copyStep_.onClick = [this] {
        const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
        clipboard_ = pattern().steps[size_t(sel_)];
        clipboardLocks_ = locksOf(pattern(), sel_);
    };
    pasteStep_.onClick = [this] {
        if (!clipboard_) return;
        edit([this](fm1::seq::Pattern& p) { p.steps[size_t(sel_)] = *clipboard_; putLocks(p, sel_, clipboardLocks_); });
    };
    clearStep_.onClick = [this] {
        edit([this](fm1::seq::Pattern& p) { auto& s = p.steps[size_t(sel_)]; s = fm1::seq::Step{}; s.rate = p.rate; putLocks(p, sel_, {}); });
    };
    clearPattern_.onClick = [this] {   // as the FM-1's Clear Pattern: the steps and locks go, Rate back to 1/16
        edit([](fm1::seq::Pattern& p) { p.rate = 6; for (auto& s : p.steps) { s = fm1::seq::Step{}; s.rate = 6; } p.locks.clear(); });
        loadSettings();
    };
    for (auto* b : {&copyStep_, &pasteStep_, &clearStep_, &clearPattern_}) b->setTooltip(b->getButtonText());

    sync_.setToggleState(proc_.sequencer.syncToHost, juce::dontSendNotification);
    overdub_.setToggleState(proc_.sequencer.overdub, juce::dontSendNotification);
    enable_.setToggleState(proc_.sequencer.enabled, juce::dontSendNotification);
    pattern_.setSelectedId(proc_.sequencer.selected + 1, juce::dontSendNotification);
    info_.setColour(juce::Label::textColourId, kDim);
    readPattern();
    loadSettings();
    loadStep();
    showPage(0);
    startTimerHz(20);
}

Fm1SeqPage::~Fm1SeqPage() { stopTimer(); }

void Fm1SeqPage::refresh() {
    pattern_.setSelectedId(proc_.sequencer.selected + 1, juce::dontSendNotification);
    readPattern(); loadSettings(); loadStep();
    resized();
    lanes_->repaint();
}

fm1::seq::Pattern& Fm1SeqPage::pattern() { return proc_.sequencer.patterns[size_t(proc_.sequencer.selected.load())]; }

void Fm1SeqPage::readPattern() {
    const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
    snap_ = pattern();
    snap_.chain = proc_.sequencer.chain[size_t(proc_.sequencer.selected.load())];
}

void Fm1SeqPage::edit(const std::function<void(fm1::seq::Pattern&)>& f) {
    {
        const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
        f(pattern());
    }
    readPattern();
    loadStep();
    lanes_->repaint();
}

bool Fm1SeqPage::chipLanes() const {
    if (lanesMode_ != 0) return lanesMode_ == 2;
    bool any = false, drum = false;   // (her app's rule: every note on a part's keys, some of them drums)
    for (int k = 0; k < std::min(kSteps, snap_.length); ++k)
        for (const auto& n : snap_.steps[size_t(k)].notes) {
            if (n.note < 17) return false;
            any = true;
            drum = drum || n.note <= 28;
        }
    return any && drum;
}

int Fm1SeqPage::laneCount() const { return chipLanes() ? 5 : 2; }
juce::String Fm1SeqPage::laneName(int lane) const { return lane == laneCount() - 1 ? juce::String("Locks") : juce::String(laneAt(lane).name); }

void Fm1SeqPage::openLane(int lane) {
    opened_ = juce::jlimit(-1, laneCount() - 1, lane);
    if (opened_ >= 0 && opened_ < laneCount() - 1 && laneAt(opened_).pitched) {   // the roll from the lane's lowest note
        const Lane l = laneAt(opened_);
        int lo = 128;
        for (int k = 0; k < std::min(kSteps, snap_.length); ++k)
            for (const auto& n : snap_.steps[size_t(k)].notes) if (n.note >= l.lo && n.note <= l.hi) lo = std::min(lo, n.note);
        rollLow_ = lo < 128 ? lo - lo % 12 : std::max(l.lo, 48);
    }
    resized();
    lanes_->repaint();
}

void Fm1SeqPage::showPage(int p) {
    page_ = juce::jlimit(0, 3, p);
    for (int i = 0; i < 4; ++i) pageButtons_[i].setToggleState(i == page_, juce::dontSendNotification);
    lanes_->repaint();
}

void Fm1SeqPage::selectStep(int step) {
    sel_ = juce::jlimit(0, kSteps - 1, step);
    if (sel_ / kCols != page_) showPage(sel_ / kCols);
    loadStep();
    lanes_->repaint();
}

bool Fm1SeqPage::keyPressed(const juce::KeyPress& k) {   // left and right: the step before or after (within the pattern)
    if (k != juce::KeyPress::leftKey && k != juce::KeyPress::rightKey) return false;
    const int len = juce::jlimit(1, kSteps, snap_.length);
    selectStep(juce::jlimit(0, len - 1, sel_ + (k == juce::KeyPress::leftKey ? -1 : 1)));
    return true;
}

void Fm1SeqPage::toggleNote(int step, int note) {
    if (step < 0 || step >= kSteps) return;
    bool full = false;
    edit([&](fm1::seq::Pattern& p) {
        auto& notes = p.steps[size_t(step)].notes;
        auto it = std::find_if(notes.begin(), notes.end(), [&](const fm1::seq::Note& n) { return n.note == note; });
        if (it != notes.end()) notes.erase(it);
        else if (int(notes.size()) < fm1::seq::kMaxNotes) notes.push_back({note, 100, 0});
        else full = true;
    });
    if (full) say("Step " + juce::String(step + 1) + " holds nine notes, the most a step can");
}

void Fm1SeqPage::setHold(int step, int note, int steps) {
    edit([&](fm1::seq::Pattern& p) {
        for (auto& n : p.steps[size_t(step)].notes) if (n.note == note) n.len = juce::jlimit(0, kSteps - 1 - step, steps);
    });
}

std::vector<std::pair<int, int>> Fm1SeqPage::locksOf(const fm1::seq::Pattern& p, int step) const {
    std::vector<std::pair<int, int>> out;
    if (p.locks.size() != size_t(fm1::seq::kLockBytes)) return out;
    for (int j = 0; j < fm1::seq::kLocksPerStep; ++j)
        if (p.locks[size_t(8 * step + 2 * j)] != 0xFF) out.emplace_back(p.locks[size_t(8 * step + 2 * j)], p.locks[size_t(8 * step + 2 * j + 1)]);
    return out;
}

void Fm1SeqPage::putLocks(fm1::seq::Pattern& p, int step, const std::vector<std::pair<int, int>>& locks) {
    if (p.locks.size() != size_t(fm1::seq::kLockBytes)) {
        if (locks.empty()) return;
        p.locks.assign(size_t(fm1::seq::kLockBytes), 0xFF);
    }
    for (int j = 0; j < fm1::seq::kLocksPerStep; ++j) {
        const bool on = j < int(locks.size());
        p.locks[size_t(8 * step + 2 * j)] = on ? uint8_t(locks[size_t(j)].first) : 0xFF;
        p.locks[size_t(8 * step + 2 * j + 1)] = on ? uint8_t(locks[size_t(j)].second) : 0xFF;
    }
}

void Fm1SeqPage::addLock(int step, int what) {
    const auto range = lockRange(what);
    if (range.second < 0) return;
    bool full = false;
    edit([&](fm1::seq::Pattern& p) {
        auto l = locksOf(p, step);
        if (std::any_of(l.begin(), l.end(), [&](auto& x) { return x.first == what; })) return;
        if (l.size() >= size_t(fm1::seq::kLocksPerStep)) { full = true; return; }
        l.emplace_back(what, (range.first + range.second) / 2);   // (a new lock starts mid-range, as in her app)
        putLocks(p, step, l);
    });
    if (full) say("Step " + juce::String(step + 1) + " has 4 locks");
}

void Fm1SeqPage::setLock(int step, int what, int value) {
    if (lockRange(what).second < 0) return;
    edit([&](fm1::seq::Pattern& p) {
        auto l = locksOf(p, step);
        for (auto& x : l) if (x.first == what) x.second = juce::jlimit(lockRange(what).first, lockRange(what).second, value);
        putLocks(p, step, l);
    });
}

void Fm1SeqPage::removeLock(int step, int what) {
    edit([&](fm1::seq::Pattern& p) {
        auto l = locksOf(p, step);
        l.erase(std::remove_if(l.begin(), l.end(), [&](auto& x) { return x.first == what; }), l.end());
        putLocks(p, step, l);
    });
}

void Fm1SeqPage::loadSettings() {
    loading_ = true;
    const auto& p = snap_;
    steps_.setSelectedId(juce::jlimit(1, kSteps, p.length), juce::dontSendNotification);
    rate_.setSelectedId(p.rate + 1, juce::dontSendNotification);
    chain_.setSelectedId(p.chain + 2, juce::dontSendNotification);
    int ri = 0;
    for (int i = 0; i < 8; ++i) if (fm1::seq::kRepeats[i] <= p.repeats) ri = i;
    repeats_.setSelectedId(ri + 1, juce::dontSendNotification);
    repeats_.setEnabled(p.chain >= 0);
    swing_.setValue(p.swing, juce::dontSendNotification);
    gate_.setValue(p.gate, juce::dontSendNotification);
    tempo_.setValue(p.tempo, juce::dontSendNotification);
    transpose_.setValue(p.transpose, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i) pageButtons_[i].setEnabled(i == 0 || 16 * i < p.length);
    loading_ = false;
}

void Fm1SeqPage::applySettings() {
    if (loading_) return;
    const int pat = proc_.sequencer.selected.load();
    edit([&](fm1::seq::Pattern& p) {
        p.length = steps_.getSelectedId();
        const int rate = rate_.getSelectedId() - 1;
        if (rate != p.rate) { for (auto& s : p.steps) s.rate = rate; p.rate = rate; }   // (as on the FM-1: a new Rate resets each step's Length)
        p.swing = int(swing_.getValue()); p.gate = int(gate_.getValue());
        p.tempo = int(tempo_.getValue()); p.transpose = int(transpose_.getValue());
        p.chain = chain_.getSelectedId() - 2;
        p.repeats = fm1::seq::kRepeats[juce::jlimit(0, 7, repeats_.getSelectedId() - 1)];
        proc_.sequencer.chain[size_t(pat)] = p.chain;
    });
    loadSettings();
    lanes_->repaint();
}

void Fm1SeqPage::loadStep() {
    loading_ = true;
    const auto& s = snap_.steps[size_t(sel_)];
    stepTitle_.setText("Step " + juce::String(sel_ + 1), juce::dontSendNotification);
    stepRate_.setSelectedId(s.rate + 1, juce::dontSendNotification);
    ratchet_.setSelectedId(s.ratchet, juce::dontSendNotification);
    stepGate_.setValue(s.gate, juce::dontSendNotification);
    stepChance_.setValue(s.chance, juce::dontSendNotification);
    stepTranspose_.setValue(s.transpose, juce::dontSendNotification);
    accent_.setToggleState(s.accent, juce::dontSendNotification);
    slide_.setToggleState(!s.notes.empty() && std::all_of(s.notes.begin(), s.notes.end(), [](const fm1::seq::Note& n) { return n.len > 0; }),
                          juce::dontSendNotification);
    // a row for each note: its velocity and how long it is held
    noteRows_.clear();
    for (const auto& n : s.notes) {
        auto r = std::make_unique<NoteRow>();
        r->note = n.note;
        // (an 8-Bit preset's drum and SFX keys by their names, as its lanes show them)
        const bool chip = chipLanes();
        r->name.setText(chip && n.note >= 17 && n.note <= 28 ? juce::String(kDrumKeys[n.note - 17])
                        : chip && n.note >= 29 && n.note <= 40 ? juce::String(kSfx[n.note - 29]) : noteName(n.note), juce::dontSendNotification);
        r->name.setColour(juce::Label::textColourId, kNote);
        r->vel.setRange(1, 127, 1);
        r->vel.setValue(n.vel, juce::dontSendNotification);
        r->vel.setTextBoxStyle(juce::Slider::TextBoxRight, false, 36, 18);
        r->vel.setScrollWheelEnabled(false);
        r->vel.setTooltip("Velocity");
        r->hold.setRange(0, kSteps - 1 - sel_, 1);
        r->hold.setValue(n.len, juce::dontSendNotification);
        r->hold.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 52, 18);
        r->hold.textFromValueFunction = [](double v) { return v < 1 ? juce::String("its step") : "+" + juce::String(int(v)); };
        r->hold.updateText();
        r->hold.setTooltip("Held past its own step for this many steps (Tie & Slide: +1)");
        const int note = n.note;
        r->vel.onValueChange = [this, note, s = r.get()] {
            const int v = int(s->vel.getValue());
            {
                const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
                for (auto& x : pattern().steps[size_t(sel_)].notes) if (x.note == note) x.vel = v;
            }
            readPattern();
            lanes_->repaint();
        };
        // (after the click: the edit rebuilds these rows, this one too)
        juce::Component::SafePointer<Fm1SeqPage> self(this);
        r->hold.onValueChange = [self, note, s = r.get()] {
            const int v = int(s->hold.getValue());
            juce::MessageManager::callAsync([self, note, v] { if (self) self->setHold(self->sel_, note, v); });
        };
        r->remove.onClick = [self, note] { juce::MessageManager::callAsync([self, note] { if (self) self->toggleNote(self->sel_, note); }); };
        r->remove.setTooltip("Remove the note");
        for (auto* c : std::initializer_list<juce::Component*>{&r->name, &r->vel, &r->hold, &r->remove}) addAndMakeVisible(c);
        noteRows_.push_back(std::move(r));
    }
    allVel_.setVisible(s.notes.size() > 1);
    allVelL_.setVisible(s.notes.size() > 1);
    if (s.notes.size() > 1) allVel_.setValue(s.notes[0].vel, juce::dontSendNotification);
    lockRows_.clear();
    for (const auto& [what, value] : locksOf(snap_, sel_)) {
        auto r = std::make_unique<LockRow>();
        r->what = what;
        r->name.setText(lockName(what).isEmpty() ? "Lock " + juce::String(what) : lockName(what), juce::dontSendNotification);
        r->name.setColour(juce::Label::textColourId, kHead);
        const auto range = lockRange(what);
        r->value.setRange(range.first, std::max(range.first + 1, range.second), 1);
        r->value.setValue(value, juce::dontSendNotification);
        r->value.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 18);
        r->value.setScrollWheelEnabled(false);
        r->value.textFromValueFunction = [what](double v) { return lockValueText(what, int(v)); };
        r->value.updateText();
        const int w = what;
        r->value.onValueChange = [this, w, s = r.get()] {
            const int v = int(s->value.getValue());
            {
                const juce::SpinLock::ScopedLockType l(proc_.sequencer.lock);
                auto l2 = locksOf(pattern(), sel_);
                for (auto& x : l2) if (x.first == w) x.second = v;
                putLocks(pattern(), sel_, l2);
            }
            readPattern();
            lanes_->repaint();
        };
        juce::Component::SafePointer<Fm1SeqPage> self(this);
        r->remove.onClick = [self, w] { juce::MessageManager::callAsync([self, w] { if (self) self->removeLock(self->sel_, w); }); };
        r->remove.setTooltip("Remove the lock");
        for (auto* c : std::initializer_list<juce::Component*>{&r->name, &r->value, &r->remove}) addAndMakeVisible(c);
        lockRows_.push_back(std::move(r));
    }
    addLock_.setEnabled(lockRows_.size() < size_t(fm1::seq::kLocksPerStep));
    loading_ = false;
    if (getWidth() < kNarrow) { resized(); if (onHeightChanged) onHeightChanged(); }
    else resized();
}

void Fm1SeqPage::applyStep() {
    if (loading_) return;
    edit([this](fm1::seq::Pattern& p) {
        auto& s = p.steps[size_t(sel_)];
        s.rate = stepRate_.getSelectedId() - 1;
        s.ratchet = ratchet_.getSelectedId();
        s.gate = stepGate_.getValue() < 5 ? 0 : int(stepGate_.getValue());
        s.chance = int(stepChance_.getValue());
        s.transpose = int(stepTranspose_.getValue());
        s.accent = accent_.getToggleState();
        const bool slide = slide_.getToggleState();   // Tie & Slide: every note held into the next step
        const bool allHeld = !s.notes.empty() && std::all_of(s.notes.begin(), s.notes.end(), [](const fm1::seq::Note& n) { return n.len > 0; });
        if (slide != allHeld) for (auto& n : s.notes) n.len = slide ? std::max(1, n.len) : 0;
    });
}

void Fm1SeqPage::say(const juce::String& text) {
    info_.setText(text, juce::dontSendNotification);
    sayUntil_ = juce::Time::getMillisecondCounter() + 6000;
}

int Fm1SeqPage::playingStep() const {
    return proc_.sequencer.isPlaying() && proc_.sequencer.playingPattern() == proc_.sequencer.selected ? proc_.sequencer.playingStep() : -1;
}

// An 8-Bit preset's parts sit on fixed keys (baud girl's 096: Drums F0-E1, SFX F1-E2, Bass F2-E5,
// Lead F5 and up); every other preset has one Notes lane. The last lane is the Locks.
Fm1SeqPage::Lane Fm1SeqPage::laneAt(int lane) const {
    static const Lane chip[4] = {{"Lead", 77, 127, true}, {"Bass", 41, 76, true}, {"SFX", 29, 40, false}, {"Drums", 17, 28, false}};
    if (!chipLanes()) return {"Notes", 0, 127, true};
    return chip[juce::jlimit(0, 3, lane)];
}

void Fm1SeqPage::timerCallback() {
    if (int v = proc_.patternsVersion.load(); v != seenVersion_) {   // patterns pulled from the synth (or imported)
        seenVersion_ = v;
        readPattern(); loadSettings(); loadStep();
        lanes_->repaint();
    }
    if (pattern_.getSelectedId() != proc_.sequencer.selected + 1) {
        pattern_.setSelectedId(proc_.sequencer.selected + 1, juce::dontSendNotification);
        sel_ = 0;
        readPattern(); loadSettings(); loadStep(); showPage(0);
    }
    const bool playing = proc_.sequencer.isPlaying();
    if (playing && !wasPlaying_) follow_ = true;   // (however it was started: the page follows again)
    wasPlaying_ = playing;
    play_.setToggleState(playing, juce::dontSendNotification);
    play_.setButtonText(playing ? "Stop" : "Play");
    if (enable_.getToggleState() != proc_.sequencer.enabled) enable_.setToggleState(proc_.sequencer.enabled, juce::dontSendNotification);
    const int at = playingStep();
    if (at != lastPlaying_) {
        lastPlaying_ = at;
        if (at >= 0 && follow_ && at / kCols != page_) showPage(at / kCols);   // the page turns with the playhead
        if (proc_.sequencer.recording) readPattern();                          // (real-time recording writes into it)
        lanes_->repaint();
    }
    if (juce::Time::getMillisecondCounter() >= sayUntil_ && info_.getText().isNotEmpty()) info_.setText({}, juce::dontSendNotification);

    // step recording (stopped, Rec on): notes played within a moment are one chord, into the outlined
    // step, which then moves on
    if (rec_.getToggleState() && playing != proc_.sequencer.recording.load()) proc_.sequencer.recording = playing;
    if (!rec_.getToggleState()) proc_.sequencer.recording = false;
    FM1Processor::NoteEvent ev;
    const double now = juce::Time::getMillisecondCounterHiRes();
    auto commit = [this] {
        edit([this](fm1::seq::Pattern& p) {
            auto& s = p.steps[size_t(sel_)];
            if (!overdub_.getToggleState()) s.notes.clear();
            for (const auto& n : recChord_)
                if (int(s.notes.size()) < fm1::seq::kMaxNotes && std::none_of(s.notes.begin(), s.notes.end(), [&](auto& x) { return x.note == n.note; }))
                    s.notes.push_back(n);
        });
        recChord_.clear();
        selectStep((sel_ + 1) % std::max(1, snap_.length));
    };
    while (proc_.popNoteOn(ev)) {
        if (!rec_.getToggleState() || playing) continue;
        if (!recChord_.empty() && now - recLastMs_ > 120.0) commit();
        recChord_.push_back({ev.note, ev.vel, 0});
        recLastMs_ = now;
    }
    if (!recChord_.empty() && now - recLastMs_ > 150.0) commit();
}

void Fm1SeqPage::showExportMenu() {
    juce::PopupMenu m;
    const int current = proc_.sequencer.selected.load();
    m.addSectionHeader("JSON (every setting)");
    m.addItem(1, "This pattern (" + juce::String(current + 1) + ")...");
    m.addItem(2, "All 16 patterns...");
    m.addSectionHeader("MIDI file (the notes as they play)");
    m.addItem(3, "This pattern (" + juce::String(current + 1) + ") as .mid...");
    m.addItem(4, "All 16 patterns as .mid, one track each...");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&export_), [this, current](int id) {
        if (id == 0) return;
        std::vector<int> pats;
        if (id == 1 || id == 3) pats.push_back(current);
        else for (int i = 0; i < Sequencer::kPatterns; ++i) pats.push_back(i);
        const bool midi = id >= 3;
        const juce::String name = (id == 1 || id == 3 ? "fm1-pattern-" + juce::String(current + 1) : juce::String("fm1-patterns")) + (midi ? ".mid" : ".json");
        chooser_ = std::make_unique<juce::FileChooser>(midi ? "Export patterns as a MIDI file" : "Export patterns as JSON",
                                                       juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(name),
                                                       midi ? "*.mid" : "*.json");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, pats, midi](const juce::FileChooser& fc) {
                juce::String shown;
                const bool ok = fm1ui::saveChosen(fc, {}, [this, pats, midi](const juce::File& f) {
                    return midi ? proc_.exportPatternsMidi(f, pats) : proc_.exportJson(f, {}, pats);
                }, shown);
                if (shown.isNotEmpty()) say(ok ? "Saved " + shown : "Could not write " + shown);
            });
    });
}

void Fm1SeqPage::paint(juce::Graphics& g) { g.fillAll(kBg); }

// Desktop: the bars, the lanes, the step along the bottom. Narrow (a phone): the bars wrap and the
// step's controls stack under the lanes (the page scrolls).
void Fm1SeqPage::resized() {
    if (getWidth() < kNarrow) { layoutNarrow(); return; }
    contentBottom_ = 0;
    auto r = getLocalBounds().reduced(8);
    auto bar = [&](int h) { auto b = r.removeFromTop(h); r.removeFromTop(6); return b; };
    {
        auto b = bar(28);
        enable_.setBounds(b.removeFromLeft(54)); b.removeFromLeft(4);
        play_.setBounds(b.removeFromLeft(40)); b.removeFromLeft(4);
        rec_.setBounds(b.removeFromLeft(54)); b.removeFromLeft(10);
        pattern_.setBounds(b.removeFromLeft(120)); b.removeFromLeft(10);
        sync_.setBounds(b.removeFromLeft(110));
        overdub_.setBounds(b.removeFromLeft(90));
        push_.setBounds(b.removeFromRight(110)); b.removeFromRight(4);
        pull_.setBounds(b.removeFromRight(110)); b.removeFromRight(10);
        export_.setBounds(b.removeFromRight(84)); b.removeFromRight(4);
        import_.setBounds(b.removeFromRight(84));
    }
    {
        auto b = bar(24);
        auto field = [&](juce::Label& l, juce::Component& c, int lw, int cw) {
            l.setBounds(b.removeFromLeft(lw)); c.setBounds(b.removeFromLeft(cw)); b.removeFromLeft(10);
        };
        // (fits from 1040 px, the narrowest this layout is used at)
        const int sw = juce::jlimit(70, 130, (b.getWidth() - 640) / 4);
        field(stepsL_, steps_, 38, 58); field(rateL_, rate_, 32, 72); field(swingL_, swing_, 40, sw); field(gateL_, gate_, 32, sw);
        field(chainL_, chain_, 40, 104); field(repeatsL_, repeats_, 52, 56); field(tempoL_, tempo_, 44, sw); field(transposeL_, transpose_, 62, sw);
    }
    {
        auto b = bar(26);
        for (auto& pb : pageButtons_) { pb.setBounds(b.removeFromLeft(64)); b.removeFromLeft(4); }
        b.removeFromLeft(10);
        lanesMode_box_.setBounds(b.removeFromRight(200));
        const bool open = opened_ >= 0;
        back_.setVisible(open);
        const bool roll = open && opened_ < laneCount() - 1 && laneAt(opened_).pitched;
        octDown_.setVisible(roll); octUp_.setVisible(roll);
        if (open) { back_.setBounds(b.removeFromLeft(90)); b.removeFromLeft(4); }
        if (roll) { octDown_.setBounds(b.removeFromLeft(54)); b.removeFromLeft(4); octUp_.setBounds(b.removeFromLeft(54)); }
        info_.setBounds(b.reduced(6, 0));
    }
    auto stepArea = r.removeFromBottom(std::min(230, r.getHeight() / 2));
    r.removeFromBottom(8);
    lanes_->setBounds(r);
    layoutStep(stepArea);
}

void Fm1SeqPage::layoutStep(juce::Rectangle<int> r) {
    const bool narrow = getWidth() < kNarrow;
    // the step's settings, its notes, its locks: three columns (narrow: one under the other)
    auto col1 = narrow ? r.removeFromTop(7 * 26 + 30) : r.removeFromLeft(r.getWidth() * 34 / 100);
    if (!narrow) r.removeFromLeft(12);
    stepTitle_.setBounds(col1.removeFromTop(22));
    auto row = [&](juce::Label& l, juce::Component& c) { auto b = col1.removeFromTop(24); col1.removeFromTop(2); l.setBounds(b.removeFromLeft(70)); c.setBounds(b); };
    row(stepRateL_, stepRate_); row(ratchetL_, ratchet_); row(stepGateL_, stepGate_); row(stepChanceL_, stepChance_); row(stepTransposeL_, stepTranspose_);
    {
        auto b = col1.removeFromTop(24);
        accent_.setBounds(b.removeFromLeft(80)); slide_.setBounds(b.removeFromLeft(110));
    }
    {
        auto b = col1.removeFromTop(26);
        const int w = (b.getWidth() - 12) / 4;
        for (auto* btn : {&copyStep_, &pasteStep_, &clearStep_, &clearPattern_}) { btn->setBounds(b.removeFromLeft(w)); b.removeFromLeft(4); }
        copyStep_.setButtonText(w < 80 ? "Copy" : "Copy step"); pasteStep_.setButtonText(w < 80 ? "Paste" : "Paste step");
        clearStep_.setButtonText(w < 80 ? "Clear" : "Clear step"); clearPattern_.setButtonText(w < 80 ? "Clear all" : "Clear pattern");
    }
    const int allRow = noteRows_.size() > 1 ? 24 : 0;
    auto col2 = narrow ? r.removeFromTop(22 + allRow + 24 * std::max(1, int(noteRows_.size()))) : r.removeFromLeft(r.getWidth() / 2);
    if (!narrow) r.removeFromLeft(12); else r.removeFromTop(8);
    notesTitle_.setBounds(col2.removeFromTop(22));
    if (allRow > 0) {
        auto b = col2.removeFromTop(22); col2.removeFromTop(2);
        allVelL_.setBounds(b.removeFromLeft(chipLanes() ? 74 : 60));
        allVel_.setBounds(b.removeFromLeft(std::max(80, b.getWidth() - 144)));
    }
    notesTitle_.setText(noteRows_.empty() ? "Notes: none (open a lane to add some, or record)" : "Notes", juce::dontSendNotification);
    for (auto& nr : noteRows_) {
        auto b = col2.removeFromTop(22); col2.removeFromTop(2);
        nr->name.setBounds(b.removeFromLeft(chipLanes() ? 74 : 44));
        nr->remove.setBounds(b.removeFromRight(26)); b.removeFromRight(4);
        nr->hold.setBounds(b.removeFromRight(110)); b.removeFromRight(4);
        nr->vel.setBounds(b);
    }
    auto col3 = r;
    locksTitle_.setBounds(col3.removeFromTop(22));
    for (auto& lr : lockRows_) {
        auto b = col3.removeFromTop(22); col3.removeFromTop(2);
        lr->name.setBounds(b.removeFromLeft(110));
        lr->remove.setBounds(b.removeFromRight(26)); b.removeFromRight(4);
        lr->value.setBounds(b);
    }
    addLock_.setBounds(col3.removeFromTop(24).withWidth(std::min(220, col3.getWidth())));
    if (narrow) contentBottom_ = addLock_.getBottom();
}

void Fm1SeqPage::layoutNarrow() {
    auto r = getLocalBounds().reduced(6).withHeight(4000);
    auto bar = [&](int h) { auto b = r.removeFromTop(h); r.removeFromTop(5); return b; };
    {
        auto b = bar(28);
        enable_.setBounds(b.removeFromLeft(50)); b.removeFromLeft(4);
        play_.setBounds(b.removeFromLeft(40)); b.removeFromLeft(4);
        rec_.setBounds(b.removeFromLeft(50)); b.removeFromLeft(6);
        pattern_.setBounds(b);
    }
    {
        auto b = bar(26);
        sync_.setBounds(b.removeFromLeft(b.getWidth() / 2)); overdub_.setBounds(b);
    }
    {
        auto b = bar(28);
        const int w = (b.getWidth() - 12) / 4;
        for (auto* c : {&pull_, &push_, &import_, &export_}) { c->setBounds(b.removeFromLeft(w)); b.removeFromLeft(4); }
        pull_.setButtonText("Pull"); push_.setButtonText("Push");
    }
    auto field = [&](juce::Label& l, juce::Component& c) { auto b = bar(24); l.setBounds(b.removeFromLeft(72)); c.setBounds(b); };
    {
        auto b = bar(24);
        stepsL_.setBounds(b.removeFromLeft(42)); steps_.setBounds(b.removeFromLeft(64)); b.removeFromLeft(8);
        rateL_.setBounds(b.removeFromLeft(34)); rate_.setBounds(b);
    }
    field(swingL_, swing_); field(gateL_, gate_);
    {
        auto b = bar(24);
        chainL_.setBounds(b.removeFromLeft(42)); chain_.setBounds(b.removeFromLeft(b.getWidth() - 120)); b.removeFromLeft(6);
        repeatsL_.setBounds(b.removeFromLeft(54)); repeats_.setBounds(b);
    }
    field(tempoL_, tempo_); field(transposeL_, transpose_);
    {
        auto b = bar(26);
        const int w = (b.getWidth() - 12) / 4;
        for (auto& pb : pageButtons_) { pb.setBounds(b.removeFromLeft(w)); b.removeFromLeft(4); }
    }
    {
        auto b = bar(26);
        const bool open = opened_ >= 0;
        const bool roll = open && opened_ < laneCount() - 1 && laneAt(opened_).pitched;
        back_.setVisible(open); octDown_.setVisible(roll); octUp_.setVisible(roll);
        if (open) { back_.setBounds(b.removeFromLeft(84)); b.removeFromLeft(4); }
        if (roll) { octDown_.setBounds(b.removeFromLeft(50)); b.removeFromLeft(4); octUp_.setBounds(b.removeFromLeft(50)); b.removeFromLeft(4); }
        lanesMode_box_.setBounds(b);
    }
    info_.setBounds(bar(18));
    lanes_->setBounds(r.removeFromTop(opened_ >= 0 ? 380 : 300));
    r.removeFromTop(8);
    layoutStep(r);
}
