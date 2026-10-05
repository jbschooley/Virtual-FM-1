// The safe area of a full-screen window on iOS: JUCE's Displays read it from a temporary
// window in a standalone app, which reports none, so this asks the component's own window.
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#import <UIKit/UIKit.h>

// nullopt while its view is not in a window yet (just after the app starts)
std::optional<juce::BorderSize<int>> fm1SafeArea(juce::Component& c) {
    auto* peer = c.getPeer();
    auto* view = peer != nullptr ? (__bridge UIView*) peer->getNativeHandle() : nil;
    if (view == nil || view.window == nil) return std::nullopt;
    const UIEdgeInsets i = view.window.safeAreaInsets;
    return juce::BorderSize<int>(int(std::ceil(i.top)), int(std::ceil(i.left)), int(std::ceil(i.bottom)), int(std::ceil(i.right)));
}
