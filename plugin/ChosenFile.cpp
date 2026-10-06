#include "ChosenFile.h"

namespace fm1ui {

#if JUCE_ANDROID
static juce::File cacheFile(const juce::String& name) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("chosen");
    dir.createDirectory();
    return dir.getChildFile(juce::File::createLegalFileName(name.isNotEmpty() ? name : juce::String("file")));
}
#endif

juce::File openedFile(const juce::FileChooser& chooser) {
#if JUCE_ANDROID
    const auto url = chooser.getURLResult();
    if (url.isEmpty()) return {};
    if (url.isLocalFile()) return url.getLocalFile();
    const auto doc = juce::AndroidDocument::fromDocument(url);
    auto in = doc.createInputStream();
    if (in == nullptr) return {};
    auto copy = cacheFile(doc.getInfo().getName());
    copy.deleteFile();
    juce::FileOutputStream out(copy);
    if (!out.openedOk() || out.writeFromInputStream(*in, -1) < 0) return {};
    out.flush();
    return copy;
#else
    return chooser.getResult();
#endif
}

bool saveChosen(const juce::FileChooser& chooser, const juce::String& extension,
                const std::function<bool(const juce::File&)>& write, juce::String& name) {
    name = {};
#if JUCE_ANDROID
    const auto url = chooser.getURLResult();
    if (url.isEmpty()) return false;
    if (url.isLocalFile()) {
        name = url.getLocalFile().getFileName();
        return write(url.getLocalFile());
    }
    const auto doc = juce::AndroidDocument::fromDocument(url);
    name = doc.getInfo().getName();
    auto temp = cacheFile(name.isNotEmpty() ? name : "saved" + extension);
    temp.deleteFile();
    if (!write(temp)) return false;
    juce::FileInputStream in(temp);
    auto out = doc.createOutputStream();
    const bool ok = in.openedOk() && out != nullptr && out->writeFromInputStream(in, -1) == temp.getSize();
    if (out) out->flush();
    temp.deleteFile();
    return ok;
#else
    auto f = chooser.getResult();
    if (f == juce::File()) return false;
    if (extension.isNotEmpty()) f = f.withFileExtension(extension);
    name = f.getFileName();
    return write(f);
#endif
}

}  // namespace fm1ui
