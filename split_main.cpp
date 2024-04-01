#include <sstream>
#include <fstream>

#include <memory>
#include <cstring>

#include <filesystem>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <fmt/printf.h>

#ifdef _WIN32
#include <windows.h>
#include <fileapi.h>
#include <synchapi.h>
#define usleep(ms) Sleep(ms)
#define sleep(s) usleep(s*1000)
#else
#include <unistd.h>
#include <sys/types.h>
#endif

uintmax_t SPLIT_SIZE;
std::string SPLIT_PREFIX;

bool is_ls = false;
bool is_split = false;
bool is_join = false;
bool command_selected = false;
bool dry_run = false;
bool remove_files = false;
bool next_is_size = false;
bool next_is_name = false;
bool next_is_help = true;
std::string file;
std::string out_directory;

struct BinWriter {
    FILE* bin = nullptr;
    const char* name = nullptr;

    enum TYPES : uint8_t {
        U8, U16, U32, U64, STR
    };

    void create(const char* name) {
        if (bin == nullptr) {
            this->name = name;
            bin = fopen(name, "wb");
        }
    }

    void close() {
        if (bin != nullptr) {
            fflush(bin);
            fclose(bin);
            bin = nullptr;
        }
    }

    void write_u8(uint8_t value) {
        uint8_t t = U8;
        fwrite(&t, 1, 1, bin);
        fwrite(&value, 1, 1, bin);
    }

    void write_u16(uint16_t value) {
        uint8_t t = U16;
        fwrite(&t, 1, 1, bin);
        fwrite(&value, 1, 2, bin);
    }

    void write_u32(uint32_t value) {
        uint8_t t = U32;
        fwrite(&t, 1, 1, bin);
        fwrite(&value, 1, 4, bin);
    }

    void write_u64(uint64_t value) {
        uint8_t t = U64;
        fwrite(&t, 1, 1, bin);
        fwrite(&value, 1, 8, bin);
    }

    void write_string(const char* value) {
        if (value == nullptr || value[0] == '\0')
            return;

        uint64_t size = (strlen(value)+1) * sizeof(char);
        uint8_t t = STR;
        fwrite(&t, 1, 1, bin);
        fwrite(&size, 1, 8, bin);
        fwrite(value, 1, size, bin);
    }
};

struct BinReader {
    FILE* bin = nullptr;

    void open(const char* name) {
        if (bin == nullptr) {
            bin = fopen(name, "rb");
            if (bin == nullptr) {
                std::string e = "the path could not be opened: ";
                e += name;
                throw std::runtime_error(e);
            }
        }
    }

    void close() {
        if (bin != nullptr) {
            fclose(bin);
            bin = nullptr;
        }
    }

    uint8_t read_u8() {
        uint8_t type;
        fread(&type, 1, 1, bin);
        if (type != BinWriter::U8) {
            throw std::runtime_error("type was not U8");
        }
        fread(&type, 1, 1, bin);
        return type;
    }

    uint16_t read_u16() {
        uint8_t type;
        fread(&type, 1, 1, bin);
        if (type != BinWriter::U16) {
            throw std::runtime_error("type was not U16");
        }
        uint16_t value;
        fread(&value, 1, 2, bin);
        return value;
    }

    uint32_t read_u32() {
        uint8_t type;
        fread(&type, 1, 1, bin);
        if (type != BinWriter::U32) {
            throw std::runtime_error("type was not U32");
        }
        uint32_t value;
        fread(&value, 1, 4, bin);
        return value;
    }

    uint64_t read_u64() {
        uint8_t type;
        fread(&type, 1, 1, bin);
        if (type != BinWriter::U64) {
            throw std::runtime_error("type was not U64");
        }
        uint64_t value;
        fread(&value, 1, 8, bin);
        return value;
    }

    const char * read_string() {
        uint8_t type;
        fread(&type, 1, 1, bin);
        if (type != BinWriter::STR) {
            throw std::runtime_error("type was not STR");
        }
        uint64_t size;
        fread(&size, 1, 8, bin);
        char* value = (char*)malloc(size);
        if (value == nullptr) {
            throw std::bad_alloc();
        }
        fread(value, 1, size, bin);
        return value;
    }
};

// the path converter is done, any path is now converted into a path relative to .
//
// [root]  ..       > .
// [child] ../foo   > foo
// [child] ../foo/a > foo/a
// [root]  ../dir   > .
// [child] ../dir/a > a
// [root]  /        > .
// [child] /a/f/g   > a/f/g
// [root]  /a/      > .
// [child] /a/f/g   > f/g
//
struct PathRecorder {
    std::string trim = {};
    BinWriter w = {};
    BinReader r = {};
    uint64_t unknowns = 0;
    struct ChunkInfo {
        uintmax_t split = 0;
        uintmax_t offset = 0;
        uintmax_t length = 0;
    };
    std::vector<std::pair<std::filesystem::path, std::pair<std::filesystem::perms, std::pair<std::filesystem::file_type, std::filesystem::file_time_type::rep>>>> bird_is_the_word_d = {};
    struct FileInfo {
        std::filesystem::path first;
        uintmax_t second;
        std::filesystem::perms third;
        std::filesystem::file_type forth;
        std::vector<ChunkInfo> fifth;
        std::filesystem::file_time_type::rep sixth;
    };
    std::vector<FileInfo> bird_is_the_word_f = {};
    std::vector<std::pair<std::filesystem::path, std::filesystem::file_type>> bird_is_the_word_s = {};

    inline char permStr(const std::filesystem::perms& perms, const std::filesystem::perms & perm, char op) {
        return ((perms & perm) == perm) ? op : '-';
    }

    std::string permsStr(const std::filesystem::perms& perms) {
        char perm[10];
        perm[0] = permStr(perms, std::filesystem::perms::group_read, 'r');
        perm[1] = permStr(perms, std::filesystem::perms::group_write, 'w');
        perm[2] = permStr(perms, std::filesystem::perms::group_exec, 'x');
        perm[3] = permStr(perms, std::filesystem::perms::owner_read, 'r');
        perm[4] = permStr(perms, std::filesystem::perms::owner_write, 'w');
        perm[5] = permStr(perms, std::filesystem::perms::owner_exec, 'x');
        perm[6] = permStr(perms, std::filesystem::perms::others_read, 'r');
        perm[7] = permStr(perms, std::filesystem::perms::others_write, 'w');
        perm[8] = permStr(perms, std::filesystem::perms::others_exec, 'x');
        perm[9] = '\0';
        return perm;
    }

    inline std::filesystem::perms strPerm(const char & perm_op, const std::filesystem::perms& perm, char op) {
        return perm_op == op ? perm : std::filesystem::perms::none;
    }

    std::filesystem::perms strPerms(const char * perms) {
        std::filesystem::perms perm;
        perm |= strPerm(perms[0], std::filesystem::perms::group_read, 'r');
        perm |= strPerm(perms[1], std::filesystem::perms::group_write, 'w');
        perm |= strPerm(perms[2], std::filesystem::perms::group_exec, 'x');
        perm |= strPerm(perms[3], std::filesystem::perms::owner_read, 'r');
        perm |= strPerm(perms[4], std::filesystem::perms::owner_write, 'w');
        perm |= strPerm(perms[5], std::filesystem::perms::owner_exec, 'x');
        perm |= strPerm(perms[6], std::filesystem::perms::others_read, 'r');
        perm |= strPerm(perms[7], std::filesystem::perms::others_write, 'w');
        perm |= strPerm(perms[8], std::filesystem::perms::others_exec, 'x');
        return perm;
    }

    int split_number = 0;
    bool first_split = true;
    bool open = false;
    uintmax_t current_chunk_size = 0;
    uintmax_t chunk_size = SPLIT_SIZE;
    uintmax_t total_chunk_count = 0;
    uintmax_t max_file_chunks = 0;
    uintmax_t total = 0;
    uintmax_t totalc = 0;

    std::string max_path = {};
    std::filesystem::perms max_perms = {};
    uintmax_t max_size = 0;
    uintmax_t max_chunk = 0;
    FILE* current_split_file = nullptr;

    int _open() {
        if (!open) {
            if (first_split) {
                first_split = false;
            }
            else {
                split_number++;
            }
            if (dry_run) {
                fmt::print("open {}split.{}\n", SPLIT_PREFIX, split_number);
            }
            else {
                std::string split_f = fmt::format("{}split.{}", SPLIT_PREFIX, split_number);
                current_split_file = fopen(split_f.c_str(), "wb");
                if (current_split_file == nullptr) {
                    fmt::print("failed to create file: {}\n", split_f);
                    return 1;
                }
            }
            open = true;
        }
        return 0;
    }

    void _close() {
        if (open) {
            if (dry_run) {
                fmt::print("close {}split.{}\n", SPLIT_PREFIX, split_number);
            }
            else {
                fflush(current_split_file);
                fclose(current_split_file);
                current_split_file = nullptr;
            }
            open = false;
        }
    }

    int recordPath_(const std::filesystem::path& path, const std::filesystem::file_type& type, const std::filesystem::perms& perms) {
        if (type == std::filesystem::file_type::directory) {
            bird_is_the_word_d.emplace_back(std::pair<std::filesystem::path, std::pair<std::filesystem::perms, std::pair<std::filesystem::file_type, std::filesystem::file_time_type::rep>>>(path, std::pair<std::filesystem::perms, std::pair<std::filesystem::file_type, std::filesystem::file_time_type::rep>>(perms, std::pair<std::filesystem::file_type, std::filesystem::file_time_type::rep>(type, std::filesystem::last_write_time(path).time_since_epoch().count()))));
        }
        else if (type == std::filesystem::file_type::regular) {
            std::vector<ChunkInfo> file_chunks;
            uintmax_t s = std::filesystem::file_size(path);
            total += s;
            auto ps = path.string();
            if (_open() == 1) return 1;
            FILE* f;
            if (dry_run) {
                fmt::print("fopen()\n");
            }
            else {
                f = fopen(ps.c_str(), "rb");
                if (f == nullptr) {
                    fmt::print("failed to open file: {}\n", ps);
                    _close();
                    return 1;
                }
            }
            while (s != 0) {
                ChunkInfo chunk;
                // see how much space we have available
                uintmax_t avail = chunk_size - current_chunk_size;
                if (avail == 0) {
                    // we have 0 bytes available, request a new chunk
                    _close();
                    if (_open() == 1) {
                        if (dry_run) {
                            fmt::print("fclose()\n");
                        }
                        else {
                            fclose(f);
                            f = nullptr;
                        }
                        return 1;
                    }
                    current_chunk_size = 0;
                    avail = chunk_size;
                }
                // we have x bytes available
                chunk.split = split_number;
                chunk.offset = current_chunk_size;
                chunk.length = s <= avail ? s : avail;
                current_chunk_size += chunk.length;
                totalc += chunk.length;
                s -= chunk.length;
                if (dry_run) {
                    fmt::print("writing {} bytes ({} bytes left)\n", chunk.length, s);
                    fmt::print("malloc()\n");
                    fmt::print("fread()\n");
                    fmt::print("fwrite()\n");
                    fmt::print("free()\n");
                }
                else {
                    void* buffer = malloc(chunk.length);
                    if (buffer == nullptr) {
                        fmt::print("failed to allocate {} bytes\n", chunk.length);
                        fclose(f);
                        _close();
                        return 1;
                    }
                    fread(buffer, 1, chunk.length, f);
                    fwrite(buffer, 1, chunk.length, current_split_file);
                    free(buffer);
                }
                file_chunks.emplace_back(chunk);
            }
            if (dry_run) {
                fmt::print("fclose()\n");
            }
            else {
                fclose(f);
                f = nullptr;
            }
            total_chunk_count += file_chunks.size();
            max_file_chunks = std::max(max_file_chunks, file_chunks.size());
            uint64_t file_size = std::filesystem::file_size(path);
            if (max_file_chunks == file_chunks.size()) {
                max_path = std::string(&ps[trim.length()]);
                max_size = file_size;
                max_chunk = max_file_chunks;
                max_perms = perms;
            }
            std::filesystem::file_time_type::rep time = std::filesystem::last_write_time(path).time_since_epoch().count();
            if (remove_files) {
                if (dry_run) {
                    fmt::print("rm -f {}\n", &ps[trim.length()]);
                }
                else {
                    try {
                        std::filesystem::remove(path);
                    }
                    catch (std::exception & e) {
                        fmt::print("failed to remove path: {}\n", &ps[trim.length()]);
                    }
                }
            }
            FileInfo file_info;
            file_info.first = path;
            file_info.second = file_size;
            file_info.third = perms;
            file_info.forth = type;
            file_info.fifth = std::move(file_chunks);
            file_info.sixth = time;
            bird_is_the_word_f.emplace_back(std::move(file_info));
        }
        else if (type == std::filesystem::file_type::symlink) {
            if (remove_files) {
                if (dry_run) {
                    auto paths = path.string();
                    fmt::print("rm -f {}\n", &paths[trim.length()]);
                }
                else {
                    try {
                        std::filesystem::remove(path);
                    }
                    catch (std::exception& e) {
                        auto paths = path.string();
                        fmt::print("failed to remove path: {}\n", &paths[trim.length()]);
                    }
                }
            }
            bird_is_the_word_s.emplace_back(std::pair<std::filesystem::path, std::filesystem::file_type>(path, type));
        }
        else {
            auto s = path.string();
            fmt::print("unknown type: {}\n", &s[trim.length()]);
            unknowns++;
        }
        return 0;
    }

    void recordPathDirectory(const std::filesystem::path& path, const std::filesystem::file_type& type, const std::filesystem::file_time_type::rep & dir_time, const std::filesystem::perms& perms) {
        auto s = path.string();
        auto ps = permsStr(perms);
        if (type == std::filesystem::file_type::directory) {
            const char* dir = &s[trim.length()];
            const char* dir_perms = ps.c_str();
            fmt::print("recording directory: d{} {: >8}   {}\n", dir_perms, 0, dir);
            w.write_string(&s[trim.length()]);
            w.write_string(dir_perms);
            w.write_u64(dir_time);
        }
        else {
            fmt::print("unknown type: {}\n", &s[trim.length()]);
        }
    }

    void recordPathFile(const FileInfo & fileInfo, const size_t& mfc) {
        auto s = fileInfo.first.string();
        auto ps = permsStr(fileInfo.third);
        if (fileInfo.forth == std::filesystem::file_type::regular) {
            const char* file = &s[trim.length()];
            const char* file_perms = ps.c_str();
            uint64_t file_chunks = fileInfo.fifth.size();

            fmt::print("recording file:       {} {: >8}   ({: >{}} chunks)   {}\n", file_perms, fileInfo.second, file_chunks, mfc, file);

            w.write_string(file);
            w.write_string(file_perms);
            w.write_u64(fileInfo.second);
            w.write_u64(fileInfo.sixth);
            w.write_u64(file_chunks);
            for (const ChunkInfo& chunk : fileInfo.fifth) {
                w.write_u64(chunk.split);
                w.write_u64(chunk.offset);
                w.write_u64(chunk.length);
            }
        }
        else {
            fmt::print("unknown type: {}\n", &s[trim.length()]);
        }
    }

    void recordPathSymlink(const std::filesystem::path& path, const std::filesystem::file_type& type) {
        auto s = path.string();
        if (type == std::filesystem::file_type::symlink) {
            const char* symlink = &s[trim.length()];
            auto sd = std::filesystem::read_symlink(path);
            auto sds = sd.string();
            const char* symlink_dest = sds.c_str();

            fmt::print("recording symlink:    {: >9} {: >8}   {} -> {}\n", "", 0, symlink, symlink_dest);

            w.write_string(symlink);
            w.write_string(symlink_dest);
        }
        else {
            fmt::print("unknown type: {}\n", &s[trim.length()]);
        }
    }

    int recordPath(const std::filesystem::path& path, const std::filesystem::file_status& status) {
        return recordPath_(path, status.type(), status.permissions());
    }

    int record(const char* path) {
        if (path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':' && path[2] == '/' && path[3] == '\0') {
            char x[5];
            x[0] = path[0];
            x[1] = ':';
            x[2] = '\\';
            x[3] = '\\';
            x[4] = '\0';
            return record(x);
        }
        std::filesystem::path p = std::filesystem::path(path);
        if (!std::filesystem::exists(p)) {
            fmt::print("item does not exist: {}\n", path);
            return 1;
        }

        if (std::filesystem::is_directory(p)) {
            auto split_map_name = fmt::format("{}split.map", SPLIT_PREFIX);
            w.create(split_map_name.c_str());
            w.write_string("BIN_WRITR_MGK");
            trim = path;
            if (trim[trim.length()] != '/') {
                trim += "/";
            } else {
                trim += "/";
            }
            fmt::print("entering directory: {}\n", path);
            std::filesystem::recursive_directory_iterator begin = std::filesystem::recursive_directory_iterator(p);
            std::filesystem::recursive_directory_iterator end;
            for (; begin != end; begin++) {
                auto & fpath = *begin;
                if (fpath.exists()) {
                    if (recordPath(fpath.path(), fpath.status()) == 1) {
                        _close();
                        return 1;
                    }
                }
                else {
                    fmt::print("item does not exist: {}\n", fpath.path());
                }
            }
            _close();
        } else if (std::filesystem::is_regular_file(p)) {
            auto split_map_name = fmt::format("{}split.map", SPLIT_PREFIX);
            w.create(split_map_name.c_str());
            w.write_string("BIN_WRITR_MGK");
            {
                std::filesystem::path copy = p;
                trim = copy.remove_filename().string();
            }
            fmt::print("entering directory: {}\n", trim);
            if (recordPath(p, std::filesystem::status(p)) == 1) {
                _close();
                return 1;
            }
            _close();
        }
        else if (std::filesystem::is_symlink(p)) {
            auto split_map_name = fmt::format("{}split.map", SPLIT_PREFIX);
            w.create(split_map_name.c_str());
            w.write_string("BIN_WRITR_MGK");
            {
                std::filesystem::path copy = p;
                trim = copy.remove_filename().string();
            }
            fmt::print("entering directory: {}\n", trim);
            if (recordPath(p, std::filesystem::status(p)) == 1) {
                _close();
                return 1;
            }
            _close();
        } else {
            fmt::print("unknown type: {}\n", &path[trim.length()]);
            w.close();
            return 1;
        }
        w.write_u64(SPLIT_SIZE);
        w.write_string(SPLIT_PREFIX.c_str());
        w.write_u64(bird_is_the_word_d.size());
        w.write_u64(bird_is_the_word_f.size());
        w.write_u64(total_chunk_count);
        w.write_u64(max_file_chunks);
        w.write_u64(split_number);
        w.write_string(max_path.c_str());
        auto mps = permsStr(max_perms);
        w.write_string(mps.c_str());
        w.write_u64(max_size);
        w.write_u64(max_chunk);
        size_t mfc = fmt::formatted_size("{}", max_file_chunks);
        w.write_u64(bird_is_the_word_s.size());
        if (remove_files) {
            auto copy = bird_is_the_word_d;
            std::reverse(copy.begin(), copy.end());
            for (auto& d : copy) {
                if (dry_run) {
                    auto paths = d.first.string();
                    fmt::print("rmdir {}\n", &paths[trim.length()]);
                }
                else {
                    try {
                        std::filesystem::remove(d.first);
                    }
                    catch (std::exception& e) {
                        auto paths = d.first.string();
                        fmt::print("failed to remove path: {}\n", &paths[trim.length()]);
                    }
                }
            }
        }
        for (auto& d : bird_is_the_word_d) {
            recordPathDirectory(d.first, d.second.second.first, d.second.second.second, d.second.first);
        }
        for (auto& f : bird_is_the_word_f) {
            recordPathFile(f, mfc);
        }
        for (auto& s : bird_is_the_word_s) {
            recordPathSymlink(s.first, s.second);
        }
        w.close();
        fmt::print("directories recorded: {}\n", bird_is_the_word_d.size());
        fmt::print("files recorded:       {}\n", bird_is_the_word_f.size());
        fmt::print("chunks recorded:      {}\n", total_chunk_count);
        fmt::print("split files recorded: {}\n", split_number);
        fmt::print("symlinks recorded:    {}\n", bird_is_the_word_s.size());
        fmt::print("unknown types:        {}\n", unknowns);
        fmt::print("total size of {: >{}} files:  {: >{}} bytes\n", bird_is_the_word_f.size(), fmt::formatted_size("{}", std::max(bird_is_the_word_f.size(), total_chunk_count)), total, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("total size of {: >{}} chunks: {: >{}} bytes\n", total_chunk_count, fmt::formatted_size("{}", std::max(bird_is_the_word_f.size(), total_chunk_count)), totalc, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("largest file chunk:   {} {: >8}   ({: >{}} chunks)   {}\n", mps, max_size, max_chunk, mfc, max_path);
        return 0;
    }

    int playback(const char * path, bool join_files, bool list_chunks) {
        if (join_files) {
            if (std::filesystem::exists(out_directory)) {
                if (!std::filesystem::is_directory(out_directory)) {
                    fmt::print("cannot output to a non-directory: {}\n", out_directory);
                    return 1;
                }
                std::filesystem::directory_iterator begin = std::filesystem::directory_iterator(out_directory);
                std::filesystem::directory_iterator end;
                for (; begin != end; begin++) {
                    auto& fpath = *begin;
                    if (fpath.exists()) {
                        fmt::print("cannot output to a non-empty directory: {}\n", out_directory);
                        return 1;
                    }
                }
            }
            else {
                if (dry_run) {
                    fmt::print("mkdir {}\n", out_directory);
                }
                else {
                    std::filesystem::create_directory(out_directory);
                }
            }
        }

        std::filesystem::path p = std::filesystem::path(path);
        if (!std::filesystem::exists(p)) {
            fmt::print("item does not exist: {}\n", path);
            return 1;
        }
        auto parent = std::filesystem::canonical(std::filesystem::absolute(path));
        if (!parent.has_parent_path()) {
            fmt::print("cannot obtain parent directory of item: {}\n", parent);
            return 1;
        }
        parent = parent.parent_path();
        r.open(path);
        const char * str = r.read_string();
        if (strcmp(str, "BIN_WRITR_MGK") != 0) {
            std::string e = "invalid magic: ";
            e += str;
            fmt::print("{}\n", e);
            free((void*)str);
            r.close();
            return 1;
        }
        free((void*)str);
        SPLIT_SIZE = r.read_u64();
        const char* SPLIT_PREFIX = r.read_string();
        uint64_t dirs = r.read_u64();
        uint64_t files = r.read_u64();
        uint64_t chunks = r.read_u64();
        uint64_t max_file_chunks = r.read_u64();
        uint64_t split_number = r.read_u64();
        size_t mfc = fmt::formatted_size("{}", max_file_chunks);
        const char* max_path = r.read_string();
        const char* max_perms = r.read_string();
        uintmax_t max_size = r.read_u64();
        uintmax_t max_chunk = r.read_u64();
        uint64_t symlinks = r.read_u64();
        fmt::print("reading {} directories\n", dirs);
        std::vector<std::pair<const char*, std::pair<const char*, std::filesystem::file_time_type::rep>>> dirs_vec;
        while (dirs != 0) {
            dirs--;

            const char* dir = r.read_string();
            const char* dir_perms = r.read_string();
            std::filesystem::file_time_type::rep t = (std::filesystem::file_time_type::rep)r.read_u64();

            if (join_files) {
                if (dry_run) {
                    fmt::print("mkdir {: >9} {}/{}\n", "", out_directory, dir);
                }
                else {
                    if (!std::filesystem::create_directory(out_directory + "/" + dir)) {
                        fmt::print("failed to create directory: {}/{}\n", out_directory, dir);
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return 1;
                    }
                }
                dirs_vec.emplace_back(std::pair<const char*, std::pair<const char*, std::filesystem::file_time_type::rep>>(dir, std::pair<const char*, std::filesystem::file_time_type::rep>(dir_perms, t)));
            }
            else {
                fmt::print("d{} {} {}\n", dir_perms, 0, dir);
                free((void*)dir);
                free((void*)dir_perms);
            }
        }
        fmt::print("reading {} files with a total of {} chunks\n", files, chunks);
        uintmax_t total = 0;
        uintmax_t totalc = 0;
        uintmax_t current_split = 0;
        bool split_open = false;
        FILE* current_split_file = nullptr;

        for (uintmax_t i = 0; i < files; i++) {
            const char* file = r.read_string();
            const char* file_perms = r.read_string();
            uint64_t file_size = r.read_u64();
            std::filesystem::file_time_type::rep file_time = (std::filesystem::file_time_type::rep)r.read_u64();
            uint64_t file_chunks = r.read_u64();
            total += file_size;

            if (join_files) {
                if (dry_run) {
                    fmt::print("fopen({}/{}, \"wb\")\n", out_directory, file);
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        if (split != current_split) {
                            if (split_open) {
                                fmt::print("fclose({}/{}split.{})\n", parent, SPLIT_PREFIX, current_split);
                                split_open = false;
                            }
                            if (remove_files) {
                                fmt::print("rm -f {}/{}split.{}\n", parent, SPLIT_PREFIX, current_split);
                            }
                            current_split = split;
                        }
                        if (!split_open) {
                            fmt::print("fopen({}/{}split.{}, \"rb\")\n", parent, SPLIT_PREFIX, split);
                            fmt::print("fseek({}/{}split.{}, 0)\n", parent, SPLIT_PREFIX, split);
                            split_open = true;
                        }
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        fmt::print("fread({}/{}split.{}, buf, {})\n", parent, SPLIT_PREFIX, split, length);
                        fmt::print("fwrite({}/{}, buf, {})\n", out_directory, file, length);
                        totalc += length;
                    }
                    fmt::print("fflush({}/{})\n", out_directory, file);
                    fmt::print("fclose({}/{})\n", out_directory, file);
                    fmt::print("chmod {: >9} {}/{}\n", file_perms, out_directory, file);
                }
                else {
                    std::string out_f = out_directory + "/" + file;
                    FILE * f = fopen(out_f.c_str(), "wb");
                    if (f == nullptr) {
                        fmt::print("failed to create file: {}\n", out_f);
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return 1;
                    }
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        if (split != current_split) {
                            if (split_open) {
                                fclose(current_split_file);
                                current_split_file = nullptr;
                                split_open = false;
                            }
                            if (remove_files) {
                                auto path_to_remove = fmt::format("{}/{}split.{}", parent, SPLIT_PREFIX, current_split);
                                try {
                                    std::filesystem::remove(path_to_remove);
                                }
                                catch (std::exception& e) {
                                    fmt::print("failed to remove path: {}\n", path_to_remove);
                                }
                            }
                            current_split = split;
                        }
                        if (!split_open) {
                            auto in_s = fmt::format("{}/{}split.{}", parent, SPLIT_PREFIX, split);
                            current_split_file = fopen(in_s.c_str(), "rb");
                            if (current_split_file == nullptr) {
                                fmt::print("failed to open file: {}\n", in_s);
                                fclose(f);
                                r.close();
                                free((void*)SPLIT_PREFIX);
                                free((void*)max_path);
                                free((void*)max_perms);
                                return 1;
                            }
                            fseek(current_split_file, 0, SEEK_SET);
                            split_open = true;
                        }
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        void* buffer = malloc(length);
                        if (buffer == nullptr) {
                            fmt::print("failed to allocate {} bytes\n", length);
                            fclose(current_split_file);
                            fclose(f);
                            r.close();
                            free((void*)SPLIT_PREFIX);
                            free((void*)max_path);
                            free((void*)max_perms);
                            return 1;
                        }
                        fread(buffer, 1, length, current_split_file);
                        fwrite(buffer, 1, length, f);
                        free(buffer);
                        totalc += length;
                    }
                    fflush(f);
                    fclose(f);
                    f = nullptr;
                    std::filesystem::permissions(out_f, strPerms(file_perms));
                    std::filesystem::last_write_time(out_f, std::filesystem::file_time_type(std::filesystem::file_time_type::duration(file_time)));
                }
            }
            else {
                if (list_chunks) {
                    fmt::print(" {} {: >8}   ({: >{}} chunks)   {}\n", file_perms, file_size, file_chunks, mfc, file);
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        fmt::print("   [chunk] {}split.{} [{: >{}}-{: >{}}]\n", SPLIT_PREFIX, split, offset, fmt::formatted_size("{}", SPLIT_SIZE), offset + length, fmt::formatted_size("{}", SPLIT_SIZE));
                        totalc += length;
                    }
                }
                else {
                    fmt::print(" {} {: >8}   {}\n", file_perms, file_size, file);
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        totalc += length;
                    }
                }
            }

            free((void*)file);
            free((void*)file_perms);
        }
        if (join_files) {
            if (split_open) {
                if (dry_run) {
                    fmt::print("fclose({}/{}split.{})\n", parent, SPLIT_PREFIX, current_split);
                    if (remove_files) {
                        fmt::print("rm -f {}/{}split.{}\n", parent, SPLIT_PREFIX, current_split);
                    }
                }
                else {
                    fclose(current_split_file);
                    if (remove_files) {
                        auto path_to_remove = fmt::format("{}/{}split.{}", parent, SPLIT_PREFIX, current_split);
                        try {
                            std::filesystem::remove(path_to_remove);
                        }
                        catch (std::exception& e) {
                            fmt::print("failed to remove path: {}\n", path_to_remove);
                        }
                    }
                    current_split_file = nullptr;
                }
                split_open = false;
            }
        }
        fmt::print("total size of {: >{}} files:  {: >{}} bytes\n", files, fmt::formatted_size("{}", std::max(files, chunks)), total, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("total size of {: >{}} chunks: {: >{}} bytes\n", chunks, fmt::formatted_size("{}", std::max(files, chunks)), totalc, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("largest file chunk:   {} {: >8}   ({: >{}} chunks)   {}\n", max_perms, max_size, max_chunk, mfc, max_path);
        fmt::print("reading {} symlinks\n", symlinks);
        while (symlinks != 0) {
            symlinks--;

            const char* symlink = r.read_string();
            const char* symlink_dest = r.read_string();

            if (join_files) {
                if (dry_run) {
                    fmt::print("ln -s {} {}/{}\n", symlink_dest, out_directory, symlink);
                }
                else {
                    // attempt to handle the case where some systems require a symlink to
                    // specify that its target is a directory or a file
                    //
                    std::filesystem::path sp = p = out_directory + symlink;
                    auto parent = sp;
                    if (!parent.has_parent_path()) {
                        fmt::print("cannot obtain parent directory of item: {}\n", parent);
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return 1;
                    }
                    parent = parent.parent_path();
                    std::filesystem::path t = parent.string() + "/" + symlink_dest;
                    std::filesystem::path resolved_t = std::filesystem::weakly_canonical(symlink_dest);
                    if (std::filesystem::exists(resolved_t)) {
                        if (std::filesystem::is_directory(resolved_t)) {
                            std::filesystem::create_directory_symlink(symlink_dest, sp);
                        }
                        else {
                            std::filesystem::create_symlink(symlink_dest, sp);
                        }
                    }
                    else {
                        std::filesystem::create_symlink(symlink_dest, sp);
                    }
                }
            }
            else {
                fmt::print(" {: >9} {: >8}   {} -> {}\n", "", 0, symlink, symlink_dest);
            }
            free((void*)symlink);
            free((void*)symlink_dest);
        }
        if (join_files) {
            for (auto& dirs : dirs_vec) {
                if (dry_run) {
                    fmt::print("chmod {: >9} {}/{}\n", dirs.second.first, out_directory, dirs.first);
                }
                else {
                    std::filesystem::permissions(out_directory + "/" + dirs.first, strPerms(dirs.second.first));
                    std::filesystem::last_write_time(out_directory + "/" + dirs.first, std::filesystem::file_time_type(std::filesystem::file_time_type::duration(dirs.second.second)));
                }
                free((void*)dirs.first);
                free((void*)dirs.second.first);
            }
        }
        r.close();
        free((void*)SPLIT_PREFIX);
        free((void*)max_path);
        free((void*)max_perms);
        return 0;
    }
};

void split_usage() {
    fmt::print("\n--split  [-n] [-r] [--size <split_size>] [--name <name>] <dir/file>\n");
    fmt::print("         info\n");
    fmt::print("                 split a directory/file into fixed size chunks\n");
    fmt::print("                 symlinks WILL NOT be followed\n");
    fmt::print("                 the split files will be created in the current working directory\n");
    fmt::print("         -n\n");
    fmt::print("                 form the split map only, does not store any content\n");
    fmt::print("         -r\n");
    fmt::print("                 remove each directory/file upon being stored\n");
    fmt::print("                 if -n is given, no directories/files will be removed\n");
    fmt::print("         --size\n");
    fmt::print("                 specifies the split size, the default is 4 MB\n");
    fmt::print("                 if a value of zero is specified then the default of 4 MB is used\n");
    fmt::print("                 if this options is not specified then the default of 4 MB is used\n");
    fmt::print("         --name\n");
    fmt::print("                 specifies the prefix to be added to split.* files\n");
    fmt::print("                 if this options is not specified then an empty prefix is used\n");
    fmt::print("         <dir/file>\n");
    fmt::print("                 directory/file to split\n");
}

void join_usage() {
    fmt::print("\n--join   [-n] [-r] [prefix.]split.map --out <out_dir>\n");
    fmt::print("         info\n");
    fmt::print("                 join a split map to restore a directory/file\n");
    fmt::print("         -n\n");
    fmt::print("                 list what would have been done, do not create/modify anything\n");
    fmt::print("         -r\n");
    fmt::print("                 remove each *split.* upon its contents being extracted\n");
    fmt::print("                 if -n is given, no *split.* files will be removed\n");
    fmt::print("         [prefix.]\n");
    fmt::print("                 an optional prefix for the split map\n");
    fmt::print("         --out\n");
    fmt::print("                 the directory to restore a directory/file into\n");
    fmt::print("                 defaults to the current directory\n");
}

void ls_usage() {
    fmt::print("\n--ls     [prefix.]split.map\n");
    fmt::print("         info\n");
    fmt::print("                 list the contents of a split map\n");
    fmt::print("         [prefix.]\n");
    fmt::print("                 an optional prefix for the split map\n");
    fmt::print("         <out_dir>\n");
    fmt::print("                 the directory to restore a directory/file into\n");
    fmt::print("                 defaults to the current directory\n");
}

void usage() {
    split_usage();
    join_usage();
    ls_usage();
}

int main(int argc, const char** argv) {
    if (argc == 0) {
        usage();
        return 1;
    }
    next_is_help = true;
    while (true) {
        argc--;
        argv++;
        if (argc == 0 && next_is_help) {
            if (is_ls) {
                ls_usage();
            }
            else if (is_split) {
                split_usage();
            }
            else if (is_join) {
                join_usage();
            }
            else {
                usage();
            }
            return 1;
        }
        next_is_help = false;
        if (command_selected) {
            if (is_ls) {
                // the next argument must be a dir/file
                PathRecorder p;
                return p.playback(argv[0], false, false);
            }
            else if (is_split) {
                if (argc == 0) {
                    // all arguments must have been met
                    if (!std::filesystem::exists(file)) {
                        fmt::print("item does not exist: {}\n", file);
                        return 1;
                    }
                    if (remove_files) {
                        auto current = std::filesystem::canonical(std::filesystem::current_path());
                        auto root = std::filesystem::canonical(current.root_path());
                        auto target = std::filesystem::canonical(std::filesystem::absolute(file));
                        if (current.compare(target) == 0) {
                            fmt::print("cannot remove the current working directory\n");
                            return 1;
                        }
                        else if (current.compare(target) == 1) {
                            while (current.has_parent_path() && current != root) {
                                if (current.compare(target) == 0) {
                                    fmt::print("cannot remove a parent directory\n");
                                    return 1;
                                }
                                current = current.parent_path();
                            }
                        }
                    }
                    if (SPLIT_SIZE == 0) {
                        SPLIT_SIZE = 4096 * 1024; // 4 MB split size
                    }
                    fmt::print("using split size of {} bytes\n", SPLIT_SIZE);
                    PathRecorder p;
                    return p.record(file.c_str());
                }
                if (next_is_name) {
                    SPLIT_PREFIX = std::string(argv[0]);
                    if (SPLIT_PREFIX.length() != 0) {
                        SPLIT_PREFIX += ".";
                    }
                    next_is_name = false;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (next_is_size) {
                    SPLIT_SIZE = (uintmax_t)atoll(argv[0]);
                    next_is_size = false;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "-n") == 0) {
                    dry_run = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "-r") == 0) {
                    remove_files = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "--size") == 0) {
                    next_is_size = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "--name") == 0) {
                    next_is_name = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                file = std::string(argv[0]);
                next_is_help = file.length() == 0;
                continue;
            }
            else if (is_join) {
                if (argc == 0) {
                    // all arguments must have been met
                    PathRecorder p;
                    return p.playback(file.c_str(), true, true);
                }
                if (next_is_name) {
                    out_directory = std::string(argv[0]);
                    if (out_directory.length() == 0) {
                        out_directory = ".";
                    }
                    next_is_name = false;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "-n") == 0) {
                    dry_run = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "-r") == 0) {
                    remove_files = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                if (strcmp(argv[0], "--out") == 0) {
                    next_is_name = true;
                    next_is_help = file.length() == 0;
                    continue;
                }
                file = std::string(argv[0]);
                next_is_help = file.length() == 0;
                continue;
            }
        }
        else {
            next_is_help = true;
            if (strcmp(argv[0], "--ls") == 0) {
                is_ls = true;
                command_selected = true;
                continue;
            }
            if (strcmp(argv[0], "--split") == 0) {
                is_split = true;
                command_selected = true;
                continue;
            }
            if (strcmp(argv[0], "--join") == 0) {
                is_join = true;
                command_selected = true;
                continue;
            }
        }
    }
    return 0;
}
