// IconButton -- a TextButton drawn with an icon instead of its text (the text stays its name, for
// accessibility, and the tooltip says what it does). Play shows stop while toggled on (playing).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class IconButton : public juce::TextButton {
public:
    enum class Icon { Play, Live, Connect, Find, Settings };
    IconButton(const juce::String& name, Icon icon) : juce::TextButton(name), icon_(icon) {}
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    static juce::Path path(Icon icon, bool on);   // in a unit square

private:
    Icon icon_;
};
