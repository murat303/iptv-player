#include "core/XtreamStore.hpp"

#include <borealis/core/logger.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "utils/config_helper.hpp"
#include "utils/text_fold.hpp"

namespace {

// Bumped whenever the item layout changes. Version 1 files (no year) are still read.
constexpr char MAGIC[4]          = {'X', 'T', 'C', '2'};
constexpr char MAGIC_V1[4]       = {'X', 'T', 'C', '1'};
constexpr uint32_t MAX_ITEMS     = 1000000;  // guards against a damaged file

std::filesystem::path storeDir() { return std::filesystem::path(ProgramConfig::instance().getConfigDir()) / "xtream"; }

std::filesystem::path storeFile(int contentType) {
    return storeDir() / (contentType == 2 ? "series.bin" : contentType == 1 ? "movies.bin" : "live.bin");
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

template <typename T>
void put(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void putString(std::string& out, const std::string& str) {
    put<uint32_t>(out, static_cast<uint32_t>(str.size()));
    out.append(str);
}

/// Bounds-checked reader over the whole file contents
class Reader {
public:
    explicit Reader(const std::string& data) : data(data) {}

    template <typename T>
    bool get(T& value) {
        if (pos + sizeof(T) > data.size()) return false;
        std::memcpy(&value, data.data() + pos, sizeof(T));
        pos += sizeof(T);
        return true;
    }

    bool getString(std::string& str) {
        uint32_t size = 0;
        if (!get(size) || pos + size > data.size()) return false;
        str.assign(data.data() + pos, size);
        pos += size;
        return true;
    }

private:
    const std::string& data;
    size_t pos = 0;
};

}  // namespace

bool XtreamStore::load(int contentType, tsvitch::LiveM3u8ListResult& list, int64_t& savedAt) {
    auto file = storeFile(contentType);
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;

    // One read of the whole file is much faster on the SD card than many small ones
    in.seekg(0, std::ios::end);
    auto size = static_cast<std::streamoff>(in.tellg());
    if (size <= 0) return false;
    std::string data(static_cast<size_t>(size), '\0');
    in.seekg(0);
    if (!in.read(&data[0], size)) return false;

    Reader reader(data);
    char magic[sizeof(MAGIC)];
    for (char& c : magic)
        if (!reader.get(c)) return false;
    uint32_t count = 0;
    bool version1  = std::memcmp(magic, MAGIC_V1, sizeof(MAGIC_V1)) == 0;
    if ((!version1 && std::memcmp(magic, MAGIC, sizeof(MAGIC)) != 0) || !reader.get(savedAt) || !reader.get(count) ||
        count > MAX_ITEMS) {
        brls::Logger::warning("XtreamStore: {} has an unknown format, ignoring it", file.string());
        return false;
    }

    tsvitch::LiveM3u8ListResult items;
    items.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        tsvitch::LiveM3u8 item;
        int32_t type = 0, year = 0;
        if (!reader.getString(item.id) || !reader.getString(item.chno) || !reader.getString(item.title) ||
            !reader.getString(item.logo) || !reader.getString(item.groupTitle) || !reader.getString(item.url) ||
            !reader.get(item.rating) || !reader.get(item.added) || !reader.get(type) ||
            (!version1 && !reader.get(year))) {
            brls::Logger::warning("XtreamStore: {} is damaged, ignoring it", file.string());
            return false;
        }
        item.type = type;
        item.year = version1 && type != 0 ? tsvitch::yearFromText(item.title) : year;
        items.push_back(std::move(item));
    }
    list = std::move(items);
    brls::Logger::info("XtreamStore: loaded {} items of type {}", list.size(), contentType);
    return true;
}

void XtreamStore::save(int contentType, const tsvitch::LiveM3u8ListResult& list) {
    std::error_code ec;
    std::filesystem::create_directories(storeDir(), ec);

    std::string data;
    data.reserve(list.size() * 256 + 32);
    data.append(MAGIC, sizeof(MAGIC));
    put<int64_t>(data, nowSeconds());
    put<uint32_t>(data, static_cast<uint32_t>(list.size()));
    for (const auto& item : list) {
        putString(data, item.id);
        putString(data, item.chno);
        putString(data, item.title);
        putString(data, item.logo);
        putString(data, item.groupTitle);
        putString(data, item.url);
        put<float>(data, item.rating);
        put<int64_t>(data, item.added);
        put<int32_t>(data, static_cast<int32_t>(item.type));
        put<int32_t>(data, static_cast<int32_t>(item.year));
    }

    // Write a temporary file first so a crash mid-write never leaves a broken cache behind
    auto file = storeFile(contentType);
    auto temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(data.data(), static_cast<std::streamsize>(data.size()))) {
            brls::Logger::error("XtreamStore: cannot write {}", temp.string());
            return;
        }
    }
    std::filesystem::remove(file, ec);
    std::filesystem::rename(temp, file, ec);
    if (ec) {
        brls::Logger::error("XtreamStore: cannot replace {}: {}", file.string(), ec.message());
        return;
    }
    brls::Logger::info("XtreamStore: saved {} items of type {} ({} KB)", list.size(), contentType, data.size() / 1024);
}

void XtreamStore::clear() {
    std::error_code ec;
    for (int type = 0; type <= 2; type++) std::filesystem::remove(storeFile(type), ec);
}

bool XtreamStore::exists(int contentType) {
    std::error_code ec;
    return std::filesystem::exists(storeFile(contentType), ec);
}

bool XtreamStore::isStale(int64_t savedAt) {
    if (savedAt <= 0) return true;
    int mode = ProgramConfig::instance().getSettingItem(SettingItem::XTREAM_AUTO_REFRESH, 0);
    if (mode == 2) return false;  // only with the refresh button
    return nowSeconds() - savedAt > (mode == 1 ? 7 : 1) * MAX_AGE_SECONDS;
}

bool XtreamStore::header(int contentType, int64_t& savedAt, uint32_t& count) {
    std::ifstream in(storeFile(contentType), std::ios::binary);
    char data[16];
    if (!in.read(data, sizeof(data))) return false;
    if (std::memcmp(data, MAGIC, 4) != 0 && std::memcmp(data, MAGIC_V1, 4) != 0) return false;
    std::memcpy(&savedAt, data + 4, sizeof(savedAt));
    std::memcpy(&count, data + 12, sizeof(count));
    return count <= MAX_ITEMS;
}

namespace {
std::filesystem::path sizeFile(int contentType) {
    return storeDir() / (contentType == 2 ? "series.size" : contentType == 1 ? "movies.size" : "live.size");
}
}  // namespace

int64_t XtreamStore::lastDownloadSize(int contentType) {
    std::ifstream in(sizeFile(contentType));
    int64_t bytes = 0;
    if (!(in >> bytes) || bytes < 0) return 0;
    return bytes;
}

void XtreamStore::rememberDownloadSize(int contentType, int64_t bytes) {
    std::error_code ec;
    std::filesystem::create_directories(storeDir(), ec);
    std::ofstream out(sizeFile(contentType), std::ios::trunc);
    out << bytes;
}
