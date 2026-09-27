#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tsvitch {

/// The genres the app shows, one bit each. TMDB's movie and TV genres and the genre texts in the providers' lists
/// (Turkish, English and a few other languages) all map onto them.
namespace genre {

enum Bit : uint32_t {
    ACTION      = 1u << 0,
    ADVENTURE   = 1u << 1,
    ANIMATION   = 1u << 2,
    COMEDY      = 1u << 3,
    CRIME       = 1u << 4,
    DOCUMENTARY = 1u << 5,
    DRAMA       = 1u << 6,
    FAMILY      = 1u << 7,
    FANTASY     = 1u << 8,
    HISTORY     = 1u << 9,
    HORROR      = 1u << 10,
    MUSIC       = 1u << 11,
    MYSTERY     = 1u << 12,
    ROMANCE     = 1u << 13,
    SCIFI       = 1u << 14,
    THRILLER    = 1u << 15,
    WAR         = 1u << 16,
    WESTERN     = 1u << 17,
    KIDS        = 1u << 18,
    REALITY     = 1u << 19,
    SOAP        = 1u << 20,
    TALK        = 1u << 21,
    NEWS        = 1u << 22,
    POLITICS    = 1u << 23,
};

struct Info {
    uint32_t bit;
    const char* key;  // i18n key of the name, also a stable id ("action")
    uint32_t colorA;  // tile gradient, 0xRRGGBB
    uint32_t colorB;
};

/// Every genre in the order they are listed
const std::vector<Info>& all();

const Info* find(uint32_t bit);
const Info* findByKey(const std::string& key);

/// Name in the app's language
std::string name(uint32_t bit);

/// TMDB genre id (movie or TV) -> bits; TV's combined genres ("Action & Adventure") give two bits
uint32_t fromTmdb(int id);

/// A provider's genre text such as "Aksiyon & Macera / Suç" or "Drama, Crime" -> bits (unknown words are ignored)
uint32_t fromText(const std::string& text);

/// The genres in a set of bits, in the listing order
std::vector<uint32_t> split(uint32_t bits);

}  // namespace genre
}  // namespace tsvitch
