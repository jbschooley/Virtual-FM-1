/**
 * Copyright (c) 2014, 2017 Pascal Gauthier.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * From Dexed's AlgoDisplay (see NOTICE.md).
 */

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AlgoDisplay : public juce::Component {
    void displayOp(juce::Graphics& g, char id, int x, int y, char link, char fb);
public:
    const char* opStatus = "111111";   // index 0 = OP6, as msfa's opSwitch
    AlgoDisplay();
    char* algo;                        // 0..31
    void paint(juce::Graphics& g) override;
};
