#include "core/TmdbStore.hpp"

#include <borealis/core/logger.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "utils/config_helper.hpp"

namespace {

constexpr char MAGIC[4]         = {'T', 'M', 'D', '2'};
constexpr uint32_t MAX_RECORDS  = 2000000;
constexpr int64_t KEEP_SECONDS  = 180LL * 24 * 3600;  // TMDB's terms: no caching longer than six months
constexpr int64_t RETRY_MISSING = 30LL * 24 * 3600;

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::filesystem::path storeFile() {
    return std::filesystem::path(ProgramConfig::instance().getConfigDir()) / "tmdb" / "meta.bin";
}

template <typename T>
void putRaw(std::string& out, const T& value) {
    out.append(reinterpret_cast<const char*>(&value), sizeof(T));
}

void putString(std::string& out, const std::string& text) {
    auto size = static_cast<uint16_t>(std::min<size_t>(text.size(), 0xFFFF));
    putRaw(out, size);
    out.append(text.data(), size);
}

void putIds(std::string& out, const std::vector<int32_t>& ids) {
    auto count = static_cast<uint8_t>(std::min<size_t>(ids.size(), 0xFF));
    putRaw(out, count);
    for (size_t i = 0; i < count; i++) putRaw(out, ids[i]);
}

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

    bool getString(std::string& text) {
        uint16_t size = 0;
        if (!get(size) || pos + size > data.size()) return false;
        text.assign(data.data() + pos, size);
        pos += size;
        return true;
    }

    bool getIds(std::vector<int32_t>& ids) {
        uint8_t count = 0;
        if (!get(count)) return false;
        ids.resize(count);
        for (auto& id : ids)
            if (!get(id)) return false;
        return true;
    }

private:
    const std::string& data;
    size_t pos = 0;
};

}  // namespace

TmdbStore& TmdbStore::instance() {
    // Never destroyed: a worker that could not stop in time may still write while the app closes
    static auto* store = new TmdbStore();
    return *store;
}

void TmdbStore::load() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (loaded) return;
    }
    Map read;
    std::unordered_map<int64_t, std::string> readNames;
    std::ifstream in(storeFile(), std::ios::binary);
    std::string data;
    if (in) {
        // One read of the whole file: many small reads are slow on the SD card
        in.seekg(0, std::ios::end);
        auto size = static_cast<std::streamoff>(in.tellg());
        in.seekg(0);
        if (size > 0) {
            data.resize(static_cast<size_t>(size));
            if (!in.read(&data[0], size)) data.clear();
        }
    }
    if (!data.empty()) {
        Reader reader(data);
        char magic[4];
        uint32_t count = 0, nameCount = 0;
        bool ok = true;
        for (char& c : magic) ok = ok && reader.get(c);
        ok = ok && std::memcmp(magic, MAGIC, 4) == 0 && reader.get(count) && count <= MAX_RECORDS;
        for (uint32_t i = 0; ok && i < count; i++) {
            uint8_t type = 0, missing = 0;
            int32_t id   = 0;
            TmdbMeta meta;
            ok = reader.get(type) && reader.get(id) && reader.get(meta.fetched) && reader.get(missing) &&
                 reader.get(meta.genres) && reader.get(meta.year) && reader.get(meta.popularity) &&
                 reader.get(meta.vote) && reader.get(meta.votes) && reader.get(meta.revenue) &&
                 reader.get(meta.collection) && reader.get(meta.lang[0]) && reader.get(meta.lang[1]) &&
                 reader.get(meta.country[0]) && reader.get(meta.country[1]) && reader.get(meta.status) &&
                 reader.get(meta.seasons) && reader.getIds(meta.keywords) && reader.getIds(meta.companies) &&
                 reader.getString(meta.backdrop);
            if (!ok) break;
            meta.missing = missing != 0;
            read.emplace(key(type, id), std::move(meta));
        }
        ok = ok && reader.get(nameCount) && nameCount <= MAX_RECORDS;
        for (uint32_t i = 0; ok && i < nameCount; i++) {
            int64_t id = 0;
            std::string name;
            ok = reader.get(id) && reader.getString(name);
            if (ok) readNames.emplace(id, std::move(name));
        }
        if (!ok) {
            // A damaged file: what was read stays, the rest is fetched again
            brls::Logger::warning("TmdbStore: meta.bin is damaged after {} records", read.size());
        }
    }
    std::lock_guard<std::mutex> lock(mutex);
    // put() may have run before the file was read: its newer records win
    for (auto& [k, meta] : read) map.emplace(k, std::move(meta));
    for (auto& [id, name] : readNames) names.emplace(id, std::move(name));
    loaded   = true;
    lastSave = nowSeconds();
    brls::Logger::info("TmdbStore: {} records", map.size());
}

bool TmdbStore::isLoaded() const {
    std::lock_guard<std::mutex> lock(mutex);
    return loaded;
}

bool TmdbStore::get(int type, int id, TmdbMeta& meta) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = map.find(key(type, id));
    if (it == map.end()) return false;
    meta = it->second;
    return true;
}

void TmdbStore::put(int type, int id, TmdbMeta meta, const Names& newNames) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& [nameId, name] : newNames)
        if (!name.empty()) names[nameId] = name;
    map[key(type, id)] = std::move(meta);
    dirty              = true;
}

bool TmdbStore::needsFetch(int type, int id, int64_t now) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = map.find(key(type, id));
    if (it == map.end()) return true;
    int64_t age = now - it->second.fetched;
    return age > (it->second.missing ? RETRY_MISSING : KEEP_SECONDS) || age < 0;
}

std::string TmdbStore::name(NameKind kind, int id) const {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = names.find(nameKey(kind, id));
    return it != names.end() ? it->second : "";
}

size_t TmdbStore::size() const {
    std::lock_guard<std::mutex> lock(mutex);
    return map.size();
}

void TmdbStore::save(bool force) {
    std::string data;
    {
        std::lock_guard<std::mutex> lock(mutex);
        int64_t now = nowSeconds();
        if (!loaded || !dirty || (!force && now - lastSave < 60)) return;
        dirty    = false;
        lastSave = now;
        data.reserve(map.size() * 96 + names.size() * 32 + 16);
        data.append(MAGIC, 4);
        putRaw(data, static_cast<uint32_t>(map.size()));
        for (const auto& [k, meta] : map) {
            putRaw(data, static_cast<uint8_t>(k >> 32));
            putRaw(data, static_cast<int32_t>(k & 0xFFFFFFFF));
            putRaw(data, meta.fetched);
            putRaw(data, static_cast<uint8_t>(meta.missing ? 1 : 0));
            putRaw(data, meta.genres);
            putRaw(data, meta.year);
            putRaw(data, meta.popularity);
            putRaw(data, meta.vote);
            putRaw(data, meta.votes);
            putRaw(data, meta.revenue);
            putRaw(data, meta.collection);
            putRaw(data, meta.lang[0]);
            putRaw(data, meta.lang[1]);
            putRaw(data, meta.country[0]);
            putRaw(data, meta.country[1]);
            putRaw(data, meta.status);
            putRaw(data, meta.seasons);
            putIds(data, meta.keywords);
            putIds(data, meta.companies);
            putString(data, meta.backdrop);
        }
        putRaw(data, static_cast<uint32_t>(names.size()));
        for (const auto& [id, name] : names) {
            putRaw(data, id);
            putString(data, name);
        }
    }
    // Two workers may save at the same moment: one file write at a time
    static std::mutex fileMutex;
    std::lock_guard<std::mutex> fileLock(fileMutex);
    std::error_code ec;
    auto file = storeFile();
    std::filesystem::create_directories(file.parent_path(), ec);
    auto temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(data.data(), static_cast<std::streamsize>(data.size()))) {
            brls::Logger::error("TmdbStore: cannot write {}", temp.string());
            return;
        }
    }
    std::filesystem::remove(file, ec);
    std::filesystem::rename(temp, file, ec);
    if (ec) brls::Logger::error("TmdbStore: cannot replace meta.bin: {}", ec.message());
}

void TmdbStore::clear() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        map.clear();
        names.clear();
        dirty = false;
    }
    std::error_code ec;
    std::filesystem::remove(storeFile(), ec);
}
