#pragma once

#include <string>

namespace tsvitch {

/**
 * Lowercases a UTF-8 string for matching and folds Turkish letters to plain ASCII
 * (Ç/ç -> c, Ğ/ğ -> g, İ/ı/I -> i, Ö/ö -> o, Ş/ş -> s, Ü/ü -> u), so "ates" finds "Ateş"
 * and "iron" finds "IRON". Other characters are kept as they are.
 */
inline std::string foldForSearch(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); i++) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            out += static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            continue;
        }
        if (i + 1 < text.size()) {
            unsigned char d = static_cast<unsigned char>(text[i + 1]);
            char folded     = 0;
            if (c == 0xC3) {
                if (d == 0x87 || d == 0xA7) folded = 'c';       // Ç ç
                else if (d == 0x96 || d == 0xB6) folded = 'o';  // Ö ö
                else if (d == 0x9C || d == 0xBC) folded = 'u';  // Ü ü
            } else if (c == 0xC4) {
                if (d == 0x9E || d == 0x9F) folded = 'g';       // Ğ ğ
                else if (d == 0xB0 || d == 0xB1) folded = 'i';  // İ ı
            } else if (c == 0xC5) {
                if (d == 0x9E || d == 0x9F) folded = 's';  // Ş ş
            }
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

/**
 * Year of release in a text such as "Rise - 2022", "Fury (2024) TR" or "2024-03-01";
 * the last year found wins ("2001: A Space Odyssey (1968)" -> 1968). 0 when there is none.
 */
inline int yearFromText(const std::string& text) {
    auto isDigit = [&text](size_t i) { return i < text.size() && text[i] >= '0' && text[i] <= '9'; };
    int found    = 0;
    for (size_t i = 0; i + 4 <= text.size(); i++) {
        if (!isDigit(i) || !isDigit(i + 1) || !isDigit(i + 2) || !isDigit(i + 3)) continue;
        if ((i > 0 && isDigit(i - 1)) || isDigit(i + 4)) continue;
        int year = (text[i] - '0') * 1000 + (text[i + 1] - '0') * 100 + (text[i + 2] - '0') * 10 + (text[i + 3] - '0');
        if (year >= 1900 && year <= 2099) found = year;
    }
    return found;
}

}  // namespace tsvitch
