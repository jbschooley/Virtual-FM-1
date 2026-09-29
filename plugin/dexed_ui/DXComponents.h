/**
 * Copyright (c) 2014-2018 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Trimmed from Dexed's DXComponents for FM-1 Companion (see NOTICE.md).
 */

#pragma once

#include <cstdint>

#include <juce_gui_basics/juce_gui_basics.h>

// Draws an operator envelope from 8 bytes: rates[4] then levels[4].
class EnvDisplay : public juce::Component {
public:
    EnvDisplay();
    uint8_t* pvalues;
    char vPos;
    void paint(juce::Graphics& g) override;
};

// Draws the pitch envelope from 8 bytes: rates[4] then levels[4].
class PitchEnvDisplay : public juce::Component {
public:
    PitchEnvDisplay();
    uint8_t* pvalues;
    char vPos;
    void paint(juce::Graphics& g) override;
};

// A combo box that shows one strip of an image per item.
class ComboBoxImage : public juce::ComboBox {
    juce::Image items;
    int itemHeight = 26;
    juce::PopupMenu popup;
    int itemPos[4];
public:
    ComboBoxImage();
    void paint(juce::Graphics& g) override;
    void showPopup() override;
    void setImage(juce::Image image);
    void setImage(juce::Image image, int pos[]);
};

// A rotary knob that moves 10% per shift+arrow and slows the wheel with shift.
class DXSlider : public juce::Slider {
public:
    DXSlider(const juce::String& componentName) : juce::Slider(componentName) { setWantsKeyboardFocus(true); }
    bool keyPressed(const juce::KeyPress& key) override;
    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override;
};
