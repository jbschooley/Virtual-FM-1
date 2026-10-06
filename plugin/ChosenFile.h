// ChosenFile -- what a FileChooser picked, as a local file the code reads or writes. On Android the
// chooser gives a document (a content:// URL), not a path: it is copied to the app's cache to be
// read, or written there and then copied into the document.
#pragma once

#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

namespace fm1ui {

// opened: the file to read (Android: a copy, named as the user's file), or File() if none was picked
juce::File openedFile(const juce::FileChooser& chooser);

// saved: write(file) into the picked file (given `extension` on desktops, where the name is typed);
// false if writing failed. `name` gets the name to show. Nothing picked: false, name empty.
bool saveChosen(const juce::FileChooser& chooser, const juce::String& extension,
                const std::function<bool(const juce::File&)>& write, juce::String& name);

}  // namespace fm1ui
