// fel_sections -- for Windows builds of the Felucca copies (CMakeLists.txt): gives every global
// that Clang's section pragma put in a section its section explicitly, in LLVM IR.
//
//   fel_sections <in.ll> <out.ll>
//
// felucca_core.c puts each copy's writable state in sections of its own with
// "#pragma clang section bss = ... data = ...". In LLVM IR that is an attribute group on each
// global ("bss-section"="felb3$m" "data-section"="feld3$m"), and on Windows (COFF) Clang 18 and
// 19 lose the name when they emit the object: every such global lands in one section with no
// name. An explicit section ("section" on the global) is emitted correctly, so this gives each
// one its section: the bss one for a global that starts at zero, the data one for the rest
// (both are the copy's state; a zero global in the data section only costs file size). It also
// drops the IR's linker options (the C runtime Clang names): the plugin's own build picks it.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

// (no std::regex: it recurses per character and overflows the stack on the IR's long lines)

static std::string attr(const std::string& body, const std::string& key) {   // "key"="value" -> value
    const std::string k = "\"" + key + "\"=\"";
    const auto at = body.find(k);
    if (at == std::string::npos) return {};
    const auto from = at + k.size(), to = body.find('"', from);
    return to == std::string::npos ? std::string() : body.substr(from, to - from);
}

// "global <type> <initializer>": the initializer is all zeros
static bool startsAtZero(const std::string& l, size_t globalAt) {
    size_t i = globalAt + 7;   // after "global "
    int depth = 0;
    for (; i < l.size(); ++i) {   // the type: up to a space outside brackets
        const char c = l[i];
        if (c == '[' || c == '{' || c == '<') ++depth;
        else if (c == ']' || c == '}' || c == '>') --depth;
        else if (c == ' ' && depth == 0) break;
    }
    if (i >= l.size()) return false;
    const std::string init = l.substr(i + 1);
    for (const char* z : {"zeroinitializer", "0", "null", "false", "0.000000e+00"}) {
        const std::string zs = z;
        if (init.compare(0, zs.size(), zs) == 0 && (init.size() == zs.size() || init[zs.size()] == ',' || init[zs.size()] == ' '))
            return true;
    }
    return false;
}

int main(int argc, char** argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: fel_sections <in.ll> <out.ll>\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) { std::fprintf(stderr, "fel_sections: cannot read %s\n", argv[1]); return 1; }
    std::vector<std::string> lines;
    for (std::string l; std::getline(in, l);) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
    }
    // the attribute groups that carry the pragma's sections: "attributes #3 = { ... }"
    struct Sections { std::string bss, data; };
    std::map<std::string, Sections> groups;
    for (const auto& l : lines) {
        if (l.rfind("attributes #", 0) != 0) continue;
        const auto eq = l.find(" = {");
        if (eq == std::string::npos) continue;
        Sections s{attr(l, "bss-section"), attr(l, "data-section")};
        if (!s.bss.empty() || !s.data.empty()) groups[l.substr(11, eq - 11)] = s;
    }
    int moved = 0;
    std::ostringstream out;
    for (const auto& l : lines) {
        if (l.rfind("!llvm.linker.options", 0) == 0) continue;
        // a global variable (not a constant) whose last word is one of those groups: "@x = ... global ... #3"
        const auto hash = l.rfind(" #");
        const auto global = l.find(" global ");
        if (!l.empty() && l[0] == '@' && hash != std::string::npos && global != std::string::npos && global < hash
            && l.find(" = ") != std::string::npos && l.find(", section \"") == std::string::npos
            && l.find_first_not_of("0123456789", hash + 2) == std::string::npos) {
            auto g = groups.find(l.substr(hash + 1));
            if (g != groups.end()) {
                std::string head = l.substr(0, hash);
                const std::string name = startsAtZero(l, global + 1) && !g->second.bss.empty() ? g->second.bss : g->second.data;
                if (!name.empty()) {
                    const auto at = head.find(", align ");
                    const std::string sec = ", section \"" + name + "\"";
                    if (at == std::string::npos) head += sec; else head.insert(at, sec);
                    out << head << l.substr(hash) << "\n";
                    ++moved;
                    continue;
                }
            }
        }
        out << l << "\n";
    }
    if (moved == 0) { std::fprintf(stderr, "fel_sections: no global had a pragma section in %s\n", argv[1]); return 1; }
    std::ofstream o(argv[2], std::ios::binary);
    o << out.str();
    if (!o) { std::fprintf(stderr, "fel_sections: cannot write %s\n", argv[2]); return 1; }
    std::printf("fel_sections: %d globals given their sections\n", moved);
    return 0;
}
