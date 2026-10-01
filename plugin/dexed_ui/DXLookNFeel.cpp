/**
 * Copyright (c) 2013-2024 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * Trimmed from Dexed's DXLookNFeel for Virtual FM-1 (see NOTICE.md).
 */

#include "DXLookNFeel.h"

#include "BinaryData.h"
#include "DXComponents.h"

using namespace juce;

DXLookNFeel::DXLookNFeel() {
    Colour ctrlBackground = Colour(20, 18, 18);

    setColour(TextButton::buttonColourId, Colour(0xFF0FC00F));
    setColour(TextButton::textColourOnId, Colours::white);
    setColour(TextButton::textColourOffId, Colours::white);
    setColour(Slider::rotarySliderOutlineColourId, Colour(0xFF0FC00F));
    setColour(Slider::rotarySliderFillColourId, Colour(0xFFFFFFFF));
    setColour(AlertWindow::backgroundColourId, lightBackground);
    setColour(AlertWindow::textColourId, Colours::white);
    setColour(TextEditor::backgroundColourId, ctrlBackground);
    setColour(TextEditor::textColourId, Colours::white);
    setColour(TextEditor::highlightColourId, fillColour);
    setColour(TextEditor::outlineColourId, Colours::transparentBlack);
    setColour(ComboBox::backgroundColourId, ctrlBackground);
    setColour(ComboBox::textColourId, Colours::white);
    setColour(ComboBox::buttonColourId, Colours::white);
    setColour(PopupMenu::backgroundColourId, background);
    setColour(PopupMenu::textColourId, Colours::white);
    setColour(PopupMenu::highlightedTextColourId, Colours::white);
    setColour(PopupMenu::highlightedBackgroundColourId, fillColour);
    setColour(ListBox::backgroundColourId, ctrlBackground);
    setColour(ScrollBar::thumbColourId, background.darker());
    setColour(Label::textColourId, Colours::white);

    imageKnob = ImageCache::getFromMemory(BinaryData::Knob_68x68_png, BinaryData::Knob_68x68_pngSize);
    imageSwitch = ImageCache::getFromMemory(BinaryData::Switch_96x52_png, BinaryData::Switch_96x52_pngSize);
    imageSwitchLighted = ImageCache::getFromMemory(BinaryData::SwitchLighted_48x26_png, BinaryData::SwitchLighted_48x26_pngSize);
    imageSwitchOperator = ImageCache::getFromMemory(BinaryData::Switch_64x64_png, BinaryData::Switch_64x64_pngSize);
    imageButton = ImageCache::getFromMemory(BinaryData::ButtonUnlabeled_50x30_png, BinaryData::ButtonUnlabeled_50x30_pngSize);
    imageSlider = ImageCache::getFromMemory(BinaryData::Slider_52x52_png, BinaryData::Slider_52x52_pngSize);
    imageScaling = ImageCache::getFromMemory(BinaryData::Scaling_36_26_png, BinaryData::Scaling_36_26_pngSize);
    imageLight = ImageCache::getFromMemory(BinaryData::Light_28x28_png, BinaryData::Light_28x28_pngSize);
    imageLFO = ImageCache::getFromMemory(BinaryData::LFO_36_26_png, BinaryData::LFO_36_26_pngSize);
    imageOperator = ImageCache::getFromMemory(BinaryData::OperatorEditor_574x436_png, BinaryData::OperatorEditor_574x436_pngSize);
    imageGlobal = ImageCache::getFromMemory(BinaryData::GlobalEditor_1728x288_png, BinaryData::GlobalEditor_1728x288_pngSize);
}

Typeface::Ptr DXLookNFeel::getTypefaceForFont(const Font&) {
    return Typeface::createSystemTypefaceFor(BinaryData::NotoSansRegular_ttf, BinaryData::NotoSansRegular_ttfSize);
}

void DXLookNFeel::drawRotarySlider(Graphics& g, int x, int y, int width, int height, float sliderPosProportional,
                                   float rotaryStartAngle, float rotaryEndAngle, Slider& slider) {
    if (imageKnob.isNull()) {
        LookAndFeel_V4::drawRotarySlider(g, x, y, width, height, sliderPosProportional, rotaryStartAngle, rotaryEndAngle, slider);
        return;
    }
    const double fractRotation = (slider.getValue() - slider.getMinimum()) / (slider.getMaximum() - slider.getMinimum());
    const int nFrames = imageKnob.getHeight() / imageKnob.getWidth();
    const int frameIdx = (int)ceil(fractRotation * ((double)nFrames - 1.0));
    const float radius = jmin(width * 0.5f, height * 0.5f);
    const float centreX = x + width * 0.5f;
    const float centreY = y + height * 0.5f;
    const float rx = centreX - radius - 1.0f;
    const float ry = centreY - radius - 1.0f;
    g.drawImage(imageKnob, (int)rx, (int)ry, 2 * (int)radius, 2 * (int)radius, 0, frameIdx * imageKnob.getWidth(),
                imageKnob.getWidth(), imageKnob.getWidth());
}

void DXLookNFeel::drawToggleButton(Graphics& g, ToggleButton& button, bool isMouseOverButton, bool isButtonDown) {
    if (imageSwitch.isNull()) {
        LookAndFeel_V4::drawToggleButton(g, button, isMouseOverButton, isButtonDown);
        return;
    }
    if (dynamic_cast<LightedToggleButton*>(&button) != nullptr) {
        if (imageSwitchLighted.isNull()) {
            LookAndFeel_V4::drawToggleButton(g, button, isMouseOverButton, isButtonDown);
            return;
        }
        g.drawImage(imageSwitchLighted, 0, 0, 48, 26, 0, button.getToggleState() ? 0 : 26, 48, 26);
    } else {
        g.drawImage(imageSwitch, 0, 0, 48, 26, 0, button.getToggleState() ? 0 : 52, 96, 52);
    }
}

void DXLookNFeel::drawButtonBackground(Graphics& g, Button& button, const Colour& backgroundColour, bool isMouseOverButton, bool isButtonDown) {
    if (imageButton.isNull()) {
        LookAndFeel_V4::drawButtonBackground(g, button, backgroundColour, isMouseOverButton, isButtonDown);
        return;
    }
    int w = button.getWidth();
    int l = button.getHeight();
    g.drawImage(imageButton, 0, 0, 3, l, 0, isButtonDown ? 30 : 0, 3, 30);
    g.drawImage(imageButton, 3, 0, w - 6, l, 3, isButtonDown ? 30 : 0, 44, 30);
    g.drawImage(imageButton, w - 3, 0, 3, l, 47, isButtonDown ? 30 : 0, 47, 30);
}

void DXLookNFeel::drawLinearSlider(Graphics& g, int x, int y, int width, int height, float sliderPos, float minSliderPos,
                                   float maxSliderPos, Slider::SliderStyle style, Slider& slider) {
    if (imageSlider.isNull()) {
        LookAndFeel_V4::drawLinearSliderThumb(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }
    int p = int(sliderPos - minSliderPos);
    p -= 2;
    g.drawImage(imageSlider, p, 0, 26, 26, 0, 0, 52, 52);
}

void DXLookNFeel::positionComboBoxText(ComboBox& box, Label& label) {
    if (dynamic_cast<ComboBoxImage*>(&box) != nullptr) return;
    LookAndFeel_V4::positionComboBoxText(box, label);
}

Colour DXLookNFeel::fillColour = Colour(77, 159, 151);
Colour DXLookNFeel::lightBackground = Colour(78, 72, 63);
Colour DXLookNFeel::background = Colour(60, 50, 47);
Colour DXLookNFeel::roundBackground = Colour(58, 52, 48);
