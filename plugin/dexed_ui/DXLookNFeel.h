/**
 * Copyright (c) 2013-2024 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Trimmed from Dexed's DXLookNFeel for Virtual FM-1 (see NOTICE.md).
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class LightedToggleButton : public juce::ToggleButton {
public:
    LightedToggleButton(const char* l) : juce::ToggleButton(l) {}
};

class DXLookNFeel : public juce::LookAndFeel_V4 {
public:
    DXLookNFeel();

    juce::Image imageKnob, imageSwitch, imageSwitchLighted, imageButton, imageSlider, imageScaling, imageLight, imageLFO;
    juce::Image imageSwitchOperator;
    juce::Image imageOperator, imageGlobal;

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override;
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button, bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool isMouseOverButton, bool isButtonDown) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                          float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;

    static juce::Colour fillColour;
    static juce::Colour lightBackground;
    static juce::Colour background;
    static juce::Colour roundBackground;
};
