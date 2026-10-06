#include "IconButton.h"

#include <cmath>

juce::Path IconButton::path(Icon icon, bool on) {
    juce::Path p;
    switch (icon) {
    case Icon::Play:
        if (on) p.addRoundedRectangle(0.2f, 0.2f, 0.6f, 0.6f, 0.06f);   // stop
        else p.addTriangle(0.25f, 0.15f, 0.25f, 0.85f, 0.85f, 0.5f);
        break;
    case Icon::Live: {   // a dot sending both ways
        p.addEllipse(0.4f, 0.4f, 0.2f, 0.2f);
        juce::PathStrokeType s(0.08f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        for (float rad : {0.25f, 0.42f}) {
            juce::Path arcs;
            arcs.addCentredArc(0.5f, 0.5f, rad, rad, 0.0f, juce::MathConstants<float>::pi * 0.2f, juce::MathConstants<float>::pi * 0.8f, true);
            arcs.addCentredArc(0.5f, 0.5f, rad, rad, 0.0f, -juce::MathConstants<float>::pi * 0.2f, -juce::MathConstants<float>::pi * 0.8f, true);
            juce::Path stroked;
            s.createStrokedPath(stroked, arcs);
            p.addPath(stroked);
        }
        break;
    }
    case Icon::Connect: {   // a plug: two prongs, its body and the cable
        p.addRoundedRectangle(0.33f, 0.08f, 0.08f, 0.22f, 0.03f);
        p.addRoundedRectangle(0.59f, 0.08f, 0.08f, 0.22f, 0.03f);
        p.addRoundedRectangle(0.22f, 0.3f, 0.56f, 0.3f, 0.08f);
        p.addTriangle(0.3f, 0.6f, 0.7f, 0.6f, 0.5f, 0.76f);
        p.addRectangle(0.46f, 0.7f, 0.08f, 0.24f);
        break;
    }
    case Icon::Find: {   // a magnifier
        juce::Path ring;
        ring.addEllipse(0.12f, 0.12f, 0.5f, 0.5f);
        juce::PathStrokeType(0.09f).createStrokedPath(p, ring);
        juce::Path handle;
        handle.startNewSubPath(0.56f, 0.56f);
        handle.lineTo(0.86f, 0.86f);
        juce::Path h;
        juce::PathStrokeType(0.13f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded).createStrokedPath(h, handle);
        p.addPath(h);
        break;
    }
    case Icon::Settings: {   // a cog
        const int teeth = 8;
        for (int i = 0; i < teeth; ++i) {
            juce::Path t;
            t.addRoundedRectangle(-0.07f, -0.46f, 0.14f, 0.2f, 0.03f);
            t.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::twoPi * float(i) / float(teeth)).translated(0.5f, 0.5f));
            p.addPath(t);
        }
        juce::Path circle;   // the hub: a ring, its hole open
        circle.addEllipse(0.3f, 0.3f, 0.4f, 0.4f);
        juce::Path hub;
        juce::PathStrokeType(0.14f).createStrokedPath(hub, circle);
        p.addPath(hub);
        break;
    }
    }
    return p;
}

void IconButton::paintButton(juce::Graphics& g, bool over, bool down) {
    auto& lf = getLookAndFeel();
    const auto bg = findColour(getToggleState() ? juce::TextButton::buttonOnColourId : juce::TextButton::buttonColourId);
    lf.drawButtonBackground(g, *this, bg, over, down);
    auto colour = findColour(getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
    if (!isEnabled()) colour = colour.withMultipliedAlpha(0.4f);
    g.setColour(colour);
    const float side = std::min(float(getHeight()) * 0.56f, float(getWidth()) - 8.0f);
    const auto box = getLocalBounds().toFloat().withSizeKeepingCentre(side, side);
    auto p = path(icon_, getToggleState());
    p.applyTransform(juce::AffineTransform::scale(side).translated(box.getX(), box.getY()));
    g.fillPath(p);
}
