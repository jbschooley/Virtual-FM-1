#include "Fm1OpenSync.h"

#include <algorithm>

#include "Fm1Link.h"

namespace fm1open {

namespace {
void put16(Bytes& b, unsigned v) { b.push_back(uint8_t(v & 255)); b.push_back(uint8_t((v >> 8) & 255)); }
void put32(Bytes& b, uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(uint8_t((v >> (8 * i)) & 255)); }
unsigned le(const Bytes& b, size_t at, int n) {
    unsigned v = 0;
    for (int i = 0; i < n && at + size_t(i) < b.size(); ++i) v |= unsigned(b[at + size_t(i)]) << (8 * i);
    return v;
}
// a status other than 0 (done) in words (transfer.c: 64 - r for an error r; fm1.h's flash errors -1..-6,
// instrument.h's -7, -8)
constexpr int kBusy = 64 + 3;   // FM1_FLASH_BUSY
juce::String statusText(int code) {
    switch (code) {
        case 1: return "that project slot is empty";
        case 65: return "its flash refused the data as invalid";
        case 66: return "its flash could not be read or written";
        case kBusy: return "its flash is busy (try again in a moment)";
        case 68: return "its flash needed erasing first";
        case 69: return "its flash was in the wrong state for it";
        case 70: return "its flash is not available";
        case 71: return "it found the file or a value invalid";
        case 72: return "it has not enough memory for it";
        default: return "it refused it (code " + juce::String(code) + ")";
    }
}
}  // namespace

Bytes frame(int command, int token, const Bytes& payload) {
    Bytes f = {0xF0, 0x7D, 'F', 'M', 2, uint8_t(command & 127), uint8_t(token & 127), uint8_t((token >> 7) & 127)};
    unsigned value = 0, bits = 0;
    for (uint8_t b : payload) {   // (LSB first, 7 bits a byte)
        value |= unsigned(b) << bits;
        bits += 8;
        while (bits >= 7) { f.push_back(uint8_t(value & 127)); value >>= 7; bits -= 7; }
    }
    if (bits) f.push_back(uint8_t(value & 127));
    unsigned sum = 0;
    for (size_t i = 1; i < f.size(); ++i) sum += f[i];
    f.push_back(uint8_t((0u - sum) & 127));
    f.push_back(0xF7);
    return f;
}

std::optional<Reply> parse(const Bytes& b) {
    if (b.size() < 10 || b.front() != 0xF0 || b.back() != 0xF7 || b[1] != 0x7D || b[2] != 'F' || b[3] != 'M' || b[4] != 2) return std::nullopt;
    unsigned sum = 0;
    for (size_t i = 1; i + 1 < b.size(); ++i) {
        if (b[i] > 127) return std::nullopt;
        sum += b[i];
    }
    if (sum % 128) return std::nullopt;
    Reply r;
    r.command = b[5];
    r.token = b[6] | (b[7] << 7);
    unsigned value = 0, bits = 0;
    for (size_t i = 8; i + 2 < b.size(); ++i) {
        value |= unsigned(b[i]) << bits;
        bits += 7;
        if (bits >= 8) { r.data.push_back(uint8_t(value & 255)); value >>= 8; bits -= 8; }
    }
    if (value || bits >= 7) return std::nullopt;
    return r;
}

uint32_t crc32(const uint8_t* p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= p[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

Bytes projectFile(const Bytes& project) {
    const uint8_t version = project.size() == size_t(kLegacyProjectBytes) ? 3 : 4;
    Bytes f = {'F', 'M', '1', 'P', version, 0, 0, 0};
    put32(f, uint32_t(project.size()));
    put32(f, crc32(project.data(), project.size()));
    f.insert(f.end(), project.begin(), project.end());
    return f;
}

Bytes upgradeProject(const Bytes& p) {
    if (p.size() == size_t(kProjectBytes) && p[3852] == 4) return p;
    if (p.size() != size_t(kLegacyProjectBytes) || p[3852] != 3) return {};
    Bytes out(size_t(kProjectBytes), 0);
    std::copy(p.begin(), p.end(), out.begin());
    out[3852] = 4;
    for (int t = 0; t < kTracks; ++t) out[size_t(kLegacyProjectBytes + 2 * t)] = 66;   // (WebUpdate.js upgradeProject)
    return out;
}

juce::String sampleBankProblem(const Bytes& d) {
    constexpr unsigned kHeader = 512, kRate = 32000, kChunkFrames = 320, kChunkBytes = 228, kDiskSlots = 16, kPlayed = 8;
    constexpr unsigned kMaxFrames = (unsigned(kSampleBytes) - kHeader) / kChunkBytes * kChunkFrames;
    const size_t n = d.size();
    if (n < kHeader || n > size_t(kSampleBytes)) return "its size is not a sample bank's";
    if (le(d, 0, 4) != 0x53314d46u || le(d, 4, 4) != 1 || le(d, 8, 4) != n || le(d, 16, 4) != 0 || le(d, 20, 4) != kRate || le(d, 24, 4) || le(d, 28, 4))
        return "it is not an FM1 Open sample bank";
    Bytes z = d;   // (its CRC: with the CRC and commit words as zeros)
    std::fill(z.begin() + 12, z.begin() + 20, uint8_t(0));
    if (le(d, 12, 4) != crc32(z.data(), z.size())) return "its CRC does not match (damaged)";
    for (size_t i = 480; i < kHeader; ++i) if (d[i]) return "its reserved bytes are not empty";
    size_t next = kHeader;
    for (unsigned i = 0; i < kDiskSlots; ++i) {
        const size_t at = 32 + i * 28;
        const unsigned offset = le(d, at, 4), frames = le(d, at + 4, 4), bytes = le(d, at + 8, 4);
        if (offset != next || frames > kMaxFrames || bytes != (frames + kChunkFrames - 1) / kChunkFrames * kChunkBytes || bytes > n - next || d[at + 27] != 0)
            return "its sample " + juce::String(i + 1) + " is not valid";
        for (size_t c = at + 12; c < at + 28; ++c) if (d[c] && (d[c] < 32 || d[c] > 126)) return "sample " + juce::String(i + 1) + "'s name is not valid";
        if (i >= kPlayed && (frames || d[at + 12])) return "it uses a slot past the eighth";
        for (size_t p = offset; p < size_t(offset) + bytes; p += kChunkBytes) if (le(d, p, 4) != 0x5a144501u) return "its sample data is damaged";
        next += bytes;
    }
    if (next != n) return "it holds more than its samples";
    return {};
}

std::optional<Bytes> LinkEndpoint::ask(const Bytes& request) {
    const auto q = parse(request);
    if (!q) return std::nullopt;
    const int command = q->command | 64, token = q->token;
    return link_.ask<Bytes>(request, [command, token](const Bytes& f) -> std::optional<Bytes> {
        const auto r = parse(f);
        if (!r || r->command != command || r->token != token) return std::nullopt;
        return f;
    }, 1000, 3);
}

Client::Client(Endpoint& e) : e_(e), token_(juce::Random::getSystemRandom().nextInt(16384)) {}

// A lost reply: the same frame again (same token), which FM1 Open answers from its cached reply, so a write is
// never done twice. A busy flash: that reply is cached too, so it is asked again as a new request (a new
// token) after a moment, a few times (FM1_FLASH_BUSY: the request was not carried out)
std::optional<Bytes> Client::query(int command, const Bytes& payload, juce::String& error, bool emptyOK, bool* empty) {
    for (int attempt = 0;; ++attempt) {
        token_ = (token_ + 1) & 16383;
        const auto packet = frame(command, token_, payload);
        auto reply = e_.ask(packet);
        if (!reply) reply = e_.ask(packet);
        const auto r = reply ? parse(*reply) : std::nullopt;
        if (!r || r->command != (command | 64) || r->token != token_) { error = "the FM-1 did not answer"; return std::nullopt; }
        if (r->data.empty()) { error = "the FM-1's answer was incomplete"; return std::nullopt; }
        const int code = r->data[0];
        if (code == kBusy && attempt + 1 < kBusyTries) { juce::Thread::sleep(kBusyWaitMs); continue; }
        if (empty) *empty = code == 1;
        if (code != 0 && !(emptyOK && code == 1)) { error = "the FM-1 says " + statusText(code); return std::nullopt; }
        return Bytes(r->data.begin() + 1, r->data.end());
    }
}

std::optional<Info> Client::info(juce::String& error) {
    auto a = query(0, {}, error);
    if (!a) return std::nullopt;
    if (a->size() != 14 || (*a)[1] != kSlots) { error = "the FM-1 gave no FM1 Open project library"; return std::nullopt; }
    Info i;
    i.version = (*a)[0];
    i.slots = (*a)[1];
    i.slot = (*a)[2];
    i.projectBytes = int(le(*a, 3, 2));
    i.clock = (*a)[5];
    for (int t = 0; t < kTracks; ++t) i.sources[t] = (*a)[size_t(6 + t)];
    if (!((i.version == 3 && i.projectBytes == kLegacyProjectBytes) || (i.version == 4 && i.projectBytes == kProjectBytes))) {
        error = "the FM-1's FM1 Open keeps projects the plugin does not know (version " + juce::String(i.version) + ")";
        return std::nullopt;
    }
    return i;
}

std::optional<Bytes> Client::download(int kind, int target, juce::String& error, const Progress& progress) {
    auto head = query(1, {uint8_t(kind), uint8_t(target)}, error);
    if (!head) return std::nullopt;
    const int size = int(le(*head, 0, 2));
    const bool sized = head->size() == 2 && (kind == 2 ? size == kTimbreBytes : kind == 1 ? size == kPatchBytes
                                                       : size == kProjectBytes || size == kLegacyProjectBytes);
    std::optional<Bytes> out;
    if (!sized) error = "the FM-1 offered a file of the wrong size (" + juce::String(size) + " bytes)";
    else {
        Bytes bytes;
        bool ok = true;
        for (int at = 0; at < size && ok; at += 64) {
            const int n = std::min(64, size - at);
            auto part = query(2, {uint8_t(at & 255), uint8_t(at >> 8), uint8_t(n)}, error);
            if (!part || int(part->size()) != n) { if (part) error = "a piece of the file came back short"; ok = false; break; }
            bytes.insert(bytes.end(), part->begin(), part->end());
            if (progress && !progress(at + n, size)) { error = "stopped"; ok = false; }
        }
        if (ok) out = std::move(bytes);
    }
    juce::String ignored;
    query(6, {}, ignored);   // (the transfer ended either way, as the workbench's finally)
    return out;
}

bool Client::upload(int kind, int target, const Bytes& bytes, juce::String& error, const Progress& progress) {
    const int size = int(bytes.size());
    if (!query(3, {uint8_t(kind), uint8_t(target), uint8_t(size & 255), uint8_t(size >> 8)}, error)) return false;
    bool ok = true;
    for (int at = 0; at < size && ok; at += 64) {
        Bytes q;
        put16(q, unsigned(at));
        q.insert(q.end(), bytes.begin() + at, bytes.begin() + std::min(size, at + 64));
        if (!query(4, q, error)) ok = false;
        else if (progress && !progress(std::min(size, at + 64), size)) { error = "stopped"; ok = false; }
    }
    if (ok) {
        Bytes q;
        put32(q, crc32(bytes.data(), bytes.size()));
        ok = query(5, q, error).has_value();
    }
    juce::String ignored;
    query(6, {}, ignored);
    return ok;
}

bool Client::setProject(const Bytes& project, juce::String& error, const Progress& progress) {
    auto i = info(error);
    if (!i) return false;
    if (int(project.size()) != i->projectBytes) {
        error = i->projectBytes == kLegacyProjectBytes ? "the FM-1 runs an FM1 Open from before V12: update it to V12 with M-VAVE's workbench first"
                                                       : "the project is not an FM1 Open V12 project";
        return false;
    }
    if (!upload(0, 0, project, error, progress)) return false;
    auto back = download(0, 0, error);   // (the workbench's check: what it plays now)
    if (!back) return false;
    if (*back != project) { error = "the FM-1 plays something else than what was sent"; return false; }
    return true;
}

bool Client::saveToSlot(int slot, juce::String& error) {
    if (slot < 0 || slot >= kSlots) { error = "no such project slot"; return false; }
    auto before = download(0, 0, error);
    if (!before || !query(7, {uint8_t(slot)}, error)) return false;
    // read back from the slot (the workbench's check: what was written there, not what plays)
    if (!query(8, {uint8_t(slot)}, error)) return false;
    auto saved = download(0, 0, error);
    if (!saved) return false;
    if (*saved != *before) { error = "slot " + juce::String(slot + 1) + " read back differs from what was saved there"; return false; }
    return true;
}

bool Client::loadSlot(int slot, juce::String& error) {
    if (slot < 0 || slot >= kSlots) { error = "no such project slot"; return false; }
    return query(8, {uint8_t(slot)}, error).has_value();
}

std::optional<std::string> Client::slotName(int slot, juce::String& error) {
    bool empty = false;
    auto a = query(9, {uint8_t(slot)}, error, true, &empty);
    if (!a) return std::nullopt;
    if (empty) return std::string();
    std::string name(a->begin(), a->end());
    while (!name.empty() && (name.back() == ' ' || name.back() == 0)) name.pop_back();
    return name;
}

bool Client::rename(const std::string& name, juce::String& error) {
    Bytes q;
    for (size_t i = 0; i < 12; ++i) {
        const char c = i < name.size() ? name[i] : ' ';
        q.push_back(uint8_t(c >= 32 && c <= 126 ? c : ' '));
    }
    return query(10, q, error).has_value();
}

std::optional<Bytes> Client::samples(juce::String& error, const Progress& progress) {
    auto i = query(11, {}, error);
    if (!i) return std::nullopt;
    if (i->size() != 9 || (*i)[0] != 2) { error = "the FM-1's sample bank is not ready (it can be restored from M-VAVE's workbench)"; return std::nullopt; }
    const int size = int(le(*i, 1, 4));
    if (size < 512 || size > kSampleBytes) { error = "the FM-1's sample bank has a size the plugin does not know"; return std::nullopt; }
    Bytes data;
    for (int at = 0; at < size; at += 64) {
        const int n = std::min(64, size - at);
        Bytes q;
        put32(q, uint32_t(at));
        q.push_back(uint8_t(n));
        auto part = query(15, q, error);
        if (!part || int(part->size()) != n) { if (part) error = "a piece of the sample bank came back short"; return std::nullopt; }
        data.insert(data.end(), part->begin(), part->end());
        if (progress && !progress(at + n, size)) { error = "stopped"; return std::nullopt; }
    }
    return data;
}

bool Client::setSamples(const Bytes& bank, juce::String& error, const Progress& progress, bool* erased) {
    const int n = int(bank.size());
    if (erased) *erased = false;
    if (const auto why = sampleBankProblem(bank); why.isNotEmpty()) { error = "the sample bank was not sent: " + why; return false; }
    auto i = query(11, {}, error);
    if (!i) return false;
    if (i->size() != 9 || (*i)[0] == 1 || (*i)[0] == 3) { error = "the FM-1's sample bank is busy: try again in a moment"; return false; }
    Bytes q;
    put32(q, uint32_t(n));
    if (!query(12, q, error)) return false;
    if (erased) *erased = true;   // (begin erased the FM-1's bank: from here it has none until finish)
    bool ok = true;
    for (int at = 0; at < n && ok; at += 128) {
        Bytes w;
        put32(w, uint32_t(at));
        w.insert(w.end(), bank.begin() + at, bank.begin() + std::min(n, at + 128));
        if (!query(13, w, error)) ok = false;
        else if (progress) progress(std::min(n, at + 128), 2 * n);   // (shown; a stop is not taken here)
    }
    if (ok) ok = query(14, {}, error).has_value();
    if (ok && erased) *erased = false;   // (finished: the FM-1 has this bank)
    juce::String ignored;
    query(16, {}, ignored);   // (as the workbench's finally)
    if (!ok) return false;
    auto back = samples(error, progress ? Progress([&progress, n](int done, int) { return progress(n + done, 2 * n); }) : Progress());
    if (!back) return false;
    if (*back != bank) { error = "the FM-1's sample bank read back differs from what was sent"; return false; }
    return true;
}

}  // namespace fm1open
