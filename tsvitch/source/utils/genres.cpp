#include "utils/genres.hpp"

#include <unordered_map>

#include <borealis/core/i18n.hpp>

#include "utils/text_fold.hpp"

namespace tsvitch::genre {

const std::vector<Info>& all() {
    static const std::vector<Info> genres = {
        {ACTION, "action", 0xF4511E, 0xB71C1C},        {ADVENTURE, "adventure", 0xFFB300, 0xE65100},
        {ANIMATION, "animation", 0x26C6DA, 0x3949AB},  {COMEDY, "comedy", 0xFDD835, 0xF57C00},
        {CRIME, "crime", 0x607D8B, 0x1C272C},          {DOCUMENTARY, "documentary", 0x66BB6A, 0x1B5E20},
        {DRAMA, "drama", 0x7E57C2, 0x311B92},          {FAMILY, "family", 0xFF8A65, 0xD81B60},
        {FANTASY, "fantasy", 0xAB47BC, 0x1A237E},      {HISTORY, "history", 0xA1887F, 0x4E342E},
        {HORROR, "horror", 0xB71C1C, 0x1A0000},        {MUSIC, "music", 0xEC407A, 0x6A1B9A},
        {MYSTERY, "mystery", 0x5C6BC0, 0x14143A},      {ROMANCE, "romance", 0xF06292, 0xAD1457},
        {SCIFI, "scifi", 0x29B6F6, 0x0D47A1},          {THRILLER, "thriller", 0x546E7A, 0x0B0F12},
        {WAR, "war", 0x9E9D24, 0x33691E},              {WESTERN, "western", 0xD7A45A, 0x7B4A1E},
        {KIDS, "kids", 0x9CCC65, 0x00897B},            {REALITY, "reality", 0xFFA726, 0xE53935},
        {SOAP, "soap", 0xCE93D8, 0x8E24AA},            {TALK, "talk", 0x90A4AE, 0x37474F},
        {NEWS, "news", 0x42A5F5, 0x1565C0},            {POLITICS, "politics", 0x8D6E63, 0x37474F},
    };
    return genres;
}

const Info* find(uint32_t bit) {
    for (const auto& info : all())
        if (info.bit == bit) return &info;
    return nullptr;
}

const Info* findByKey(const std::string& key) {
    for (const auto& info : all())
        if (key == info.key) return &info;
    return nullptr;
}

std::string name(uint32_t bit) {
    const Info* info = find(bit);
    return info ? brls::getStr(std::string("tsvitch/genre/") + info->key) : "";
}

uint32_t fromTmdb(int id) {
    switch (id) {
        case 28: return ACTION;
        case 12: return ADVENTURE;
        case 16: return ANIMATION;
        case 35: return COMEDY;
        case 80: return CRIME;
        case 99: return DOCUMENTARY;
        case 18: return DRAMA;
        case 10751: return FAMILY;
        case 14: return FANTASY;
        case 36: return HISTORY;
        case 27: return HORROR;
        case 10402: return MUSIC;
        case 9648: return MYSTERY;
        case 10749: return ROMANCE;
        case 878: return SCIFI;
        case 53: return THRILLER;
        case 10752: return WAR;
        case 37: return WESTERN;
        // TV
        case 10759: return ACTION | ADVENTURE;
        case 10762: return KIDS;
        case 10763: return NEWS;
        case 10764: return REALITY;
        case 10765: return SCIFI | FANTASY;
        case 10766: return SOAP;
        case 10767: return TALK;
        case 10768: return WAR | POLITICS;
        default: return 0;  // 10770 TV movie and unknown ids
    }
}

namespace {

/// Lowercase ASCII with the Turkish letters and the common Latin accents folded ("Ação" -> "acao")
std::string fold(const std::string& text) {
    std::string in = foldForSearch(text), out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        auto c = static_cast<unsigned char>(in[i]);
        if (c == 0xC3 && i + 1 < in.size()) {
            auto d      = static_cast<unsigned char>(in[i + 1]);
            char folded = 0;
            if ((d >= 0x80 && d <= 0x85) || (d >= 0xA0 && d <= 0xA5)) folded = 'a';
            else if (d == 0x87 || d == 0xA7) folded = 'c';
            else if ((d >= 0x88 && d <= 0x8B) || (d >= 0xA8 && d <= 0xAB)) folded = 'e';
            else if ((d >= 0x8C && d <= 0x8F) || (d >= 0xAC && d <= 0xAF)) folded = 'i';
            else if (d == 0x91 || d == 0xB1) folded = 'n';
            else if ((d >= 0x92 && d <= 0x96) || (d >= 0xB2 && d <= 0xB6)) folded = 'o';
            else if ((d >= 0x99 && d <= 0x9C) || (d >= 0xB9 && d <= 0xBC)) folded = 'u';
            if (folded) {
                out += folded;
                i++;
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

std::string trim(const std::string& text) {
    auto first = text.find_first_not_of(" \t\r\n.-");
    if (first == std::string::npos) return "";
    auto last = text.find_last_not_of(" \t\r\n.-");
    return text.substr(first, last - first + 1);
}

const std::unordered_map<std::string, uint32_t>& names() {
    // Folded names (see fold) in English, Turkish, Italian, Portuguese, Spanish, German and French
    static const std::unordered_map<std::string, uint32_t> table = {
        {"action", ACTION}, {"aksiyon", ACTION}, {"azione", ACTION}, {"acao", ACTION}, {"accion", ACTION},
        {"adventure", ADVENTURE}, {"macera", ADVENTURE}, {"avventura", ADVENTURE}, {"aventura", ADVENTURE},
        {"abenteuer", ADVENTURE}, {"aventure", ADVENTURE},
        {"animation", ANIMATION}, {"animasyon", ANIMATION}, {"animazione", ANIMATION}, {"animacao", ANIMATION},
        {"animacion", ANIMATION}, {"anime", ANIMATION},
        {"comedy", COMEDY}, {"komedi", COMEDY}, {"commedia", COMEDY}, {"comedia", COMEDY}, {"komodie", COMEDY},
        {"comedie", COMEDY},
        {"crime", CRIME}, {"suc", CRIME}, {"crimine", CRIME}, {"crimen", CRIME}, {"krimi", CRIME},
        {"documentary", DOCUMENTARY}, {"belgesel", DOCUMENTARY}, {"documentario", DOCUMENTARY},
        {"documental", DOCUMENTARY}, {"dokumentarfilm", DOCUMENTARY}, {"dokumentation", DOCUMENTARY},
        {"documentaire", DOCUMENTARY},
        {"drama", DRAMA}, {"dram", DRAMA}, {"dramma", DRAMA}, {"drame", DRAMA},
        {"family", FAMILY}, {"aile", FAMILY}, {"famiglia", FAMILY}, {"familia", FAMILY}, {"familie", FAMILY},
        {"familial", FAMILY},
        {"fantasy", FANTASY}, {"fantastik", FANTASY}, {"fantazi", FANTASY}, {"fantasia", FANTASY},
        {"fantastique", FANTASY},
        {"history", HISTORY}, {"tarih", HISTORY}, {"storia", HISTORY}, {"storico", HISTORY}, {"historia", HISTORY},
        {"historie", HISTORY}, {"histoire", HISTORY},
        {"horror", HORROR}, {"korku", HORROR}, {"terror", HORROR}, {"horreur", HORROR}, {"epouvante", HORROR},
        {"music", MUSIC}, {"muzik", MUSIC}, {"musica", MUSIC}, {"musik", MUSIC}, {"musique", MUSIC},
        {"musical", MUSIC}, {"muzikal", MUSIC},
        {"mystery", MYSTERY}, {"gizem", MYSTERY}, {"mistero", MYSTERY}, {"misterio", MYSTERY},
        {"mystere", MYSTERY},
        {"romance", ROMANCE}, {"romantik", ROMANCE}, {"romantico", ROMANCE}, {"romantique", ROMANCE},
        {"romantic", ROMANCE},
        {"science fiction", SCIFI}, {"science-fiction", SCIFI}, {"sci-fi", SCIFI}, {"scifi", SCIFI},
        {"bilim kurgu", SCIFI}, {"bilim-kurgu", SCIFI}, {"bilimkurgu", SCIFI}, {"fantascienza", SCIFI},
        {"ficcao cientifica", SCIFI}, {"ciencia ficcion", SCIFI},
        {"thriller", THRILLER}, {"gerilim", THRILLER}, {"suspense", THRILLER},
        {"war", WAR}, {"savas", WAR}, {"guerra", WAR}, {"krieg", WAR}, {"guerre", WAR},
        {"western", WESTERN}, {"vahsi bati", WESTERN}, {"faroeste", WESTERN},
        {"kids", KIDS}, {"cocuk", KIDS}, {"cocuklar", KIDS}, {"bambini", KIDS}, {"infantil", KIDS},
        {"kinder", KIDS}, {"enfants", KIDS},
        {"reality", REALITY}, {"gerceklik", REALITY}, {"reality-tv", REALITY}, {"realita", REALITY},
        {"soap", SOAP}, {"pembe dizi", SOAP}, {"telenovela", SOAP}, {"soap opera", SOAP},
        {"talk", TALK}, {"talk show", TALK}, {"talk-show", TALK},
        {"news", NEWS}, {"haber", NEWS}, {"haberler", NEWS}, {"notizie", NEWS}, {"noticias", NEWS},
        {"nachrichten", NEWS},
        {"politics", POLITICS}, {"politik", POLITICS}, {"politica", POLITICS}, {"politique", POLITICS},
    };
    return table;
}

}  // namespace

uint32_t fromText(const std::string& text) {
    if (text.empty()) return 0;
    uint32_t bits  = 0;
    std::string folded = fold(text), word;
    auto flush = [&bits, &word]() {
        std::string key = trim(word);
        word.clear();
        if (key.empty()) return;
        auto it = names().find(key);
        if (it != names().end()) bits |= it->second;
    };
    for (char c : folded) {
        if (c == '/' || c == ',' || c == '&' || c == '|' || c == ';' || c == '+') flush();
        else word += c;
    }
    flush();
    return bits;
}

std::vector<uint32_t> split(uint32_t bits) {
    std::vector<uint32_t> out;
    for (const auto& info : all())
        if (bits & info.bit) out.push_back(info.bit);
    return out;
}

}  // namespace tsvitch::genre
