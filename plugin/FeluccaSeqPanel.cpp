#include "FeluccaSeqPanel.h"
#include "ChosenFile.h"

#if FM1_FELUCCA

#include "FeluccaDevice.h"
#include "FeluccaEngine.h"
#include "FeluccaMidi.h"
#include "PluginProcessor.h"

namespace {

const juce::Colour kBg(0xff1d1d24), kBox(0xff30303a), kText(0xffe8e8ee), kDim(0xffa0a0b0), kLine(0xff3a3a46),
    kTheme(0xff6fb7c9), kPlay(0xfff0c060);
// SLOOP's track colours (ui_studio.c): blue, green, yellow, orange
const juce::Colour kSloop[4] = {juce::Colour(40, 124, 255), juce::Colour(30, 204, 112), juce::Colour(255, 198, 24), juce::Colour(255, 98, 26)};

juce::String noteName(int n) {
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return juce::String(names[((n % 12) + 12) % 12]) + juce::String(n / 12 - 1);
}

// The device, read through its editor protocol (its replies only; its pushes stay for a live mirror)
struct EngineEndpoint : felucca::Endpoint {
    std::shared_ptr<FeluccaEngine> f;
    const felucca::Dialect& d;
    EngineEndpoint(std::shared_ptr<FeluccaEngine> e, const felucca::Dialect& dl) : f(std::move(e)), d(dl) {}
    const felucca::Dialect& dialect() const override { return d; }
    std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int) override { return f->ask(q); }
    std::vector<fm1::Bytes> pushes() override { return {}; }
};

// a step without note k (its level and ratchet bits follow its notes: SLOOP)
void removeNote(felucca::Step& s, int k) {
    for (int i = k; i + 1 < s.n; ++i) s.note[size_t(i)] = s.note[size_t(i + 1)];
    auto shift = [k](int bits) {
        const int low = bits & ((1 << (2 * k)) - 1), high = bits >> (2 * (k + 1));
        return low | (high << (2 * k));
    };
    s.lvl = shift(s.lvl);
    s.rat = shift(s.rat);
    --s.n;
    s.note[size_t(s.n)] = 0;
}

}  // namespace

// ---- the grid: a piano roll, or drum lanes ---------------------------------------------------------

class FeluccaSeqPage::Grid : public juce::Component {
public:
    explicit Grid(FeluccaSeqPage& p) : page_(p) {}
    static constexpr int kRows = 25;           // two octaves and a note

    juce::Rectangle<float> cell(int col, int row) const {
        const float w = (float(getWidth()) - kLabel) / float(page_.cols_), h = float(getHeight()) / float(rows());
        return {kLabel + col * w, row * h, w, h};
    }
    int rows() const { return page_.drumsView() ? std::max(1, int(page_.lanes_.size())) : kRows; }

    void paint(juce::Graphics& g) override {
        g.fillAll(page_.sloop() ? juce::Colours::black : kBg);
        const bool drums = page_.drumsView();
        const auto colour = page_.trackColour(page_.track_);
        const int first = page_.firstStep(), len = std::max(1, page_.pat_.len);
        // rows: in-scale pitches tinted, the root brighter (the device's piano roll)
        for (int r = 0; r < rows(); ++r) {
            auto row = cell(0, r).withX(kLabel).withWidth(float(getWidth()) - kLabel);
            juce::String label;
            if (drums) {
                label = juce::String(page_.lanes_[size_t(r)]);
            } else {
                const int note = page_.lowNote_ + kRows - 1 - r, pc = ((note - page_.root_) % 12 + 12) % 12;
                if ((page_.scaleMask_ >> pc) & 1u) g.setColour(colour.withAlpha(pc == 0 ? 0.16f : 0.08f)), g.fillRect(row);
                label = note % 12 == 0 || r == 0 ? noteName(note) : juce::String();
            }
            g.setColour(kDim);
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(label, 2, int(row.getY()), int(kLabel) - 4, int(row.getHeight()), juce::Justification::centredLeft);
            g.setColour(kLine.withAlpha(0.6f));
            g.drawHorizontalLine(int(row.getBottom()), kLabel, float(getWidth()));
        }
        // columns: beats every 4 steps, steps past LEN dimmed, the selection and the playhead
        for (int c = 0; c < page_.cols_; ++c) {
            const int step = first + c;
            auto col = cell(c, 0).withHeight(float(getHeight()));
            if (step >= len) g.setColour(juce::Colours::black.withAlpha(0.45f)), g.fillRect(col);
            if (step == page_.sel_) g.setColour(juce::Colours::white.withAlpha(0.07f)), g.fillRect(col);
            if (step == page_.playhead_) g.setColour(kPlay.withAlpha(0.22f)), g.fillRect(col);
            g.setColour(kLine.withAlpha(step % 4 == 0 ? 1.0f : 0.4f));
            g.drawVerticalLine(int(col.getX()), 0.0f, float(getHeight()));
        }
        // the steps
        for (int c = 0; c < page_.cols_; ++c) {
            const int step = first + c;
            if (drums) {
                for (int lane = 0; lane < rows(); ++lane) {
                    auto box = cell(c, lane).reduced(2.5f);
                    if (page_.sloop()) {
                        if (size_t(step) >= page_.pat_.drums.size()) continue;
                        const auto& d = page_.pat_.drums[size_t(step)];
                        if (!d.has(lane)) continue;
                        // the device's shading: ghost dim, soft mid, normal the colour, hard white
                        static const float shade[4] = {1.0f, 0.35f, 0.65f, 1.0f};
                        const int lv = d.level(lane);
                        g.setColour(lv == 3 ? juce::Colours::white : colour.withMultipliedBrightness(shade[lv]));
                        g.fillRoundedRectangle(box, 3.0f);
                        for (int k = 0; k < d.ratchet(lane); ++k)   // ratchet: a notch a repeat
                            g.setColour(juce::Colours::black), g.fillRect(box.getX() + 2.0f + 4.0f * float(k), box.getBottom() - 4.0f, 2.0f, 2.0f);
                    } else {
                        if (size_t(step) >= page_.pat_.steps.size()) continue;
                        const auto& s = page_.pat_.steps[size_t(step)];
                        if (!((s.hit >> lane) & 1)) continue;
                        g.setColour((s.acc >> lane) & 1 ? kPlay : colour);
                        g.fillRoundedRectangle(box, 3.0f);
                    }
                }
                continue;
            }
            if (size_t(step) >= page_.pat_.steps.size()) continue;
            const auto& s = page_.pat_.steps[size_t(step)];
            const bool tieNext = size_t(step + 1) < page_.pat_.steps.size() && page_.pat_.steps[size_t(step + 1)].time == felucca::kTie;
            // a TIE step holds the notes of the step before: they draw through it
            const felucca::Step* sounding = &s;
            {   // (across the loop: a TIE on step 1 holds the last step's notes, as the device plays and draws it)
                const int len = std::clamp(page_.pat_.len, 1, int(page_.pat_.steps.size()));
                int back = step;
                for (int guard = 0; guard < len && sounding != nullptr && sounding->time == felucca::kTie; ++guard) {
                    back = (back + len - 1) % len;
                    sounding = &page_.pat_.steps[size_t(back)];
                }
                if (sounding != nullptr && sounding->time == felucca::kTie) sounding = nullptr;   // all ties: nothing
            }
            if (sounding == nullptr || sounding->time == felucca::kRest) continue;
            for (int k = 0; k < sounding->n; ++k) {
                const int r = page_.lowNote_ + kRows - 1 - sounding->note[size_t(k)];
                if (r < 0 || r >= kRows) continue;
                auto box = cell(c, r).reduced(1.0f, 2.0f);
                if (tieNext) box = box.withRight(cell(c, r).getRight() + 1.0f);
                float alpha = 1.0f;
                if (!page_.sloop() && sounding->chance < 100) alpha = 0.35f + 0.65f * float(std::max(0, sounding->chance)) / 100.0f;
                juce::Colour c;
                if (page_.sloop()) {   // level: ghost dim, soft mid, normal the colour, hard white
                    static const float shade[4] = {1.0f, 0.35f, 0.65f, 1.0f};
                    const int lv = (sounding->lvl >> (2 * k)) & 3;
                    c = lv == 3 ? juce::Colours::white : colour.withMultipliedBrightness(shade[lv]);
                } else {
                    c = ((sounding->flags & felucca::kAccent) ? colour.brighter(0.5f) : colour).withAlpha(alpha);
                }
                g.setColour(&s != sounding ? c.withMultipliedAlpha(0.6f) : c);   // (held by a TIE: fainter)
                g.fillRoundedRectangle(box, 2.0f);
                if (sounding->flags & felucca::kSlide) {   // a slide: a diagonal into the next step
                    g.setColour(juce::Colours::white.withAlpha(0.8f));
                    g.drawLine(box.getX() + 2.0f, box.getBottom() - 1.0f, box.getRight(), box.getY() + 1.0f, 1.2f);
                }
                if (page_.sloop() && &s == sounding)
                    for (int q = 0; q < ((sounding->rat >> (2 * k)) & 3); ++q)
                        g.setColour(juce::Colours::black), g.fillRect(box.getX() + 2.0f + 4.0f * float(q), box.getBottom() - 3.0f, 2.0f, 2.0f);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        const float w = (float(getWidth()) - kLabel) / float(page_.cols_), h = float(getHeight()) / float(rows());
        if (e.position.x < kLabel) return;
        const int col = std::clamp(int((e.position.x - kLabel) / w), 0, page_.cols_ - 1), row = std::clamp(int(e.position.y / h), 0, rows() - 1);
        const int step = page_.firstStep() + col;
        if (e.mods.isPopupMenu() || e.mods.isCommandDown()) { page_.selectStep(step); return; }   // select only
        if (page_.drumsView()) page_.toggleLane(step, row, e.mods.isAltDown() || e.mods.isShiftDown());
        else page_.toggleNote(step, page_.lowNote_ + kRows - 1 - row);
    }

private:
    static constexpr float kLabel = 46.0f;
    FeluccaSeqPage& page_;
};

// ---- Felucca's song chain: rows of a project slot and its repeats ----------------------------------

class FeluccaSeqPage::SongView : public juce::Component {
public:
    explicit SongView(FeluccaSeqPage& p) : page_(p) {
        for (auto* b : {&add_, &play_, &stop_}) addAndMakeVisible(b);
        addAndMakeVisible(status_);
        status_.setColour(juce::Label::textColourId, kDim);
        add_.onClick = [this] { if (rows_.size() < 16) { rows_.push_back({0, 1}); write(); } };
        play_.onClick = [this] { page_.proc_.feluccaEdit(felucca::chainPlay(true)); read(); };
        stop_.onClick = [this] { page_.proc_.feluccaEdit(felucca::chainPlay(false)); read(); };
        add_.setTooltip("A row: a project slot (A-D, saved on the Library tab) played this many times");
        play_.setTooltip("Play the chain from its first row (as SONG > PLAY on the device)");
    }
    void read() {
        auto f = page_.engine();
        if (!f) return;
        EngineEndpoint e(f, page_.dialect());
        auto c = felucca::readChain(e);
        if (!c) return;
        status_.setText(c->running ? "Playing row " + juce::String(c->row + 1) + ", " + juce::String(c->remaining) + " repeat(s) left"
                                   : juce::String(c->rows.size()) + " row(s); the last row stops the song", juce::dontSendNotification);
        if (c->rows == rows_ && !widgets_.empty()) return;
        rows_ = c->rows;
        build();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        auto top = r.removeFromTop(28);
        add_.setBounds(top.removeFromLeft(90));
        top.removeFromLeft(6);
        play_.setBounds(top.removeFromLeft(70));
        top.removeFromLeft(6);
        stop_.setBounds(top.removeFromLeft(70));
        top.removeFromLeft(10);
        status_.setBounds(top);
        r.removeFromTop(8);
        for (auto& w : widgets_) {
            auto row = r.removeFromTop(28);
            w->index.setBounds(row.removeFromLeft(34));
            w->slot.setBounds(row.removeFromLeft(90));
            row.removeFromLeft(8);
            w->repeat.setBounds(row.removeFromLeft(std::min(260, row.getWidth() - 90)));
            row.removeFromLeft(8);
            w->remove.setBounds(row.removeFromLeft(70));
            r.removeFromTop(4);
        }
    }

private:
    struct RowWidgets {
        juce::Label index;
        juce::ComboBox slot;
        juce::Slider repeat{juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft};
        juce::TextButton remove{"Remove"};
    };
    void build() {
        widgets_.clear();
        for (size_t i = 0; i < rows_.size(); ++i) {
            auto w = std::make_unique<RowWidgets>();
            w->index.setText(juce::String(int(i) + 1), juce::dontSendNotification);
            w->index.setColour(juce::Label::textColourId, kDim);
            for (int s = 0; s < 4; ++s) w->slot.addItem("Project " + juce::String::charToString(juce::juce_wchar('A' + s)), s + 1);
            w->slot.setSelectedId(rows_[i].first + 1, juce::dontSendNotification);
            w->repeat.setRange(1, 16, 1);
            w->repeat.setValue(rows_[i].second, juce::dontSendNotification);
            w->repeat.setTextValueSuffix(" x");
            // a change (a drag's end, the wheel, the keys, the text box) is written after the event that
            // made it: writing rebuilds the rows, which must not delete the control while it runs
            w->repeat.onValueChange = [this, i] {
                rows_[i].second = int(widgets_[i]->repeat.getValue());
                if (!widgets_[i]->repeat.isMouseButtonDown()) writeLater();
            };
            w->repeat.onDragEnd = [this] { writeLater(); };
            w->slot.onChange = [this, i] { rows_[i].first = widgets_[i]->slot.getSelectedId() - 1; writeLater(); };
            w->remove.onClick = [this, i] { rows_.erase(rows_.begin() + long(i)); writeLater(); };
            for (auto* c : std::initializer_list<juce::Component*>{&w->index, &w->slot, &w->repeat, &w->remove}) addAndMakeVisible(c);
            widgets_.push_back(std::move(w));
        }
        resized();
    }
    void write() {
        page_.proc_.feluccaEdit(felucca::chainWrite(rows_));
        widgets_.clear();
        read();
    }
    void writeLater() {
        juce::Component::SafePointer<SongView> self(this);
        juce::MessageManager::callAsync([self] { if (self) self->write(); });
    }
    FeluccaSeqPage& page_;
    std::vector<std::pair<int, int>> rows_;
    std::vector<std::unique_ptr<RowWidgets>> widgets_;
    juce::TextButton add_{"Add a row"}, play_{"Play"}, stop_{"Stop"};
    juce::Label status_;
};

// ---- Felucca's motion: the selected track's recorded parameter events -------------------------------

class FeluccaSeqPage::MotionView : public juce::Component {
public:
    explicit MotionView(FeluccaSeqPage& p) : page_(p) {
        addAndMakeVisible(on_);
        addAndMakeVisible(clear_);
        addAndMakeVisible(status_);
        addAndMakeVisible(list_);
        status_.setColour(juce::Label::textColourId, kDim);
        list_.setColour(juce::ListBox::backgroundColourId, kBox);
        on_.onClick = [this] { page_.proc_.feluccaEdit(felucca::motionOn(page_.track_, on_.getToggleState())); read(); };
        clear_.onClick = [this] { page_.proc_.feluccaEdit(felucca::motionClear(page_.track_)); read(); };
        on_.setTooltip("Play this track's motion: the knob moves it recorded, step by step");
        model_.view = this;   // (before the list asks it anything)
        list_.setModel(&model_);
    }
    void read() {
        auto f = page_.engine();
        if (!f) return;
        EngineEndpoint e(f, page_.dialect());
        auto m = felucca::readMotion(e, page_.track_);
        if (!m) return;
        on_.setToggleState(m->on, juce::dontSendNotification);
        events_ = m->events;
        status_.setText(juce::String(int(events_.size())) + " event(s) on this track (" + juce::String(m->max) + " in all, shared by the tracks). "
                        "Record them on the Device tab: REC, then turn knobs while it plays.", juce::dontSendNotification);
        list_.updateContent();
        list_.repaint();
    }
    void resized() override {
        auto r = getLocalBounds().reduced(8);
        auto top = r.removeFromTop(28);
        on_.setBounds(top.removeFromLeft(130));
        clear_.setBounds(top.removeFromLeft(80));
        r.removeFromTop(4);
        status_.setBounds(r.removeFromTop(36));
        list_.setBounds(r);
    }

private:
    struct Model : juce::ListBoxModel {
        MotionView* view = nullptr;
        int getNumRows() override { return int(view->events_.size()); }
        void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) override {
            if (row >= int(view->events_.size())) return;
            const auto& e = view->events_[size_t(row)];
            juce::String what = "parameter " + juce::String(e.param);
            if (auto f = view->page_.engine()) what = juce::String(f->paramDesc(view->page_.track_, e.param).label);
            g.setColour(kText);
            g.setFont(juce::FontOptions(13.0f));
            g.drawText("Step " + juce::String(e.step + 1) + "   " + what + " = " + juce::String(e.value), 8, 0, w - 90, h, juce::Justification::centredLeft);
            g.setColour(kDim);
            g.drawText("delete", w - 70, 0, 60, h, juce::Justification::centredRight);
        }
        void listBoxItemClicked(int row, const juce::MouseEvent& m) override {
            if (row >= int(view->events_.size()) || m.x < view->list_.getWidth() - 80) return;
            const auto e = view->events_[size_t(row)];
            view->page_.proc_.feluccaEdit(felucca::motionDelete(view->page_.track_, e.step, e.param));
            view->read();
        }
    } model_;
    FeluccaSeqPage& page_;
    juce::ToggleButton on_{"Motion plays"};
    juce::TextButton clear_{"Clear"};
    juce::Label status_;
    juce::ListBox list_;
    std::vector<felucca::Motion::Event> events_;
};

// ---- SLOOP's live sections A-D, its song and its mixer -----------------------------------------------
// As its SONG and GLO layers do them. They have no editor command, so they act on this instance's
// SLOOP only (the FM-1's are its own); a track's mute is a parameter, and reaches the FM-1 while Live.

class FeluccaSeqPage::SloopSongView : public juce::Component, private juce::Timer {
public:
    explicit SloopSongView(FeluccaSeqPage& p) : page_(p) {
        addAndMakeVisible(view_);
        view_.setViewedComponent(&content_, false);
        view_.setScrollBarsShown(true, false);
        static const char* const names[4] = {"A", "B", "C", "D"};
        for (int s = 0; s < 4; ++s) {
            play_[s].setButtonText(juce::String("Play ") + names[s]);
            store_[s].setButtonText(juce::String("Store ") + names[s]);
            play_[s].onClick = [this, s] { act(0, s); };
            store_[s].onClick = [this, s] { store(s); };
            play_[s].setTooltip("Playing: this section from the next bar; stopped: it becomes the loop (SAVE + key on the device)");
            store_[s].setTooltip("The loop as it is now, as this section (a stored one: press again within 3 s to replace it)");
            content_.addAndMakeVisible(play_[s]);
            content_.addAndMakeVisible(store_[s]);
        }
        for (int t = 0; t < 4; ++t) {
            mute_[t].setButtonText(t < 3 ? "Mute " + juce::String(t + 1) : juce::String("Mute drums"));
            solo_[t].setButtonText(t < 3 ? "Solo " + juce::String(t + 1) : juce::String("Solo drums"));
            mute_[t].setClickingTogglesState(true);
            solo_[t].setClickingTogglesState(true);
            mute_[t].setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffb04040));
            solo_[t].setColour(juce::TextButton::buttonOnColourId, kSloop[t].darker(0.2f));
            mute_[t].onClick = [this, t] { page_.proc_.feluccaEdit(felucca::paramWrite(t, 40, mute_[t].getToggleState() ? 1 : 0)); };   // P_MUTE
            solo_[t].onClick = [this] {
                unsigned m = 0;
                for (int k = 0; k < 4; ++k) m |= solo_[k].getToggleState() ? 1u << k : 0u;
                if (auto f = page_.engine()) f->arrangementDo(4, int(m));
            };
            content_.addAndMakeVisible(mute_[t]);
            content_.addAndMakeVisible(solo_[t]);
        }
        for (auto* b : {&songMode_, &songRec_, &loop_}) b->setClickingTogglesState(true), content_.addAndMakeVisible(b);
        songMode_.onClick = [this] { act(2, songMode_.getToggleState() ? 1 : 0); };
        songRec_.onClick = [this] { act(3, songRec_.getToggleState() ? 1 : 0); };
        loop_.onClick = [this] { writeChain(); };
        songMode_.setTooltip("Song mode: PLAY plays the song below, section by section; off, the loop");
        songRec_.setTooltip("SONG REC: from the next bar, the sections you play (Play A-D) and how long become the song; off again to finish");
        loop_.setTooltip("At the end of the song, from its start again");
        content_.addAndMakeVisible(add_);
        add_.onClick = [this] { if (chain_.size() < 16) { chain_.push_back({0, 4}); writeChain(); } };
        for (auto* l : {&status_, &sectionsLabel_, &songLabel_, &mixLabel_}) {
            l->setColour(juce::Label::textColourId, kDim);
            content_.addAndMakeVisible(l);
        }
        sectionsLabel_.setText("Sections", juce::dontSendNotification);
        songLabel_.setText("Song (edited while stopped)", juce::dontSendNotification);
        mixLabel_.setText("Mix (on this instance; mute also reaches the FM-1 while Live)", juce::dontSendNotification);
        startTimerHz(5);
    }
    void read() {
        auto f = page_.engine();
        if (!f) return;
        auto a = f->arrangement();
        if (!a) return;
        static const char* const names[4] = {"A", "B", "C", "D"};
        for (int s = 0; s < 4; ++s) {
            const bool stored = (a->stored >> s) & 1u;
            play_[s].setEnabled(stored);
            const auto c = kSloop[s];
            play_[s].setColour(juce::TextButton::buttonColourId, a->playing == s ? c : a->queued == s ? c.withAlpha(0.45f) : kBox);
            store_[s].setColour(juce::TextButton::buttonColourId, stored ? c.darker(0.6f) : kBox);
        }
        songMode_.setToggleState(a->songMode, juce::dontSendNotification);
        songRec_.setToggleState(a->songRec != 0, juce::dontSendNotification);
        loop_.setToggleState(a->loop, juce::dontSendNotification);
        for (int t = 0; t < 4; ++t) {
            solo_[t].setToggleState((a->solo >> t) & 1u, juce::dontSendNotification);
            mute_[t].setToggleState(f->param(t, 40) != 0, juce::dontSendNotification);
        }
        juce::String st = a->songPlays ? "The song plays: part " + juce::String(a->entry + 1) + ", bar " + juce::String(a->bar + 1)
                        : a->playing >= 0 ? juce::String("Playing section ") + names[a->playing] : juce::String("No section yet: store the loop as one");
        if (a->queued >= 0) st << " (next: " << names[a->queued] << ")";
        // SONG REC records from the bar after a section is played (seq.c): none yet, nothing records
        if (a->songRec == 1) st << (a->playing < 0 ? ". SONG REC: play a section first (Play A-D)" : ". SONG REC: from the next bar");
        if (a->songRec == 2) st << ". SONG REC: recording";
        if (a->songMode && !a->songPlays && !chainReady(*a)) st << ". The song has an empty section: PLAY will not start it";
        status_.setText(st, juce::dontSendNotification);
        if (!pending_ && (a->chain != chain_ || rows_.empty())) { chain_ = a->chain; build(); }
    }
    void resized() override {
        view_.setBounds(getLocalBounds());
        const int width = std::max(300, getWidth() - 12);
        auto r = juce::Rectangle<int>(0, 0, width, 2000).reduced(8);
        sectionsLabel_.setBounds(r.removeFromTop(20));
        auto row = r.removeFromTop(30);
        const int w = std::min(120, (row.getWidth() - 12) / 4);
        for (int s = 0; s < 4; ++s) { play_[s].setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); }
        row = r.removeFromTop(30);
        for (int s = 0; s < 4; ++s) { store_[s].setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); }
        r.removeFromTop(4);
        status_.setBounds(r.removeFromTop(36));
        r.removeFromTop(4);
        mixLabel_.setBounds(r.removeFromTop(20));
        row = r.removeFromTop(28);
        for (int t = 0; t < 4; ++t) { mute_[t].setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); }
        row = r.removeFromTop(28);
        for (int t = 0; t < 4; ++t) { solo_[t].setBounds(row.removeFromLeft(w)); row.removeFromLeft(4); }
        r.removeFromTop(8);
        songLabel_.setBounds(r.removeFromTop(20));
        row = r.removeFromTop(28);
        const bool narrow = row.getWidth() < 390;   // (a phone: two rows)
        songMode_.setBounds(row.removeFromLeft(narrow ? row.getWidth() / 2 - 2 : 110)); row.removeFromLeft(4);
        songRec_.setBounds(row.removeFromLeft(narrow ? row.getWidth() : 100)); row.removeFromLeft(4);
        if (narrow) { r.removeFromTop(4); row = r.removeFromTop(28); }
        loop_.setBounds(row.removeFromLeft(70)); row.removeFromLeft(4);
        add_.setBounds(row.removeFromLeft(90));
        r.removeFromTop(6);
        for (auto& rw : rows_) {
            auto line = r.removeFromTop(26);
            rw->index.setBounds(line.removeFromLeft(26));
            rw->section.setBounds(line.removeFromLeft(90));
            line.removeFromLeft(6);
            rw->bars.setBounds(line.removeFromLeft(std::min(240, line.getWidth() - 80)));
            line.removeFromLeft(6);
            rw->remove.setBounds(line.removeFromLeft(70));
            r.removeFromTop(3);
        }
        content_.setSize(width, r.getY() + 8);
    }

private:
    struct Row {
        juce::Label index;
        juce::ComboBox section;
        juce::Slider bars{juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft};
        juce::TextButton remove{"Remove"};
    };
    void timerCallback() override { if (isShowing() && !isMouseButtonDownAnywhere()) read(); }
    static bool chainReady(const FeluccaEngine::Arrangement& a) {
        for (auto [s, bars] : a.chain) if (!((a.stored >> s) & 1u)) return false;
        return !a.chain.empty();
    }
    void act(int op, int arg) {
        auto f = page_.engine();
        if (!f) return;
        static const char* const why[4] = {"", "That section is empty: store the loop as it first", "The song plays: stop it first", "SONG REC is on"};
        const int rc = f->arrangementDo(op, arg);
        if (rc > 0 && rc < 4) page_.say(why[rc]);
        read();
    }
    // a stored section is replaced on a second press within 3 s, as its panel asks ("AGAIN")
    void store(int s) {
        auto f = page_.engine();
        if (!f) return;
        auto a = f->arrangement();
        const auto now = juce::Time::getMillisecondCounter();
        if (a && ((a->stored >> s) & 1u) && !(armed_ == s && now - armedAt_ < 3000)) {
            armed_ = s;
            armedAt_ = now;
            page_.say(juce::String("Section ") + juce::juce_wchar('A' + s) + " is stored: press Store again to replace it with the loop");
            return;
        }
        armed_ = -1;
        f->arrangementDo(1, s);
        page_.say(juce::String("The loop stored as section ") + juce::juce_wchar('A' + s));
        read();
    }
    void build() {
        rows_.clear();
        for (size_t i = 0; i < chain_.size(); ++i) {
            auto rw = std::make_unique<Row>();
            rw->index.setText(juce::String(int(i) + 1), juce::dontSendNotification);
            rw->index.setColour(juce::Label::textColourId, kDim);
            for (int s = 0; s < 4; ++s) rw->section.addItem("Section " + juce::String::charToString(juce::juce_wchar('A' + s)), s + 1);
            rw->section.setSelectedId(chain_[i].first + 1, juce::dontSendNotification);
            rw->bars.setRange(1, 64, 1);
            rw->bars.setValue(chain_[i].second, juce::dontSendNotification);
            rw->bars.setTextValueSuffix(" bars");
            rw->section.onChange = [this, i] { chain_[i].first = rows_[i]->section.getSelectedId() - 1; writeLater(); };
            rw->bars.onValueChange = [this, i] { chain_[i].second = int(rows_[i]->bars.getValue()); pending_ = true; if (!rows_[i]->bars.isMouseButtonDown()) writeLater(); };
            rw->bars.onDragEnd = [this] { writeLater(); };
            rw->remove.onClick = [this, i] { if (chain_.size() > 1) { chain_.erase(chain_.begin() + long(i)); writeLater(); } };
            for (auto* c : std::initializer_list<juce::Component*>{&rw->index, &rw->section, &rw->bars, &rw->remove}) content_.addAndMakeVisible(c);
            rows_.push_back(std::move(rw));
        }
        resized();
    }
    void writeChain() {
        pending_ = false;
        if (auto f = page_.engine()) {
            const int rc = f->setChain(chain_, loop_.getToggleState());
            if (rc == 2) page_.say("Stop first: the song is edited while stopped, as on the device");
        }
        rows_.clear();
        read();
    }
    void writeLater() {
        pending_ = true;   // (until written, the timer's read does not take the device's song over this one)
        juce::Component::SafePointer<SloopSongView> self(this);
        juce::MessageManager::callAsync([self] { if (self) self->writeChain(); });
    }
    FeluccaSeqPage& page_;
    juce::Viewport view_;
    juce::Component content_;
    juce::TextButton play_[4], store_[4], mute_[4], solo_[4], songMode_{"Song mode"}, songRec_{"SONG REC"}, loop_{"Loop"}, add_{"Add a part"};
    juce::Label status_, sectionsLabel_, songLabel_, mixLabel_;
    std::vector<std::pair<int, int>> chain_;
    std::vector<std::unique_ptr<Row>> rows_;
    bool pending_ = false;
    int armed_ = -1;
    juce::uint32 armedAt_ = 0;
};

// ---- the page ---------------------------------------------------------------------------------------

FeluccaSeqPage::FeluccaSeqPage(FM1Processor& p) : proc_(p) {
    grid_ = std::make_unique<Grid>(*this);
    song_ = std::make_unique<SongView>(*this);
    sloopSong_ = std::make_unique<SloopSongView>(*this);
    motion_ = std::make_unique<MotionView>(*this);
    addChildComponent(*sloopSong_);
    addAndMakeVisible(*grid_);
    addChildComponent(*song_);
    addChildComponent(*motion_);
    for (int i = 0; i < 4; ++i) {
        auto& b = trackButtons_[i];
        b.setClickingTogglesState(true);
        b.setRadioGroupId(4901);
        b.onClick = [this, i] { if (trackButtons_[i].getToggleState()) selectTrack(i); };
        addAndMakeVisible(b);
        auto& pb = pageButtons_[i];
        pb.setClickingTogglesState(true);
        pb.setRadioGroupId(4902);
        pb.onClick = [this, i] { if (pageButtons_[i].getToggleState()) { page_ = i; grid_->repaint(); } };
        addAndMakeVisible(pb);
    }
    trackButtons_[0].setToggleState(true, juce::dontSendNotification);
    pageButtons_[0].setToggleState(true, juce::dontSendNotification);
    for (auto* b : {&notesView_, &drumsView_}) {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4903);
        addAndMakeVisible(b);
    }
    notesView_.onClick = [this] { drumsChoice_ = false; forceDrums_ = true; grid_->repaint(); loadControls(); resized(); };
    drumsView_.onClick = [this] { drumsChoice_ = true; forceDrums_ = true; grid_->repaint(); loadControls(); resized(); };
    notesView_.setTooltip("The steps' notes, on a piano roll");
    drumsView_.setTooltip("The steps' drum lanes (Felucca: any track's 8 lanes, which a DRUM engine plays)");
    for (auto* b : {&patternTab_, &songTab_, &motionTab_}) {
        b->setClickingTogglesState(true);
        b->setRadioGroupId(4904);
        addAndMakeVisible(b);
    }
    patternTab_.setToggleState(true, juce::dontSendNotification);
    patternTab_.onClick = [this] { showView(0); };
    songTab_.onClick = [this] { showView(1); };
    motionTab_.onClick = [this] { showView(2); };
    play_.setClickingTogglesState(true);
    play_.setColour(juce::TextButton::buttonOnColourId, juce::Colours::seagreen);
    play_.onClick = [this] {
        auto f = engine();
        if (!f) return;
        if (play_.getToggleState() && sloop())
            if (auto a = f->arrangement(); a && a->songMode && !a->songPlays) {   // as its SONG page: an empty section does not start
                for (auto [sec, bars] : a->chain)
                    if (!((a->stored >> sec) & 1u)) {
                        say(juce::String("The song has an empty section (") + juce::juce_wchar('A' + sec) + "): store it first, or turn song mode off");
                        play_.setToggleState(false, juce::dontSendNotification);
                        return;
                    }
            }
        f->transport(play_.getToggleState());
    };
    play_.setTooltip("Start or stop this instance's sequencer (with \"Tempo follows the host\" on, the host's transport does)");
    for (auto* b : {&octDown_, &octUp_}) addAndMakeVisible(b);
    octDown_.onClick = [this] { lowNote_ = std::max(0, lowNote_ - 12); grid_->repaint(); };
    octUp_.onClick = [this] { lowNote_ = std::min(127 - Grid::kRows + 1, lowNote_ + 12); grid_->repaint(); };
    for (auto* b : std::initializer_list<juce::Component*>{&play_, &pull_, &send_}) addAndMakeVisible(b);
    pull_.setTooltip("Every track's pattern from the connected FM-1 (Felucca: and its song chain and motion)");
    send_.setTooltip("Every track's pattern to the connected FM-1 (Felucca: and its song chain and motion); its RAM, not saved there");
    pull_.onClick = [this] { proc_.feluccaPullPatterns(); };
    for (auto* b : {&importMidi_, &exportMidi_}) addAndMakeVisible(b);
    exportMidi_.setTooltip("Every track's pattern as a MIDI file: one pass of each, as the sequencer plays it (drum lanes on channel 10)");
    importMidi_.setTooltip("A MIDI file's notes onto the selected track's steps at its DIV (or onto its drum lanes), replacing them");
    exportMidi_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Export the patterns as a MIDI file", juce::File(), "*.mid");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
            [this](const juce::FileChooser& c) {
                juce::String name;
                const bool ok = fm1ui::saveChosen(c, ".mid", [this](const juce::File& f) {
                    juce::FileOutputStream out(f);
                    return out.openedOk() && (out.setPosition(0), out.truncate(), exportMidi().writeTo(out));
                }, name);
                if (name.isNotEmpty()) say(ok ? "Exported " + name : "Could not write " + name);
            });
    };
    importMidi_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Import a MIDI file onto this track", juce::File(), "*.mid;*.midi");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& c) {
            auto f = fm1ui::openedFile(c);
            if (!f.existsAsFile()) return;
            juce::FileInputStream in(f);
            juce::MidiFile file;
            say(in.openedOk() && file.readFrom(in) ? importMidi(file) : "Not a MIDI file: " + f.getFileName());
        });
    };
    send_.onClick = [this] { proc_.feluccaSendPatterns(); };

    auto label = [this](juce::Label& l, const juce::String& t) {
        l.setText(t, juce::dontSendNotification);
        l.setColour(juce::Label::textColourId, kDim);
        l.setFont(juce::FontOptions(12.0f));
        addAndMakeVisible(l);
    };
    label(lenLabel_, "LEN");
    label(divLabel_, "DIV");
    label(swingLabel_, "SWG");
    label(gateLabel_, "GATE");
    label(velLabel_, "VEL");
    label(chanceLabel_, "CHANCE");
    label(stepLabel_, "");
    label(info_, "");
    stepLabel_.setColour(juce::Label::textColourId, kText);
    for (int i = 1; i <= felucca::kSteps; ++i) len_.addItem(juce::String(i), i);
    len_.onChange = [this] { if (!loading_) setPatternParam(felucca::kLen, len_.getSelectedId()); };
    div_.onChange = [this] { if (!loading_) setPatternParam(felucca::kDiv, div_.getSelectedId() - 1); };
    for (auto* s : {&swing_, &gate_, &vel_, &chance_}) {
        s->setSliderStyle(juce::Slider::LinearHorizontal);
        s->setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 18);
        addAndMakeVisible(s);
    }
    swing_.setRange(0, 100, 1);
    gate_.setRange(1, 127, 1);
    vel_.setRange(0, 127, 1);
    chance_.setRange(0, 100, 1);
    swing_.onValueChange = [this] { if (!loading_) setPatternParam(felucca::kSwing, int(swing_.getValue())); };
    gate_.onValueChange = [this] { if (!loading_) setPatternParam(felucca::kGate, int(gate_.getValue())); };
    vel_.onValueChange = [this] { if (!loading_) setStepVelocity(int(vel_.getValue())); };
    chance_.onValueChange = [this] { if (!loading_) setStepChance(int(chance_.getValue())); };
    vel_.setTooltip("The step's velocity (0: the default, 96; an accent plays 127)");
    chance_.setTooltip("How often the step plays, in percent: 100 always, 0 never");
    time_.addItem("NOTE", 1);
    time_.addItem("TIE", 2);
    time_.addItem("REST", 3);
    time_.setTooltip("NOTE plays its notes; TIE holds the step before's on; REST is silent");
    time_.onChange = [this] { if (!loading_) setStepTime(time_.getSelectedId() - 1); };
    for (auto* c : std::initializer_list<juce::Component*>{&len_, &div_, &time_, &accent_, &slide_}) addAndMakeVisible(c);
    accent_.onClick = [this] { if (!loading_) setStepFlag(felucca::kAccent, accent_.getToggleState()); };
    slide_.onClick = [this] { if (!loading_) setStepFlag(felucca::kSlide, slide_.getToggleState()); };
    static const char* const levels[4] = {"normal", "ghost", "soft", "hard"};
    for (int l = 0; l < 4; ++l) paintLevel_.addItem(levels[l], l + 1), paintRatchet_.addItem("x" + juce::String(l + 1), l + 1);
    paintLevel_.setSelectedId(1, juce::dontSendNotification);
    paintRatchet_.setSelectedId(1, juce::dontSendNotification);
    paintLevel_.setTooltip("The level a click puts on a drum hit (click a hit again with the same level and ratchet to take it off)");
    paintRatchet_.setTooltip("The ratchet a click puts on a drum hit: x1 to x4");
    addChildComponent(paintLevel_);
    addChildComponent(paintRatchet_);
    for (int k = 0; k < 4; ++k) {
        for (int l = 0; l < 4; ++l) noteLevel_[k].addItem(levels[l], l + 1), noteRatchet_[k].addItem("x" + juce::String(l + 1), l + 1);
        noteLevel_[k].onChange = [this, k] { if (!loading_) setNoteLevel(k, noteLevel_[k].getSelectedId() - 1); };
        noteRatchet_[k].onChange = [this, k] { if (!loading_) setNoteRatchet(k, noteRatchet_[k].getSelectedId() - 1); };
        noteName_[k].setColour(juce::Label::textColourId, kText);
        addChildComponent(noteLevel_[k]);
        addChildComponent(noteRatchet_[k]);
        addChildComponent(noteName_[k]);
    }
    refresh();
    fitNotes();
    startTimerHz(10);
}

FeluccaSeqPage::~FeluccaSeqPage() = default;

std::shared_ptr<FeluccaEngine> FeluccaSeqPage::engine() const { return proc_.felucca(); }

const felucca::Dialect& FeluccaSeqPage::dialect() const {
    auto f = engine();
    return f ? felucca::dialectOf(*f) : felucca::feluccaDialect();
}

bool FeluccaSeqPage::sloop() const { return felucca::isSloop(dialect()); }

bool FeluccaSeqPage::drumsView() const {
    if (sloop()) return felucca::isDrumTrack(dialect(), track_);
    if (forceDrums_) return drumsChoice_;
    auto f = engine();
    return f && f->engineName(f->engineOf(track_)) == "DRUM";
}

juce::Colour FeluccaSeqPage::trackColour(int t) const { return sloop() ? kSloop[t & 3] : kTheme; }

void FeluccaSeqPage::refresh() {
    auto f = engine();
    for (int i = 0; i < 4; ++i) {
        trackButtons_[i].setVisible(f && i < f->tracks());
        trackButtons_[i].setButtonText(sloop() && f && i >= f->parts() ? juce::String("DRUMS") : "PART " + juce::String(i + 1));
        if (sloop()) trackButtons_[i].setColour(juce::TextButton::buttonOnColourId, kSloop[i].darker(0.3f));
    }
    songTab_.setVisible(true);         // (SLOOP's: its sections, song and mix)
    motionTab_.setVisible(!sloop());
    notesView_.setVisible(!sloop());
    drumsView_.setVisible(!sloop());
    if (f) {
        auto d = f->paramDesc(track_, felucca::kDiv);
        div_.clear(juce::dontSendNotification);
        for (size_t i = 0; i < d.names.size(); ++i) div_.addItem(d.names[i], int(i) + 1);
    }
    readContext();
    readPattern();
    loadControls();
    if (view_ == 1) { if (sloop()) sloopSong_->read(); else song_->read(); }
    if (view_ == 2) motion_->read();
    resized();
    repaint();
}

void FeluccaSeqPage::readContext() {
    auto f = engine();
    if (!f) return;
    scaleMask_ = f->scaleMask(track_);
    root_ = f->param(track_, 25);   // P_ROOT (both firmwares)
    lanes_ = f->laneNames(track_);
}

void FeluccaSeqPage::readPattern() {
    auto f = engine();
    if (!f) return;
    EngineEndpoint e(f, dialect());
    juce::String err;
    if (auto p = felucca::readPattern(e, track_, err)) pat_ = *p;
}

void FeluccaSeqPage::visibilityChanged() { if (isVisible()) refresh(); }

void FeluccaSeqPage::timerCallback() {
    if (!isShowing()) return;
    auto f = engine();
    if (!f) return;
    const int at = f->stepOf(track_);
    if (at != playhead_) {
        playhead_ = at;
        grid_->repaint();
    }
    play_.setToggleState(f->playing(), juce::dontSendNotification);
    const bool synth = proc_.feluccaSynth() && !proc_.session.busy();
    pull_.setEnabled(synth);
    send_.setEnabled(synth && proc_.feluccaWritesRefused().isEmpty());
    if (juce::Time::getMillisecondCounter() >= sayUntil_)   // (a message said lately stays)
        info_.setText(proc_.feluccaLiveOn() ? "Live with the FM-1: every edit here reaches it." :
                      proc_.feluccaSynth() ? "Connected: Pull or Send the patterns, or turn Live on (top bar)." : "",
                      juce::dontSendNotification);
    // what the device itself changed (its panel, recording, a synced FM-1): read again now and then,
    // not while the mouse is down on a control
    if (++tick_ % 5 == 0 && !isMouseButtonDownAnywhere()) {
        const auto before = pat_;
        readContext();
        readPattern();
        if (!(before == pat_)) { loadControls(); grid_->repaint(); }
        if (view_ == 1 && tick_ % 10 == 0 && !sloop()) song_->read();
        if (view_ == 2 && tick_ % 10 == 0) motion_->read();
    }
    if (f->selected() != track_ && f->selected() < f->tracks() && !isMouseButtonDownAnywhere()) {   // chosen on the device
        track_ = f->selected();
        trackButtons_[track_].setToggleState(true, juce::dontSendNotification);
        refresh();
        fitNotes();
    }
}

void FeluccaSeqPage::selectTrack(int t) {
    track_ = t;
    forceDrums_ = false;
    if (t >= 0 && t < 4) trackButtons_[t].setToggleState(true, juce::dontSendNotification);
    if (auto f = engine()) f->select(t);   // the device's selected part too: its keys play it
    sel_ = 0;
    refresh();
    fitNotes();
}

// the piano roll's two octaves where the track's notes are (its lowest, from a C)
void FeluccaSeqPage::fitNotes() {
    int lo = 128, hi = -1;
    for (const auto& s : pat_.steps)
        if (s.time == felucca::kNote)
            for (int k = 0; k < s.n; ++k) lo = std::min(lo, int(s.note[size_t(k)])), hi = std::max(hi, int(s.note[size_t(k)]));
    if (hi < 0) return;   // none: where it was
    int low = lo / 12 * 12;
    if (hi - low >= Grid::kRows) low = std::max(0, (lo + hi) / 2 - Grid::kRows / 2);
    lowNote_ = std::clamp(low, 0, 127 - Grid::kRows + 1);
    grid_->repaint();
}

void FeluccaSeqPage::selectStep(int step) {
    sel_ = std::clamp(step, 0, felucca::kSteps - 1);
    loadControls();
    grid_->repaint();
}

void FeluccaSeqPage::writeStep(int step) {
    if (step < 0 || size_t(step) >= pat_.steps.size()) return;
    proc_.feluccaEdit(felucca::stepWrite(dialect(), track_, step, pat_.steps[size_t(step)]));
}

void FeluccaSeqPage::writeDrum(int step) {
    if (step < 0 || size_t(step) >= pat_.drums.size()) return;
    proc_.feluccaEdit(felucca::drumWrite(dialect(), step, pat_.drums[size_t(step)]));
}

void FeluccaSeqPage::toggleNote(int step, int note) {
    if (step < 0 || size_t(step) >= pat_.steps.size() || note < 0 || note > 127) return;
    auto& s = pat_.steps[size_t(step)];
    int at = -1;
    for (int k = 0; k < s.n; ++k) if (s.note[size_t(k)] == note) at = k;
    if (at >= 0 && s.time == felucca::kNote) {
        removeNote(s, at);
        if (s.n == 0) s.time = felucca::kRest;
    } else {
        if (s.time != felucca::kNote) { s.n = 0; s.note = {}; s.lvl = 0; s.rat = 0; s.time = felucca::kNote; }
        if (s.n >= 4) removeNote(s, 0);   // (four at most: the oldest goes)
        s.note[size_t(s.n++)] = uint8_t(note);
    }
    sel_ = step;
    writeStep(step);
    loadControls();
    grid_->repaint();
}

void FeluccaSeqPage::toggleLane(int step, int lane, bool accent) {
    if (sloop()) {
        if (size_t(step) >= pat_.drums.size() || lane < 0 || lane >= 16) return;
        auto& d = pat_.drums[size_t(step)];
        const int lv = paintLevel_.getSelectedId() - 1, rt = paintRatchet_.getSelectedId() - 1;
        const bool same = d.has(lane) && d.level(lane) == lv && d.ratchet(lane) == rt;
        d.set(lane, !same, lv, rt);   // a click paints the level and ratchet; on the same again, off
        sel_ = step;
        writeDrum(step);
    } else {
        if (size_t(step) >= pat_.steps.size() || lane < 0 || lane >= 8) return;
        auto& s = pat_.steps[size_t(step)];
        const int bit = 1 << lane;
        if (!(s.hit & bit) && s.time != felucca::kNote) { s = felucca::Step{}; s.time = felucca::kNote; }   // a hit plays on a NOTE step (ui.c grid_hit)
        if (accent) { s.acc ^= bit; s.hit |= bit; }
        else if (s.hit & bit) { s.hit &= ~bit; s.acc &= ~bit; }
        else s.hit |= bit;
        sel_ = step;
        writeStep(step);
    }
    loadControls();
    grid_->repaint();
}

void FeluccaSeqPage::setStepTime(int time) {
    if (size_t(sel_) >= pat_.steps.size()) return;
    pat_.steps[size_t(sel_)].time = std::clamp(time, 0, 2);
    writeStep(sel_);
    grid_->repaint();
}

void FeluccaSeqPage::setStepFlag(int flag, bool on) {
    if (size_t(sel_) >= pat_.steps.size()) return;
    auto& s = pat_.steps[size_t(sel_)];
    s.flags = on ? (s.flags | flag) : (s.flags & ~flag);
    writeStep(sel_);
    grid_->repaint();
}

void FeluccaSeqPage::setStepVelocity(int v) {
    if (size_t(sel_) >= pat_.steps.size()) return;
    pat_.steps[size_t(sel_)].vel = std::clamp(v, 0, 127);
    writeStep(sel_);
}

void FeluccaSeqPage::setStepChance(int c) {
    if (size_t(sel_) >= pat_.steps.size()) return;
    pat_.steps[size_t(sel_)].chance = std::clamp(c, 0, 100);
    writeStep(sel_);
    grid_->repaint();
}

void FeluccaSeqPage::setNoteLevel(int k, int level) {
    if (size_t(sel_) >= pat_.steps.size()) return;
    auto& s = pat_.steps[size_t(sel_)];
    s.lvl = (s.lvl & ~(3 << (2 * k))) | ((level & 3) << (2 * k));
    writeStep(sel_);
    grid_->repaint();
}

void FeluccaSeqPage::setNoteRatchet(int k, int ratchet) {
    if (size_t(sel_) >= pat_.steps.size()) return;
    auto& s = pat_.steps[size_t(sel_)];
    s.rat = (s.rat & ~(3 << (2 * k))) | ((ratchet & 3) << (2 * k));
    writeStep(sel_);
    grid_->repaint();
}

void FeluccaSeqPage::setPatternParam(int id, int value) {
    proc_.feluccaEdit(felucca::paramWrite(track_, id, value));
    if (auto f = engine()) {   // as it took it (clamped)
        const int v = f->param(track_, id);
        if (id == felucca::kLen) pat_.len = v;
        if (id == felucca::kDiv) pat_.div = v;
        if (id == felucca::kSwing) pat_.swing = v;
        if (id == felucca::kGate) pat_.gate = v;
    }
    proc_.feluccaChanged(track_);   // the host's parameter follows
    grid_->repaint();
}

void FeluccaSeqPage::say(const juce::String& text) {
    info_.setText(text, juce::dontSendNotification);
    sayUntil_ = juce::Time::getMillisecondCounter() + 6000;
}

juce::MidiFile FeluccaSeqPage::exportMidi() const {
    felmidi::Song song;
    auto f = engine();
    if (!f) return {};
    song.sloop = sloop();
    song.bpm = f->global(0);   // G_BPM (both)
    EngineEndpoint e(f, dialect());
    for (int t = 0; t < f->tracks(); ++t) {
        juce::String err;
        auto p = felucca::readPattern(e, t, err);
        if (!p) continue;
        felmidi::Track tr;
        tr.index = t;
        tr.pattern = *p;
        tr.drums = felucca::isDrumTrack(dialect(), t);
        tr.swing = std::clamp(p->swing + f->global(1), 0, 100);   // its own plus G_SWING
        const auto names = f->paramDesc(t, felucca::kDiv).names;
        tr.stepQuarters = felmidi::divQuarters(size_t(p->div) < names.size() ? names[size_t(p->div)] : "1/16");
        song.tracks.push_back(tr);
    }
    return felmidi::toMidi(song);
}

juce::String FeluccaSeqPage::importMidi(const juce::MidiFile& file) {
    auto f = engine();
    if (!f) return {};
    const auto names = f->paramDesc(track_, felucca::kDiv).names;
    const double q = felmidi::divQuarters(size_t(pat_.div) < names.size() ? names[size_t(pat_.div)] : "1/16");
    const bool drums = drumsView();
    auto r = felmidi::fromMidi(file, pat_, sloop(), drums, q, track_);
    const auto where = trackButtons_[track_].getButtonText();
    if (r.smpte) return "The file is timed in SMPTE frames, not beats: nothing imported";
    if (r.notes == 0) return juce::String("No ") + (drums ? "drum notes" : "notes") + " in the file for " + where + ": nothing changed";
    // written as edits: to this instance's device, and to the FM-1 while Live
    struct EditEndpoint : felucca::Endpoint {
        FM1Processor& p;
        const felucca::Dialect& d;
        EditEndpoint(FM1Processor& pr, const felucca::Dialect& dl) : p(pr), d(dl) {}
        const felucca::Dialect& dialect() const override { return d; }
        std::optional<fm1::Bytes> ask(const fm1::Bytes& q, int) override { return p.feluccaEdit(q); }
        std::vector<fm1::Bytes> pushes() override { return {}; }
    } edit(proc_, dialect());
    juce::String err;
    const auto known = pat_;
    if (!felucca::writePattern(edit, track_, r.pattern, err, &known)) return "The import stopped: " + err;
    readPattern();
    if (!drums) fitNotes();
    loadControls();
    grid_->repaint();
    juce::String said = juce::String(r.notes) + (drums ? " drum hits" : " notes") + " on " + where + ", LEN " + juce::String(r.pattern.len);
    if (r.from.isNotEmpty()) said << " (from " << r.from << ")";
    if (r.crowded) said << "; " << r.crowded << " left out (4 notes a step at most)";
    if (r.pastEnd) said << "; " << r.pastEnd << " past step 64 left out";
    return said;
}

void FeluccaSeqPage::loadControls() {
    loading_ = true;
    len_.setSelectedId(std::clamp(pat_.len, 1, felucca::kSteps), juce::dontSendNotification);
    div_.setSelectedId(pat_.div + 1, juce::dontSendNotification);
    swing_.setValue(pat_.swing, juce::dontSendNotification);
    gate_.setValue(pat_.gate, juce::dontSendNotification);
    const bool drums = drumsView(), slp = sloop();
    notesView_.setToggleState(!drums, juce::dontSendNotification);
    drumsView_.setToggleState(drums, juce::dontSendNotification);
    paintLevel_.setVisible(slp && drums);
    paintRatchet_.setVisible(slp && drums);
    const bool stepCtl = !(slp && drums);   // SLOOP's drum steps: their lanes are all there is
    for (auto* c : std::initializer_list<juce::Component*>{&time_, &accent_, &slide_, &vel_, &velLabel_}) c->setVisible(stepCtl);
    chance_.setVisible(stepCtl && !slp);
    chanceLabel_.setVisible(stepCtl && !slp);
    juce::String text = "Step " + juce::String(sel_ + 1);
    if (size_t(sel_) < pat_.steps.size()) {
        const auto& s = pat_.steps[size_t(sel_)];
        time_.setSelectedId(s.time + 1, juce::dontSendNotification);
        accent_.setToggleState(s.flags & felucca::kAccent, juce::dontSendNotification);
        slide_.setToggleState(s.flags & felucca::kSlide, juce::dontSendNotification);
        vel_.setValue(s.vel, juce::dontSendNotification);
        chance_.setValue(s.chance, juce::dontSendNotification);
        juce::StringArray notes;
        for (int k = 0; k < s.n; ++k) notes.add(noteName(s.note[size_t(k)]));
        if (!drums) text << ":  " << (s.time == felucca::kRest ? juce::String("rest") : s.time == felucca::kTie ? juce::String("tie") : notes.joinIntoString(" "));
        for (int k = 0; k < 4; ++k) {
            const bool show = slp && !drums && k < s.n && s.time == felucca::kNote;
            noteName_[k].setVisible(show);
            noteLevel_[k].setVisible(show);
            noteRatchet_[k].setVisible(show);
            if (!show) continue;
            noteName_[k].setText(noteName(s.note[size_t(k)]), juce::dontSendNotification);
            noteLevel_[k].setSelectedId(((s.lvl >> (2 * k)) & 3) + 1, juce::dontSendNotification);
            noteRatchet_[k].setSelectedId(((s.rat >> (2 * k)) & 3) + 1, juce::dontSendNotification);
        }
    } else {
        for (int k = 0; k < 4; ++k) { noteName_[k].setVisible(false); noteLevel_[k].setVisible(false); noteRatchet_[k].setVisible(false); }
    }
    stepLabel_.setText(text, juce::dontSendNotification);
    loading_ = false;
}

void FeluccaSeqPage::showView(int v) {
    view_ = v;
    (v == 1 ? songTab_ : v == 2 ? motionTab_ : patternTab_).setToggleState(true, juce::dontSendNotification);
    grid_->setVisible(v == 0);
    song_->setVisible(v == 1 && !sloop());
    sloopSong_->setVisible(v == 1 && sloop());
    motion_->setVisible(v == 2);
    if (v == 1) { if (sloop()) sloopSong_->read(); else song_->read(); }
    if (v == 2) motion_->read();
    resized();
}

void FeluccaSeqPage::paint(juce::Graphics& g) { g.fillAll(kBg); }

// Desktop: the bars on top, the grid, the step's details along the bottom. Narrow (a phone): the
// bars wrap, the grid shows 16 steps, the details stack under it.
void FeluccaSeqPage::resized() {
    auto r = getLocalBounds().reduced(6);
    const bool narrow = getWidth() < 760;
    auto bar = [&](int h) { auto b = r.removeFromTop(h); r.removeFromTop(4); return b; };
    {   // tracks, play, views
        auto b = bar(28);
        const int tw = narrow ? (b.getWidth() - 44) / 4 : 74;
        for (int i = 0; i < 4; ++i) if (trackButtons_[i].isVisible()) trackButtons_[i].setBounds(b.removeFromLeft(tw - 4)), b.removeFromLeft(4);
        play_.setBounds(b.removeFromLeft(40));
        if (!narrow) {
            b.removeFromLeft(12);
            for (auto* t : {&patternTab_, &songTab_, &motionTab_}) if (t->isVisible()) t->setBounds(b.removeFromLeft(80)), b.removeFromLeft(4);
            b.removeFromLeft(8);
            send_.setBounds(b.removeFromRight(120));
            b.removeFromRight(4);
            pull_.setBounds(b.removeFromRight(120));
            b.removeFromRight(10);
            exportMidi_.setBounds(b.removeFromRight(110));
            b.removeFromRight(4);
            importMidi_.setBounds(b.removeFromRight(110));
            info_.setBounds(b);
        }
    }
    if (narrow) {
        auto b = bar(28);
        for (auto* t : {&patternTab_, &songTab_, &motionTab_}) if (t->isVisible()) t->setBounds(b.removeFromLeft(80)), b.removeFromLeft(4);
        b = bar(28);
        const int q4 = (b.getWidth() - 12) / 4;
        for (auto* c : {&pull_, &send_, &importMidi_, &exportMidi_}) { c->setBounds(b.removeFromLeft(q4)); b.removeFromLeft(4); }
        info_.setBounds(bar(18));   // (a phone: the messages on a line of their own)
    }
    if (view_ != 0) {
        for (auto* c : std::initializer_list<juce::Component*>{&len_, &div_, &swing_, &gate_, &lenLabel_, &divLabel_, &swingLabel_, &gateLabel_,
                                                                &octDown_, &octUp_, &notesView_, &drumsView_, &stepLabel_})
            c->setBounds(0, 0, 0, 0);
        for (auto& b : pageButtons_) b.setBounds(0, 0, 0, 0);
        layoutControls({});
        (view_ == 1 ? (sloop() ? static_cast<juce::Component&>(*sloopSong_) : *song_) : static_cast<juce::Component&>(*motion_)).setBounds(r);
        return;
    }
    // the pattern's settings, the pages, the view and the octave
    {   // 16, 32 or 64 steps across, as many as fit at 16 pixels a step or more
        const int room = (r.getWidth() - 46) / 16;
        cols_ = narrow || room < 32 ? 16 : room < 64 ? 32 : 64;
    }
    for (int i = 0; i < 4; ++i) {
        pageButtons_[i].setVisible(cols_ < felucca::kSteps && i * cols_ < felucca::kSteps);   // (one page: none)
        pageButtons_[i].setButtonText(juce::String(i * cols_ + 1) + "-" + juce::String(std::min(felucca::kSteps, (i + 1) * cols_)));
    }
    if (page_ * cols_ >= felucca::kSteps) { page_ = 0; pageButtons_[0].setToggleState(true, juce::dontSendNotification); }
    {
        auto b = bar(26);
        auto pair = [&](juce::Label& l, juce::Component& c, int w) { l.setBounds(b.removeFromLeft(narrow ? 34 : 38)); c.setBounds(b.removeFromLeft(w)); b.removeFromLeft(8); };
        pair(lenLabel_, len_, 64);
        pair(divLabel_, div_, 80);
        if (narrow) b = bar(26);
        pair(swingLabel_, swing_, narrow ? 120 : 130);
        pair(gateLabel_, gate_, narrow ? 120 : 130);
        if (narrow) b = bar(26);
        const int views = notesView_.isVisible() ? 2 * (narrow ? 54 : 60) + 8 : 0, pw = narrow ? std::max(40, (b.getWidth() - views - 72) / 4 - 3) : 60;
        for (auto& pb : pageButtons_) if (pb.isVisible()) pb.setBounds(b.removeFromLeft(pw)), b.removeFromLeft(3);
        b.removeFromLeft(8);
        if (notesView_.isVisible()) { notesView_.setBounds(b.removeFromLeft(narrow ? 54 : 60)); drumsView_.setBounds(b.removeFromLeft(narrow ? 54 : 60)); b.removeFromLeft(8); }
        const bool drums = drumsView();
        octDown_.setVisible(!drums);
        octUp_.setVisible(!drums);
        octDown_.setBounds(b.removeFromLeft(28));
        octUp_.setBounds(b.removeFromLeft(28));
    }
    // the step's details along the bottom (narrow: two rows)
    auto details = r.removeFromBottom(narrow ? 120 : 64);
    r.removeFromBottom(4);
    grid_->setBounds(r);
    layoutControls(details);
}

void FeluccaSeqPage::layoutControls(juce::Rectangle<int> r) {
    if (r.isEmpty()) {
        for (auto* c : std::initializer_list<juce::Component*>{&stepLabel_, &time_, &accent_, &slide_, &vel_, &velLabel_, &chance_, &chanceLabel_,
                                                                &paintLevel_, &paintRatchet_})
            c->setBounds(0, 0, 0, 0);
        for (int k = 0; k < 4; ++k) noteName_[k].setBounds(0, 0, 0, 0), noteLevel_[k].setBounds(0, 0, 0, 0), noteRatchet_[k].setBounds(0, 0, 0, 0);
        return;
    }
    auto row = r.removeFromTop(28);
    stepLabel_.setBounds(row.removeFromLeft(std::min(260, row.getWidth() / 3)));
    if (paintLevel_.isVisible()) {
        paintLevel_.setBounds(row.removeFromLeft(100));
        row.removeFromLeft(6);
        paintRatchet_.setBounds(row.removeFromLeft(70));
    }
    time_.setBounds(row.removeFromLeft(80));
    row.removeFromLeft(6);
    accent_.setBounds(row.removeFromLeft(80));
    slide_.setBounds(row.removeFromLeft(70));
    if (row.getWidth() < 200) { r.removeFromTop(4); row = r.removeFromTop(28); }
    velLabel_.setBounds(row.removeFromLeft(30));
    vel_.setBounds(row.removeFromLeft(std::min(170, row.getWidth() / 2)));
    if (chance_.isVisible()) {
        row.removeFromLeft(8);
        chanceLabel_.setBounds(row.removeFromLeft(56));
        chance_.setBounds(row.removeFromLeft(std::min(170, row.getWidth())));
    }
    r.removeFromTop(4);
    auto notes = r.removeFromTop(28);
    for (int k = 0; k < 4; ++k) {
        if (!noteName_[k].isVisible()) continue;
        noteName_[k].setBounds(notes.removeFromLeft(40));
        noteLevel_[k].setBounds(notes.removeFromLeft(80));
        noteRatchet_[k].setBounds(notes.removeFromLeft(54));
        notes.removeFromLeft(10);
    }
}

#endif
