#include "Fm1Codec.h"

#include <algorithm>
#include <cstdio>

namespace fm1 {

namespace {
inline int s8(uint8_t b) { return b > 127 ? int(b) - 256 : int(b); }
}

// ---- checksums and packing ---------------------------------------------------

uint8_t ysum(const uint8_t* b, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; ++i) s = (s + (b[i] ^ 0xFF)) & 0xFF;
    return uint8_t(s & 0x7F);
}

uint8_t dxsum(const uint8_t* b, size_t n) {
    unsigned s = 0;
    for (size_t i = 0; i < n; ++i) s += b[i];
    return uint8_t((-int(s)) & 0x7F);
}

Bytes pack87(const uint8_t* b, size_t n) {
    Bytes out;
    for (size_t g = 0; g < n; g += 7) {
        size_t len = std::min<size_t>(7, n - g);
        uint8_t hi = 0;
        for (size_t k = 0; k < len; ++k) hi |= uint8_t(((b[g + k] >> 7) & 1) << k);
        out.push_back(hi);
        for (size_t k = 0; k < len; ++k) out.push_back(b[g + k] & 0x7F);
    }
    return out;
}

Bytes unpack87(const uint8_t* p, size_t packedLen, size_t n) {
    Bytes out(n);
    for (size_t i = 0; i < n; ++i) {
        size_t g = (i / 7) * 8, k = i % 7;
        if (g + 1 + k >= packedLen) throw CodecError("packed data too short");
        out[i] = uint8_t(p[g + 1 + k] | (((p[g] >> k) & 1) << 7));
    }
    return out;
}

Bytes pack7(const uint8_t* b, size_t n) {
    Bytes out;
    uint32_t acc = 0; int nb = 0;
    for (size_t i = 0; i < n; ++i) {
        acc |= uint32_t(b[i]) << nb; nb += 8;
        while (nb >= 7) { out.push_back(acc & 0x7F); acc >>= 7; nb -= 7; }
    }
    if (nb) out.push_back(acc & 0x7F);
    return out;
}

Bytes unpack7(const uint8_t* b, size_t n) {
    Bytes out;
    uint32_t acc = 0; int nb = 0;
    for (size_t i = 0; i < n; ++i) {
        acc |= uint32_t(b[i]) << nb; nb += 7;
        while (nb >= 8) { out.push_back(acc & 0xFF); acc >>= 8; nb -= 8; }
    }
    return out;
}

// ---- the voice ---------------------------------------------------------------

Edit unpackVoice(const Voice& src) {
    Edit e{};
    for (int n = 0; n < 6; ++n) {
        int s = n * 17, d = n * 21;
        for (int k = 0; k < 11; ++k) e[d + k] = src[s + k];
        e[d + 11] = src[s + 11] & 3;                       // left curve
        e[d + 12] = (src[s + 11] >> 2) & 3;                // right curve
        int c = s8(src[s + 12]);
        e[d + 13] = uint8_t(c & 7);                        // rate scaling
        e[d + 20] = uint8_t((c >> 3) & 0xFF);              // detune
        c = s8(src[s + 13]);
        e[d + 14] = uint8_t(c & 3);                        // amp mod sensitivity
        e[d + 15] = uint8_t((c >> 2) & 0xFF);              // key velocity sensitivity
        e[d + 16] = src[s + 14];                           // output level
        c = s8(src[s + 15]);
        e[d + 17] = uint8_t(c & 1);                        // oscillator mode
        e[d + 18] = uint8_t((c >> 1) & 0xFF);              // frequency coarse
        e[d + 19] = src[s + 16];                           // frequency fine
    }
    for (int k = 0; k < 8; ++k) e[126 + k] = src[102 + k]; // pitch EG
    e[134] = src[110];                                     // algorithm
    int c = s8(src[111]);
    e[135] = uint8_t(c & 7);                               // feedback
    e[136] = uint8_t((c >> 3) & 0xFF);                     // oscillator key sync
    for (int k = 0; k < 4; ++k) e[137 + k] = src[112 + k]; // LFO
    uint8_t b = src[116];
    e[141] = b & 1;                                        // LFO sync
    e[142] = (b >> 1) & 7;                                 // LFO wave
    e[143] = uint8_t((s8(b) >> 4) & 0xFF);                 // pitch mod sensitivity
    e[144] = src[117];                                     // transpose
    for (int k = 0; k < 10; ++k) e[145 + k] = src[118 + k]; // name
    return e;
}

Voice packVoice(const Edit& e) {
    Voice b{};
    for (int n = 0; n < 6; ++n) {
        int d = n * 21, p = n * 17;
        for (int k = 0; k < 11; ++k) b[p + k] = e[d + k];
        b[p + 11] = uint8_t((e[d + 11] & 3) | ((e[d + 12] << 2) & 0xC));
        b[p + 12] = uint8_t((e[d + 13] & 7) | ((e[d + 20] << 3) & 0x78));
        b[p + 13] = uint8_t((e[d + 14] & 3) | ((e[d + 15] << 2) & 0x1C));
        b[p + 14] = e[d + 16];
        b[p + 15] = uint8_t((e[d + 17] & 1) | ((e[d + 18] << 1) & 0x3E));
        b[p + 16] = e[d + 19];
    }
    for (int k = 0; k < 9; ++k) b[102 + k] = e[126 + k];
    b[111] = uint8_t((e[135] & 7) | ((e[136] << 3) & 8));
    for (int k = 0; k < 4; ++k) b[112 + k] = e[137 + k];
    b[116] = uint8_t((e[141] & 1) | ((e[142] << 1) & 0xE) | ((e[143] << 4) & 0x70));
    b[117] = e[144];
    for (int k = 0; k < 10; ++k) b[118 + k] = e[145 + k];
    return b;
}

std::string voiceName(const Voice& v) {
    std::string s;
    for (int i = 118; i < 128; ++i) {
        char c = char(v[i]);
        s += (c >= 0x20 && c <= 0x7E) ? c : '?';
    }
    return s;
}

Voice withName(const Voice& v, const std::string& name) {
    Voice out = v;
    std::string s;
    for (char c : name) s += (c >= 0x20 && c <= 0x7E) ? c : '?';
    s.resize(10, ' ');
    for (int i = 0; i < 10; ++i) out[118 + i] = uint8_t(s[i]);
    return out;
}

const Edit kInitEdit = [] {
    Edit e{};
    const uint8_t op[21] = {99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7};
    for (int n = 0; n < 5; ++n)
        for (int k = 0; k < 21; ++k) e[n * 21 + k] = op[k];
    const uint8_t op6[21] = {99, 99, 99, 99, 99, 99, 99, 0, 0, 0, 0, 0, 0, 0, 0, 0, 99, 0, 1, 0, 7};
    for (int k = 0; k < 21; ++k) e[105 + k] = op6[k];
    const uint8_t rest[29] = {99, 99, 99, 99, 50, 50, 50, 50, 0, 0, 1, 35, 0, 0, 0, 1, 0, 3, 24,
                              73, 78, 73, 84, 32, 86, 79, 73, 67, 69};
    for (int k = 0; k < 29; ++k) e[126 + k] = rest[k];
    return e;
}();

// ---- the record --------------------------------------------------------------

Engine engineOf(const Record& r) { return r[18] == kMarkVA ? Engine::VA : Engine::FM; }

Record defaultRecord() {
    Record r{};
    for (int k = 0; k < 6; ++k) r[27 + 3 * k] = uint8_t(k);   // chain 0..5, off, type 0
    r[18] = kMarkFM;
    for (int i = 23; i < 27; ++i) r[i] = 0x03;                // SX_PERF_UNSET
    return r;
}

// ---- requests ----------------------------------------------------------------

static Bytes frame7D(const Bytes& body) {
    Bytes m = {0xF0, 0x43, 0x00, kSubId};
    m.insert(m.end(), body.begin(), body.end());
    m.push_back(ysum(body));
    m.push_back(0xF7);
    return m;
}

Bytes encodeSoundRead(int slot) {
    return frame7D({0x10, uint8_t(slot & 0x7F)});
}

Bytes encodeMemRead(uint32_t addr, int n) {
    Bytes body = {0x11};
    for (int i = 0; i < 5; ++i) body.push_back(uint8_t((addr >> (7 * i)) & 0x7F));
    body.push_back(uint8_t(n & 0x7F));
    body.push_back(uint8_t((n >> 7) & 0x7F));
    return frame7D(body);
}

Bytes encodeExact(int slot, const Voice& v, const Record& r) {
    Edit e = unpackVoice(v);
    Bytes payload(e.begin(), e.end());
    Bytes rec = pack87(r.data(), r.size());
    payload.insert(payload.end(), rec.begin(), rec.end());
    Bytes m = {0xF0, 0x43, 0x00, kSubId, 0x04, uint8_t(slot & 0x7F)};
    m.insert(m.end(), payload.begin(), payload.end());
    m.push_back(ysum(payload));
    m.push_back(0xF7);
    if (m.size() != kExactLen) throw CodecError("internal: v4 length");
    return m;
}

std::optional<Sound> decodeExact(const Bytes& m) {
    if (m.size() != kExactLen || m[0] != 0xF0 || m[1] != 0x43 || m[3] != kSubId ||
        m[4] != 0x04 || m[kExactLen - 1] != 0xF7)
        return std::nullopt;
    const uint8_t* payload = m.data() + 6;
    size_t payloadLen = kExactLen - 8;
    if (ysum(payload, payloadLen) != m[kExactLen - 2])
        throw CodecError("checksum wrong in a sound message");
    Sound s;
    s.slot = m[5];
    Edit e{};
    std::copy(payload, payload + kEditBytes, e.begin());
    s.voice = packVoice(e);
    Bytes rec = unpack87(payload + kEditBytes, payloadLen - kEditBytes, kRecordBytes);
    std::copy(rec.begin(), rec.end(), s.record.begin());
    s.hasRecord = true;
    s.from = "FM-1";
    return s;
}

// ---- replies -----------------------------------------------------------------

const char* const kStatusText[4] = {
    "done", "refused: a value out of range", "damaged in transit",
    "the Sequencer is playing; stop it and send again"};

std::optional<Reply> decodeReply(const uint8_t* sx, size_t n) {
    if (n < 3 || sx[0] != 0xF0 || sx[1] != kSubId || sx[n - 1] != 0xF7) return std::nullopt;
    // the firmware packs an 8-bit buffer as a 7-bit LSB-first bitstream
    const uint8_t* body = sx + 1;
    size_t bodyLen = n - 2;
    for (size_t i = 0; i < bodyLen; ++i)
        if (body[i] & 0x80) throw CodecError("a data byte has bit 7 set");
    size_t n8 = bodyLen * 7 / 8;
    Bytes buf(n8);
    uint32_t acc = 0; int bits = 0; size_t k = 0;
    for (size_t i = 0; i < bodyLen; ++i) {
        acc |= uint32_t(body[i]) << bits;
        bits += 7;
        while (bits >= 8 && k < n8) { buf[k++] = acc & 0xFF; acc >>= 8; bits -= 8; }
    }
    if (buf.size() < 10 || buf[0] != kSubId) return std::nullopt;
    size_t len = buf[7] | (size_t(buf[8]) << 8);
    if (buf.size() < 10 + len) throw CodecError("reply is short");
    unsigned sum = 0;
    for (size_t i = 0; i < 9 + len; ++i) sum += buf[i];
    if (((~sum) & 0xFF) != buf[9 + len]) throw CodecError("reply checksum wrong");
    Reply r;
    switch (buf[1]) {
        case 0x50: r.kind = Reply::Kind::Sound; break;
        case 0x51: r.kind = Reply::Kind::Mem; break;
        case 0x52: r.kind = Reply::Kind::Pattern; break;
        default:   r.kind = Reply::Kind::Unknown; break;
    }
    r.status = buf[2];
    r.arg = uint32_t(buf[3]) | (uint32_t(buf[4]) << 8) | (uint32_t(buf[5]) << 16) | (uint32_t(buf[6]) << 24);
    r.data.assign(buf.begin() + 9, buf.begin() + 9 + long(len));
    return r;
}

Sound soundFromReply(const Reply& r) {
    if (r.kind != Reply::Kind::Sound) throw CodecError("not a sound reply");
    if (r.status != 0) {
        char msg[80];
        std::snprintf(msg, sizeof msg, "the synth refused (status %u: %s)", unsigned(r.status),
                      r.status < 4 ? kStatusText[r.status] : "unknown");
        throw CodecError(msg);
    }
    if (r.data.size() != size_t(kVoiceBytes + kRecordBytes)) throw CodecError("sound reply is the wrong size");
    Sound s;
    s.slot = int(r.arg);
    std::copy(r.data.begin(), r.data.begin() + kVoiceBytes, s.voice.begin());
    std::copy(r.data.begin() + kVoiceBytes, r.data.end(), s.record.begin());
    s.hasRecord = true;
    s.from = "FM-1";
    return s;
}

// ---- identity ----------------------------------------------------------------

const Bytes kIdentityQuery = {0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7};

std::string Identity::name() const {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%s_%03d", model.c_str(), version);
    return buf;
}

std::optional<Identity> parseIdentity(const uint8_t* sx, size_t n) {
    if (!sx || n < 4 || sx[0] != 0xF0 || sx[n - 1] != 0xF7) return std::nullopt;
    Bytes d = unpack7(sx + 1, n - 2);
    if (d.size() != 34 || d[0] != 0x00 || d[1] != 0x59 || d[2] != 0x11) return std::nullopt;
    size_t bodyLen = d[3] | (size_t(d[4]) << 8) | (size_t(d[5]) << 16);
    if (bodyLen != 27 || bodyLen + 7 != d.size()) return std::nullopt;
    Bytes plain(d.begin() + 6, d.begin() + 31);
    auto sep = std::find(plain.begin(), plain.end(), uint8_t(0x5F));
    if (sep == plain.end()) return std::nullopt;
    size_t sepIx = size_t(sep - plain.begin());
    for (size_t i = 0; i < sepIx; ++i) if (plain[i] > 0x7F) return std::nullopt;
    Identity id;
    id.model.assign(plain.begin(), sep);
    auto digits = [](const Bytes& b, size_t from) {
        std::string s;
        for (size_t i = from; i < b.size(); ++i) {
            if (b[i] < 0x30 || b[i] > 0x39) break;
            s += char(b[i]);
        }
        return s;
    };
    std::string v = digits(plain, sepIx + 1);
    if (v.empty()) {
        Bytes enc;
        for (size_t i = 14; i < 34; ++i) enc.push_back(uint8_t((d[i] + 0x30) & 0xFF));
        v = digits(enc, sepIx + 1);
        if (v.empty()) return std::nullopt;
    }
    id.version = std::atoi(v.c_str());
    return id;
}

// ---- DX7 SysEx and .syx files --------------------------------------------

Bytes toDx7Voice(const Voice& v) {
    Edit e = unpackVoice(v);
    Bytes m = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    m.insert(m.end(), e.begin(), e.end());
    m.push_back(dxsum(e.data(), e.size()));
    m.push_back(0xF7);
    return m;
}

Bytes toDx7Bank(const std::vector<Voice>& voices) {
    if (voices.size() > 32) throw CodecError("a DX7 bank holds 32 voices");
    Bytes data;
    Voice init = packVoice(kInitEdit);
    for (size_t i = 0; i < 32; ++i) {
        const Voice& v = i < voices.size() ? voices[i] : init;
        data.insert(data.end(), v.begin(), v.end());
    }
    Bytes m = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    m.insert(m.end(), data.begin(), data.end());
    m.push_back(dxsum(data.data(), data.size()));
    m.push_back(0xF7);
    return m;
}

std::vector<Bytes> splitSysex(const Bytes& stream) {
    std::vector<Bytes> out;
    long start = -1;
    for (size_t i = 0; i < stream.size(); ++i) {
        if (stream[i] == 0xF0) start = long(i);
        else if (stream[i] == 0xF7 && start >= 0) {
            out.emplace_back(stream.begin() + start, stream.begin() + long(i) + 1);
            start = -1;
        }
    }
    return out;
}

SyxContents readSyx(const Bytes& file) {
    SyxContents out;
    auto msgs = splitSysex(file);
    for (size_t i = 0; i < msgs.size(); ++i) {
        const Bytes& m = msgs[i];
        try {
            if (m.size() > 4 && m[1] == 0x43 && m[3] == kSubId && m[4] == 0x04) {
                auto s = decodeExact(m);
                if (!s) throw CodecError("a sound message of the wrong size");
                out.sounds.push_back(*s);
            } else if (m.size() == 4104 && m[1] == 0x43 && m[3] == 0x09) {
                const uint8_t* data = m.data() + 6;
                if (dxsum(data, 4096) != m[4102]) throw CodecError("DX7 bank checksum wrong");
                for (int v = 0; v < 32; ++v) {
                    Voice raw{};
                    std::copy(data + v * 128, data + v * 128 + 128, raw.begin());
                    Sound s;
                    // normalised through the firmware's own unpack and pack
                    s.voice = packVoice(unpackVoice(raw));
                    s.from = "DX7 bank";
                    out.sounds.push_back(s);
                }
            } else if (m.size() == 163 && m[1] == 0x43 && m[3] == 0x00) {
                Edit e{};
                std::copy(m.begin() + 6, m.begin() + 6 + kEditBytes, e.begin());
                if (dxsum(e.data(), e.size()) != m[161]) throw CodecError("DX7 voice checksum wrong");
                Sound s;
                s.voice = packVoice(e);
                s.from = "DX7 voice";
                out.sounds.push_back(s);
            } else {
                char msg[64];
                std::snprintf(msg, sizeof msg, "not a sound (%zu bytes)", m.size());
                throw CodecError(msg);
            }
        } catch (const CodecError& err) {
            out.skipped.push_back("message " + std::to_string(i + 1) + ": " + err.what());
        }
    }
    return out;
}

Bytes toSyx(const std::vector<Sound>& sounds) {
    Bytes out;
    for (const Sound& s : sounds) {
        Bytes m = encodeExact(s.slot, s.voice, s.hasRecord ? s.record : defaultRecord());
        out.insert(out.end(), m.begin(), m.end());
    }
    return out;
}

}  // namespace fm1
