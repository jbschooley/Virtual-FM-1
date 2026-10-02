// Fm1Codec -- the FM-1's stored sounds and the SysEx that reads and writes them.
//
// A C++ port of the protocol implemented by baud girl's FM-1+VA web tools
// (fm1sound.js, fm1seq.js, ota.js), which is the only public description of the
// firmware's read-back protocol. Everything here is pure byte transformation;
// nothing talks to a device.
//
// A stored sound is two records:
//   voice   128 B, the DX7 packed layout (VMEM): 17 B per operator, OP6 first;
//           pitch EG 102..109, algorithm 110, name 118..127
//   record   59 B of settings: effects [0..17] and [27..44], engine marker [18]
//           (0x5A = Virtual Analog, 0xA5 = FM), engine settings [19..22],
//           VA filter [23..26], A D S R [54..57], [58]
// The firmware takes the voice UNPACKED (the 155-byte DX7 edit buffer, VCED)
// and packs it itself.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace fm1 {

constexpr int kVoiceBytes  = 128;
constexpr int kEditBytes   = 155;
constexpr int kRecordBytes = 59;
constexpr int kSlots       = 128;
constexpr int kBankSlots   = 32;   // slots per bank A..D

constexpr uint8_t kMarkVA = 0x5A;
constexpr uint8_t kMarkFM = 0xA5;
constexpr uint8_t kSubId  = 0x7D;
constexpr size_t  kExactLen = 231;

constexpr int kLastStockVersion = 19;  // FM-1_020 and up is FM-1+VA, or Felucca from FM-1_900 (sync/Firmware.cpp)

using Bytes  = std::vector<uint8_t>;
using Voice  = std::array<uint8_t, kVoiceBytes>;
using Edit   = std::array<uint8_t, kEditBytes>;
using Record = std::array<uint8_t, kRecordBytes>;

enum class Engine { FM, VA };

struct Sound {
    int slot = -1;              // 0..127, or -1 when the source gave none
    Voice voice{};
    Record record{};
    bool hasRecord = false;     // false for plain DX7 voices/banks
    std::string from;           // "FM-1", "DX7 bank", "DX7 voice"
};

class CodecError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// ---- checksums and packing ---------------------------------------------------

// The firmware's 7-bit Yamaha-style sum: sum of (b ^ 0xFF), low 7 bits.
uint8_t ysum(const uint8_t* b, size_t n);
inline uint8_t ysum(const Bytes& b) { return ysum(b.data(), b.size()); }

// DX7 checksum: two's complement of the sum, 7 bits.
uint8_t dxsum(const uint8_t* b, size_t n);

// 8-into-7: groups of seven bytes, each preceded by a byte of their top bits.
Bytes pack87(const uint8_t* b, size_t n);
Bytes unpack87(const uint8_t* packed, size_t packedLen, size_t n);

// 7-bit LSB-first continuous bitstream (the updater's and the reply packing).
Bytes pack7(const uint8_t* b, size_t n);
Bytes unpack7(const uint8_t* b, size_t n);

// ---- the voice ---------------------------------------------------------------

Edit  unpackVoice(const Voice& v);   // 128 -> 155, the firmware's own unpack
Voice packVoice(const Edit& e);      // 155 -> 128, the firmware's own pack

std::string voiceName(const Voice& v);              // 10 chars, '?' for non-ASCII
Voice withName(const Voice& v, const std::string& name);
extern const Edit kInitEdit;                        // Dexed's INIT VOICE

// ---- the record --------------------------------------------------------------

Engine engineOf(const Record& r);
Record defaultRecord();   // FM, every effect off in default order, filter unset

// ---- requests to the synth -----------------------------------------------

Bytes encodeSoundRead(int slot);                 // F0 43 00 7D 10 <slot> sum F7
Bytes encodeMemRead(uint32_t addr, int n);       // F0 43 00 7D 11 <addr:5x7> <n:2x7> sum F7, n <= 256
Bytes encodeExact(int slot, const Voice& v, const Record& r);  // 231 bytes, "the sound exactly"
std::optional<Sound> decodeExact(const Bytes& m); // nullopt if not a v4 message; throws CodecError on bad checksum

// ---- replies from the synth ------------------------------------------------

struct Reply {
    enum class Kind { Sound, Mem, Pattern, Unknown };
    Kind kind = Kind::Unknown;
    uint8_t status = 0;
    uint32_t arg = 0;
    Bytes data;
};

extern const char* const kStatusText[4];

// nullopt if the frame is not one of the firmware's replies; throws CodecError
// if it is one but damaged.
std::optional<Reply> decodeReply(const uint8_t* sx, size_t n);
inline std::optional<Reply> decodeReply(const Bytes& b) { return decodeReply(b.data(), b.size()); }

Sound soundFromReply(const Reply& r);  // throws CodecError on refusal or wrong size

// ---- identity (the updater's handshake, answered by stock and FM-1+VA) ------

extern const Bytes kIdentityQuery;  // F0 00 32 45 00 00 00 40 7F F7

struct Identity {
    std::string model;   // "FM-1"
    int version = 0;     // 89 for FM-1_089
    std::string name() const;
    bool isStock() const { return version <= kLastStockVersion; }
    bool canReadBack() const { return !isStock(); }
};

std::optional<Identity> parseIdentity(const uint8_t* sx, size_t n);

// ---- DX7 SysEx and .syx files --------------------------------------------

Bytes toDx7Voice(const Voice& v);                  // F0 43 00 00 01 1B <155> sum F7
Bytes toDx7Bank(const std::vector<Voice>& voices); // F0 43 00 09 20 00 <32x128> sum F7, padded with INIT VOICE

std::vector<Bytes> splitSysex(const Bytes& stream);

struct SyxContents {
    std::vector<Sound> sounds;
    std::vector<std::string> skipped;
};
SyxContents readSyx(const Bytes& file);        // v4 messages, DX7 banks, DX7 voices
Bytes toSyx(const std::vector<Sound>& sounds); // one v4 message per sound

}  // namespace fm1
