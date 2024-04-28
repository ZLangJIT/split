#include <sstream>
#include <fstream>

#include <memory>
#include <cstring>

#include <sys/stat.h>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <fmt/printf.h>
#include <curl/curl.h>
#include <tmpfile/tmpfile.h>

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
bool verbose_files = false;
bool next_is_size = false;
bool next_is_name = false;
bool next_is_help = true;
int  next_ret = -1; // zero if -h or --help was explicitly specified
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
            if (bin == nullptr) {
                auto se = errno;
                std::string e = fmt::format("failed to create item {}\nerrno: -{} ({})\n", name, se, fmt::system_error(se, ""));
                throw std::runtime_error(e);
            }
            fseek(bin, 0, SEEK_SET);
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
        if (value == nullptr) {
            value = "";
        }

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
                auto se = errno;
                std::string e = fmt::format("failed to open item {}\nerrno: -{} ({})\n", name, se, fmt::system_error(se, ""));
                throw std::runtime_error(e);
            }
            fseek(bin, 0, SEEK_SET);
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

bool get_stats(const std::filesystem::path& path, struct stat& st) {
    auto ps = std::filesystem::absolute(path).string();
    auto s = ps.c_str();;
    if (lstat(s, &st) == -1) {
        auto se = errno;
        fmt::print("failed to lstat {}\nerrno: -{} ({})\n", s, se, fmt::system_error(se, ""));
        return false;
    }
    return true;
}

bool path_exists(const std::filesystem::path& path) {
    struct stat st;
    auto ps = std::filesystem::absolute(path).string();
    auto s = ps.c_str();;
    if (lstat(s, &st) == -1) {
        auto se = errno;
        if (se == ENOENT) return false;
        fmt::print("failed to lstat {}\nerrno: -{} ({})\n", s, se, fmt::system_error(se, ""));
        return false;
    }
    return true;
}

inline bool is_directory(const struct stat& st) {
    return (st.st_mode & S_IFMT) == S_IFDIR;
}

inline bool is_reg(const struct stat& st) {
    return (st.st_mode & S_IFMT) == S_IFREG;
}

inline bool is_symlink(const struct stat& st) {
    return (st.st_mode & S_IFMT) == S_IFLNK;
}

bool is_symlink(const std::filesystem::path& path) {
    struct stat st;
    if (!get_stats(path, st)) return false;
    return is_symlink(st);
}

std::string permissions_to_string(const struct stat& st) {
    char s[11];
    s[0] = is_directory(st) ? 'd' : is_symlink(st) ? 'l' : '-';
    s[1] = (st.st_mode & S_IRUSR) == S_IRUSR ? 'r' : '-';
    s[2] = (st.st_mode & S_IWUSR) == S_IWUSR ? 'w' : '-';
    s[3] = (st.st_mode & S_IXUSR) == S_IXUSR ? 'x' : '-';
    s[4] = (st.st_mode & S_IRGRP) == S_IRGRP ? 'r' : '-';
    s[5] = (st.st_mode & S_IWGRP) == S_IWGRP ? 'w' : '-';
    s[6] = (st.st_mode & S_IXGRP) == S_IXGRP ? 'x' : '-';
    s[7] = (st.st_mode & S_IROTH) == S_IROTH ? 'r' : '-';
    s[8] = (st.st_mode & S_IWOTH) == S_IWOTH ? 'w' : '-';
    s[9] = (st.st_mode & S_IXOTH) == S_IXOTH ? 'x' : '-';
    s[10] = '\0';
    return s;
}

struct stat string_to_permissions(const char * s) {
    struct stat st;
    st.st_mode |= s[0] == 'd' ? S_IFDIR : s[0] == 'l' ? S_IFLNK : S_IFREG;
    st.st_mode |= s[1] == 'r' ? S_IRUSR : 0;
    st.st_mode |= s[2] == 'w' ? S_IWUSR : 0;
    st.st_mode |= s[3] == 'x' ? S_IXUSR : 0;
    st.st_mode |= s[4] == 'r' ? S_IRGRP : 0;
    st.st_mode |= s[5] == 'w' ? S_IWGRP : 0;
    st.st_mode |= s[6] == 'x' ? S_IXGRP : 0;
    st.st_mode |= s[7] == 'r' ? S_IROTH : 0;
    st.st_mode |= s[8] == 'w' ? S_IWOTH : 0;
    st.st_mode |= s[9] == 'x' ? S_IXOTH : 0;
    return st;
}

std::filesystem::perms permissions_to_filesystem(const struct stat& st) {
    std::filesystem::perms s;
    s |= (st.st_mode & S_IRUSR) == S_IRUSR ? std::filesystem::perms::owner_read : std::filesystem::perms::none;
    s |= (st.st_mode & S_IWUSR) == S_IWUSR ? std::filesystem::perms::owner_write : std::filesystem::perms::none;
    s |= (st.st_mode & S_IXUSR) == S_IXUSR ? std::filesystem::perms::owner_exec : std::filesystem::perms::none;
    s |= (st.st_mode & S_IRGRP) == S_IRGRP ? std::filesystem::perms::group_read : std::filesystem::perms::none;
    s |= (st.st_mode & S_IWGRP) == S_IWGRP ? std::filesystem::perms::group_write : std::filesystem::perms::none;
    s |= (st.st_mode & S_IXGRP) == S_IXGRP ? std::filesystem::perms::group_exec : std::filesystem::perms::none;
    s |= (st.st_mode & S_IROTH) == S_IROTH ? std::filesystem::perms::others_read : std::filesystem::perms::none;
    s |= (st.st_mode & S_IWOTH) == S_IWOTH ? std::filesystem::perms::others_write : std::filesystem::perms::none;
    s |= (st.st_mode & S_IXOTH) == S_IXOTH ? std::filesystem::perms::others_exec : std::filesystem::perms::none;
    return s;
}

std::string get_symlink_dest(const std::filesystem::path& path, const struct stat & st) {
    if ((st.st_mode & S_IFMT) == S_IFLNK) {
        auto path_size = st.st_size + 1;
        if (path_size == 1) return "";
        auto s = path.string();
        char* buf = (char*)malloc(path_size * sizeof(char));
        if (readlink(s.c_str(), buf, path_size) == -1) {
            auto se = errno;
            fmt::print("failed to read content of symbolic link {}\nerrno: -{} ({})\n", s, se, fmt::system_error(se, ""));
            free(buf);
            return "";
        }
        buf[path_size - 1] = '\0';
        std::string pstr = buf;
        free(buf);
        return pstr;
    }
    else {
        return "";
    }
}

std::string get_symlink_dest(const std::filesystem::path& path) {
    struct stat st;
    if (!get_stats(path, st)) return "";
    return get_symlink_dest(path, st);
}

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

    struct DirInfo {
        std::filesystem::path path;
        std::string perms;
        std::filesystem::file_time_type::rep write_time;
    };
    struct FileInfo {
        std::filesystem::path path;
        std::string perms;
        std::filesystem::file_time_type::rep write_time;
        uintmax_t file_size;
        std::vector<ChunkInfo> file_chunks;
    };
    struct SymlinkInfo {
        std::filesystem::path path;
        std::filesystem::path dest;
    };

    std::vector<DirInfo> bird_is_the_word_d = {};
    std::vector<FileInfo> bird_is_the_word_f = {};
    std::vector<SymlinkInfo> bird_is_the_word_s = {};

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
    uint64_t max_perms = 0;
    std::string max_perms_str = {};
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
                    return -1;
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

    int recordPath(const std::filesystem::path& path) {
        struct stat st;
        if (!get_stats(path, st)) {
            return -1;
        }
        if (is_directory(st)) {
            if (verbose_files) fmt::print("packing directory: {}\n", path);
            DirInfo di;
            di.path = path;
            di.perms = permissions_to_string(st);
            di.write_time = std::filesystem::last_write_time(path).time_since_epoch().count();
            bird_is_the_word_d.emplace_back(di);
        }
        else if (is_reg(st)) {
            if (verbose_files) fmt::print("packing file: {}\n", path);
            std::vector<ChunkInfo> file_chunks;
            uintmax_t s = std::filesystem::file_size(path);
            total += s;
            auto ps = path.string();
            if (_open() == -1) return -1;
            FILE* f;
            if (dry_run) {
                fmt::print("fopen()\n");
            }
            else {
                f = fopen(ps.c_str(), "rb");
                if (f == nullptr) {
                    fmt::print("failed to open file: {}\n", ps);
                    _close();
                    return -1;
                }
            }
            while (s != 0) {
                ChunkInfo chunk;
                // see how much space we have available
                uintmax_t avail = chunk_size - current_chunk_size;
                if (avail == 0) {
                    // we have 0 bytes available, request a new chunk
                    _close();
                    if (_open() == -1) {
                        if (dry_run) {
                            fmt::print("fclose()\n");
                        }
                        else {
                            fclose(f);
                            f = nullptr;
                        }
                        return -1;
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
                        throw std::bad_alloc();
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
            uint64_t current_file_chunks = file_chunks.size();
            uint64_t current_file_size = std::filesystem::file_size(path);
            if (current_file_size >= max_size) {
                max_path = std::string(&ps[trim.length()]);
                max_size = current_file_size;
                max_chunk = current_file_chunks;
                max_perms = st.st_mode;
                max_perms_str = permissions_to_string(st);
            }
            auto file_time = std::filesystem::last_write_time(path).time_since_epoch().count();
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
            file_info.path = path;
            file_info.perms = permissions_to_string(st);
            file_info.write_time = file_time;
            file_info.file_size = current_file_size;
            file_info.file_chunks = std::move(file_chunks);
            bird_is_the_word_f.emplace_back(std::move(file_info));
        }
        else if (is_symlink(st)) {
            if (verbose_files) fmt::print("packing symlink: {}\n", path);
            auto dest = get_symlink_dest(path);
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
            SymlinkInfo si;
            si.path = path;
            si.dest = dest;
            bird_is_the_word_s.emplace_back(si);
        }
        else {
            auto s = path.string();
            fmt::print("unknown type: {}\n", &s[trim.length()]);
            unknowns++;
        }
        return 0;
    }

    void recordPathDirectory(const DirInfo & dirInfo, const size_t& mfc) {
        auto s = dirInfo.path.string();
        const char* dir = &s[trim.length()];
        if (verbose_files) fmt::print("recording directory: {} {: >8}   ({: >{}} chunks)   {}\n", dirInfo.perms, 0, 0, mfc, dir);
        w.write_string(&s[trim.length()]);
        w.write_string(dirInfo.perms.c_str());
        w.write_u64(dirInfo.write_time);
    }

    void recordPathFile(const FileInfo & fileInfo, const size_t& mfc) {
        auto s = fileInfo.path.string();
        const char* file = &s[trim.length()];
        uint64_t file_chunks = fileInfo.file_chunks.size();

        if (verbose_files) fmt::print("recording file:      {} {: >8}   ({: >{}} chunks)   {}\n", fileInfo.perms, fileInfo.file_size, file_chunks, mfc, file);

        w.write_string(file);
        w.write_string(fileInfo.perms.c_str());
        w.write_u64(fileInfo.write_time);
        w.write_u64(fileInfo.file_size);
        w.write_u64(file_chunks);
        for (const ChunkInfo& chunk : fileInfo.file_chunks) {
            w.write_u64(chunk.split);
            w.write_u64(chunk.offset);
            w.write_u64(chunk.length);
        }
    }

    void recordPathSymlink(const SymlinkInfo& symlinkInfo, const size_t& mfc) {
        auto s = symlinkInfo.path.string();
        const char* symlink = &s[trim.length()];

        if (verbose_files) fmt::print("recording symlink:   {} {: >8}   ({: >{}} chunks)   {} -> {}\n", "lrwxrwxrwx", 0, 0, mfc, symlink, symlinkInfo.dest);

        w.write_string(symlink);
        w.write_string(symlinkInfo.dest.c_str());
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

        if (::is_symlink(p)) {
            auto split_map_name = fmt::format("{}split.map", SPLIT_PREFIX);
            w.create(split_map_name.c_str());
            w.write_string("BIN_WRITR_MGK");
            {
                std::filesystem::path copy = p;
                trim = copy.remove_filename().string();
            }
            fmt::print("entering directory: {}\n", trim);
            if (recordPath(p) == -1) {
                _close();
                return -1;
            }
            _close();
        } else if (std::filesystem::is_directory(p)) {
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
                if (path_exists(fpath)) {
                    if (recordPath(fpath.path()) == -1) {
                        _close();
                        return -1;
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
            if (recordPath(p) == -1) {
                _close();
                return -1;
            }
            _close();
        }
        else {
            fmt::print("unknown type: {}\n", &path[trim.length()]);
            w.close();
            return -1;
        }
        w.write_u64(SPLIT_SIZE);
        w.write_string(SPLIT_PREFIX.c_str());
        w.write_u64(bird_is_the_word_d.size());
        w.write_u64(bird_is_the_word_f.size());
        w.write_u64(total_chunk_count);
        w.write_u64(max_file_chunks);
        w.write_u64(split_number);
        w.write_string(max_path.c_str());
        w.write_string(max_perms_str.c_str());
        w.write_u64(max_size);
        w.write_u64(max_chunk);
        size_t mfc = fmt::formatted_size("{}", max_file_chunks);
        w.write_u64(bird_is_the_word_s.size());
        if (remove_files) {
            auto copy = bird_is_the_word_d;
            std::reverse(copy.begin(), copy.end());
            for (auto& d : copy) {
                if (dry_run) {
                    auto paths = d.path.string();
                    fmt::print("rmdir {}\n", &paths[trim.length()]);
                }
                else {
                    try {
                        std::filesystem::remove(d.path);
                    }
                    catch (std::exception& e) {
                        auto paths = d.path.string();
                        fmt::print("failed to remove path: {}\n", &paths[trim.length()]);
                    }
                }
            }
        }
        for (auto& d : bird_is_the_word_d) {
            recordPathDirectory(d, mfc);
        }
        for (auto& f : bird_is_the_word_f) {
            recordPathFile(f, mfc);
        }
        for (auto& s : bird_is_the_word_s) {
            recordPathSymlink(s, mfc);
        }
        w.close();
        fmt::print("split size:           {}\n", SPLIT_SIZE);
        fmt::print("split prefix:         {}\n", SPLIT_PREFIX);
        fmt::print("directories recorded: {}\n", bird_is_the_word_d.size());
        fmt::print("files recorded:       {}\n", bird_is_the_word_f.size());
        fmt::print("chunks recorded:      {}\n", total_chunk_count);
        fmt::print("split files recorded: {}\n", split_number+1);
        fmt::print("symlinks recorded:    {}\n", bird_is_the_word_s.size());
        fmt::print("unknown types:        {}\n", unknowns);
        fmt::print("total size of {: >{}} files:  {: >{}} bytes\n", bird_is_the_word_f.size(), fmt::formatted_size("{}", std::max(bird_is_the_word_f.size(), total_chunk_count)), total, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("total size of {: >{}} chunks: {: >{}} bytes\n", total_chunk_count, fmt::formatted_size("{}", std::max(bird_is_the_word_f.size(), total_chunk_count)), totalc, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("largest file: {: >{}}         {} {: >8}   ({: >{}} chunks)   {}\n", "", fmt::formatted_size("{}", std::max(bird_is_the_word_f.size(), total_chunk_count)), max_perms_str, max_size, max_chunk, mfc, max_path);
        return 0;
    }

    struct F {
        FILE * f;
        size_t size;
    };

    static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        F* file = (F*)userp;
        size_t w = fwrite(contents, size, nmemb, file->f);
        fflush(file->f);
        file->size += w;
        return w;
    }

    bool is_url(const char* url) {
        return
            strstr(url, "https://") == url ||
            strstr(url, "http://") == url ||
            strstr(url, "ftp://") == url ||
            strstr(url, "ftps://") == url;
    }

    int download_url_(char* url, TempFileFILE & tmp) {
        CURL* curl = nullptr;
        char* location = strdup(url);
        if (location == nullptr) {
            throw std::bad_alloc();
        }
        char* locationNew = location;
        long response_code = 0;
        curl = curl_easy_init();
        F f = { 0 };
        f.f = tmp.get_handle();
        if (curl) {
        LOC:
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L); // enable progress report
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // fail on error
            char errbuf[CURL_ERROR_SIZE] = { 0 };
            curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
#include "cacert.pem.h"
            struct curl_blob pem_blob;
            pem_blob.data = (void*)PEM.c_str();
            pem_blob.len = PEM.length();
            pem_blob.flags = CURL_BLOB_NOCOPY;
            curl_easy_setopt(curl, CURLOPT_CAINFO, nullptr);
            curl_easy_setopt(curl, CURLOPT_CAPATH, nullptr);
            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &pem_blob);
            curl_easy_setopt(curl, CURLOPT_URL, location);

            /* send all data to this function  */
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&f);

            /* some servers do not like requests that are made without a user-agent
               field, so we provide one */
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

            fflush(stdout);
            fflush(stderr);
            CURLcode res = curl_easy_perform(curl);
            fflush(stdout);
            fflush(stderr);

            if (res != CURLE_OK) {
                fmt::print("\ncurl_easy_perform() failed: {}\n{}\n", curl_easy_strerror(res), errbuf);
                curl_easy_cleanup(curl);
                free((void*)location);
                return -1;
            }

            res = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            if (res != CURLE_OK) {
                fmt::print("\ncurl_easy_getinfo(CURLINFO_RESPONSE_CODE) failed: {}\n{}\n", curl_easy_strerror(res), errbuf);
                curl_easy_cleanup(curl);
                free((void*)location);
                return -1;
            }

            if ((response_code / 100) == 3) {
                res = curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &locationNew);
                if (res != CURLE_OK) {
                    fmt::print("\ncurl_easy_getinfo(CURLINFO_REDIRECT_URL) failed: {}\n{}\n", curl_easy_strerror(res), errbuf);
                    curl_easy_cleanup(curl);
                    free((void*)location);
                    return -1;
                }
                free((void*)location);
                location = strdup(locationNew);
                if (location == nullptr) {
                    throw std::bad_alloc();
                }
                goto LOC;
            }
            free((void*)location);
            fmt::print("downloaded {} bytes -> {}\n", f.size, tmp.get_path());
            fflush(stdout);
            fflush(stderr);
        }
        else {
            fmt::print("failed to initialize curl\n");
            fflush(stdout);
            fflush(stderr);
            return -1;
        }
        fflush(stdout);
        fflush(stderr);
        return 0;
    }

    int download_url(const char * url, TempFileFILE & tmp) {
        if (!is_url(url)) {
            fmt::print("attempting to download a non-url\n");
            return -1;
        }
        fmt::print("executing curl_global_init() ...\n");
        CURLcode res = curl_global_init(CURL_GLOBAL_ALL);
        if (res != CURLE_OK) {
            fmt::print("curl_global_init() failed: {}\n", curl_easy_strerror(res));
            return -1;
        }
        fmt::print("executed curl_global_init() ...\n");
        char* p = strdup(url);
        int r = download_url_(p, tmp);
        free((void*)p);
        fmt::print("executing curl_global_cleanup() ...\n");
        curl_global_cleanup();
        fmt::print("executed curl_global_cleanup() ...\n");
        fflush(stdout);
        fflush(stderr);
        return r;
    }

    int playback_url(const char* url, bool join_files, bool list_chunks) {
        if (!join_files) {
            remove_files = true; // remove temporary downloaded temporary files if we are not joining them
        }
        if (join_files) {
            if (path_exists(out_directory)) {
                if (!dry_run) {
                    if (!std::filesystem::is_directory(out_directory)) {
                        fmt::print("cannot output to a non-directory: {}\n", out_directory);
                        return -1;
                    }
                    std::filesystem::directory_iterator begin = std::filesystem::directory_iterator(out_directory);
                    std::filesystem::directory_iterator end;
                    for (; begin != end; begin++) {
                        auto& fpath = *begin;
                        if (path_exists(fpath)) {
                            fmt::print("cannot output to a non-empty directory: {}\n", out_directory);
                            return -1;
                        }
                    }
                }
            }
            else {
                if (dry_run) {
                    fmt::print("mkdir {}\n", out_directory);
                }
                else {
                    fmt::print("creating output directory: {}\n", out_directory);
                    std::filesystem::create_directory(out_directory);
                }
            }
        }

        TempFileFILE tmp_split_map;
        tmp_split_map.construct(TempFile::TempDir(), fmt::format("split.map.", TEMP_FILE_OPEN_MODE_READ | TEMP_FILE_OPEN_MODE_WRITE | TEMP_FILE_OPEN_MODE_BINARY), !remove_files);
        const char* path = tmp_split_map.get_path().c_str();
        fmt::print("downloading item: {}\n", url);
        fmt::print("-> path: {}\n", path);
        if (download_url(url, tmp_split_map) == -1) {
            fmt::print("failed to download item: {}\n", url);
            return -1;
        }
        fmt::print("downloaded item: {}\n", url);
        fflush(stdout);
        fflush(stderr);
        fseek(tmp_split_map.get_handle(), 0, SEEK_SET);

        auto parent = std::filesystem::canonical(std::filesystem::absolute(path));
        if (!parent.has_parent_path()) {
            fmt::print("cannot obtain parent directory of item: {}\n", parent);
            return -1;
        }
        parent = parent.parent_path();
        r.open(path);
        const char* str = r.read_string();
        if (strcmp(str, "BIN_WRITR_MGK") != 0) {
            std::string e = "invalid magic: ";
            e += str;
            fmt::print("{}\n", e);
            free((void*)str);
            r.close();
            return -1;
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
                    if (verbose_files) fmt::print("unpacking directory: {}/{}\n", out_directory, dir);
                    if (!std::filesystem::create_directory(out_directory + "/" + dir)) {
                        fmt::print("failed to create directory: {}/{}\n", out_directory, dir);
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return -1;
                    }
                }
                dirs_vec.emplace_back(std::pair<const char*, std::pair<const char*, std::filesystem::file_time_type::rep>>(dir, std::pair<const char*, std::filesystem::file_time_type::rep>(dir_perms, t)));
            }
            else {
                fmt::print("{} {: >8}   ({: >{}} chunks)   {}\n", dir_perms, 0, 0, mfc, dir);
                free((void*)dir);
                free((void*)dir_perms);
            }
        }
        fmt::print("reading {} files with a total of {} split files consisting of {} chunks\n", files, split_number+1, chunks);
        uintmax_t total = 0;
        uintmax_t totalc = 0;
        uintmax_t current_split = 0;
        bool split_open = false;
        TempFileFILE * current_tmp_split = nullptr;

        for (uintmax_t i = 0; i < files; i++) {
            const char* file = r.read_string();
            const char* file_perms = r.read_string();
            std::filesystem::file_time_type::rep file_time = (std::filesystem::file_time_type::rep)r.read_u64();
            uint64_t file_size = r.read_u64();
            uint64_t file_chunks = r.read_u64();
            total += file_size;

            if (join_files) {
                if (dry_run) {
                    fmt::print("fopen({}/{}, \"wb\")\n", out_directory, file);
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        if (split != current_split) {
                            if (split_open) {
                                fmt::print("fclose({}/split.{}.<TMP_XXXXXX>)\n", parent, current_split);
                                split_open = false;
                            }
                            if (remove_files) {
                                fmt::print("rm -f {}/split.{}.<TMP_XXXXXX>\n", parent, current_split);
                            }
                            current_split = split;
                        }
                        if (!split_open) {
                            char* t = strdup(url);
                            if (t == nullptr) {
                                throw std::bad_alloc();
                            }
                            strrchr(t, '/')[1] = '\0';
                            fmt::print("download_url({}{}split.{}) -> {}/split.{}.<TMP_XXXXXX>\n", t, SPLIT_PREFIX, split, parent, split);
                            free(t);
                            fmt::print("fopen({}/split.{}.<TMP_XXXXXX>, \"rb\")\n", parent, split);
                            fmt::print("fseek({}/split.{}.<TMP_XXXXXX>, 0)\n", parent, SPLIT_PREFIX, split);
                            split_open = true;
                        }
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        fmt::print("fread({}/split.{}.<TMP_XXXXXX>, buf, {})\n", parent, split, length);
                        fmt::print("fwrite({}/{}, buf, {})\n", out_directory, file, length);
                        totalc += length;
                    }
                    fmt::print("fflush({}/{})\n", out_directory, file);
                    fmt::print("fclose({}/{})\n", out_directory, file);
                    fmt::print("chmod {: >9} {}/{}\n", file_perms, out_directory, file);
                }
                else {
                    if (verbose_files) fmt::print("unpacking file: {}/{}\n", out_directory, file);
                    std::string out_f = out_directory + "/" + file;
                    FILE* f = fopen(out_f.c_str(), "wb");
                    if (f == nullptr) {
                        fmt::print("failed to create file: {}\n", out_f);
                        delete current_tmp_split;
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return -1;
                    }
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        if (split != current_split) {
                            if (split_open) {
                                if (!remove_files) {
                                    current_tmp_split->detach();
                                }
                                delete current_tmp_split;
                                current_tmp_split = nullptr;
                                split_open = false;
                                fmt::print("extracted\n");
                                fflush(stdout);
                                fflush(stderr);
                            }
                            current_split = split;
                        }
                        if (!split_open) {
                            char* t = strdup(url);
                            if (t == nullptr) {
                                throw std::bad_alloc();
                            }
                            strrchr(t, '/')[1] = '\0';
                            current_tmp_split = new TempFileFILE();
                            current_tmp_split->construct(TempFile::TempDir(), fmt::format("split.{}.", split), TEMP_FILE_OPEN_MODE_READ | TEMP_FILE_OPEN_MODE_WRITE | TEMP_FILE_OPEN_MODE_BINARY, !remove_files);
                            std::string out_url = fmt::format("{}{}split.{}", t, SPLIT_PREFIX, split);
                            fmt::print("downloading item: {}\n", out_url);
                            auto in_s = current_tmp_split->get_path();
                            fmt::print("-> path: {}\n", in_s);
                            if (download_url(out_url.c_str(), *current_tmp_split) == -1) {
                                fmt::print("failed to download item: {}\n", out_url);
                                delete current_tmp_split;
                                current_tmp_split = nullptr;
                                free(t);
                                fclose(f);
                                r.close();
                                free((void*)SPLIT_PREFIX);
                                free((void*)max_path);
                                free((void*)max_perms);
                                return -1;
                            }
                            fmt::print("downloaded item: {}\n", out_url);
                            fmt::print("extracting ...\n");
                            fflush(stdout);
                            fflush(stderr);
                            free(t);
                            fseek(current_tmp_split->get_handle(), 0, SEEK_SET);
                            split_open = true;
                        }
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        void* buffer = malloc(length);
                        if (buffer == nullptr) {
                            throw std::bad_alloc();
                        }
                        fread(buffer, 1, length, current_tmp_split->get_handle());
                        fwrite(buffer, 1, length, f);
                        free(buffer);
                        totalc += length;
                    }
                    fflush(f);
                    fclose(f);
                    f = nullptr;
                    std::filesystem::permissions(out_f, permissions_to_filesystem(string_to_permissions(file_perms)));
                    std::filesystem::last_write_time(out_f, std::filesystem::file_time_type(std::filesystem::file_time_type::duration(file_time)));
                }
            }
            else {
                if (list_chunks) {
                    fmt::print("{} {: >8}   ({: >{}} chunks)   {}\n", file_perms, file_size, file_chunks, mfc, file);
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        fmt::print("   [chunk] {}split.{} [{: >{}}-{: >{}}]\n", SPLIT_PREFIX, split, offset, fmt::formatted_size("{}", SPLIT_SIZE), offset + length, fmt::formatted_size("{}", SPLIT_SIZE));
                        totalc += length;
                    }
                }
                else {
                    fmt::print("{} {: >8}   ({: >{}} chunks)   {}\n", file_perms, file_size, file_chunks, mfc, file);
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
                    fmt::print("fclose({}/split.{}.<TMP_XXXXXX>)\n", parent, current_split);
                    if (remove_files) {
                        fmt::print("rm -f {}/split.{}.<TMP_XXXXXX>\n", parent, current_split);
                    }
                }
                else {
                    if (!remove_files) {
                        current_tmp_split->detach();
                    }
                    delete current_tmp_split;
                    current_tmp_split = nullptr;
                }
                split_open = false;
                fmt::print("extracted\n");
                fflush(stdout);
                fflush(stderr);
            }
        }
        fmt::print("total size of {: >{}} files:  {: >{}} bytes\n", files, fmt::formatted_size("{}", std::max(files, chunks)), total, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("total size of {: >{}} chunks: {: >{}} bytes\n", chunks, fmt::formatted_size("{}", std::max(files, chunks)), totalc, fmt::formatted_size("{}", std::max(total, totalc)));
        fmt::print("largest file: {: >{}}         {} {: >8}   ({: >{}} chunks)   {}\n", "", fmt::formatted_size("{}", std::max(files, chunks)), max_perms, max_size, max_chunk, mfc, max_path);
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
                    if (verbose_files) fmt::print("unpacking symlink: {}/{}\n", out_directory, symlink);
                    std::filesystem::path sp = out_directory + "/" + symlink;

                    // TODO: set symlink permissions for MacOS
                    //
                    // TODO: resolve a symlink destination path with account for non-existant path
                    // 
                    // attempt to handle the case where some systems require a symlink to
                    // specify that its target is a directory or a file
                    //
                    //if (path_exists(sp)) {
                    //    fmt::print("symlink destination does not exist: {}\n", parent);
                    //    r.close();
                    //    free((void*)SPLIT_PREFIX);
                    //    free((void*)max_path);
                    //    free((void*)max_perms);
                    //    return -1;
                    //}
                    //auto parent = sp;
                    //if (!parent.has_parent_path()) {
                    //    fmt::print("cannot obtain parent directory of item: {}\n", parent);
                    //    r.close();
                    //    free((void*)SPLIT_PREFIX);
                    //    free((void*)max_path);
                    //    free((void*)max_perms);
                    //    return -1;
                    //}
                    //parent = parent.parent_path();
                    //std::filesystem::path t = parent.string() + "/" + symlink_dest;
                    //std::filesystem::path resolved_t = std::filesystem::weakly_canonical(symlink_dest);
                    //if (path_exists(resolved_t)) {
                    //    if (std::filesystem::is_directory(resolved_t)) {
                    //        std::filesystem::create_directory_symlink(symlink_dest, sp);
                    //    }
                    //    else {
                    //        std::filesystem::create_symlink(symlink_dest, sp);
                    //    }
                    //}
                    //else {
                    std::filesystem::create_symlink(symlink_dest, sp);
                    //}
                }
            }
            else {
                fmt::print("{} {: >8}   ({: >{}} chunks)   {} -> {}\n", "lrwxrwxrwx", 0, 0, mfc, symlink, symlink_dest);
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
                    std::filesystem::permissions(out_directory + "/" + dirs.first, permissions_to_filesystem(string_to_permissions(dirs.second.first)));
                    std::filesystem::last_write_time(out_directory + "/" + dirs.first, std::filesystem::file_time_type(std::filesystem::file_time_type::duration(dirs.second.second)));
                }
                free((void*)dirs.first);
                free((void*)dirs.second.first);
            }
        }
        if (!dry_run && !remove_files) {
            tmp_split_map.detach();
        }
        r.close();
        free((void*)SPLIT_PREFIX);
        free((void*)max_path);
        free((void*)max_perms);
        return 0;
    }
    int playback_file(const char * path, bool join_files, bool list_chunks) {
        if (join_files) {
            if (path_exists(out_directory)) {
                if (!dry_run) {
                    if (!std::filesystem::is_directory(out_directory)) {
                        fmt::print("cannot output to a non-directory: {}\n", out_directory);
                        return -1;
                    }
                    std::filesystem::directory_iterator begin = std::filesystem::directory_iterator(out_directory);
                    std::filesystem::directory_iterator end;
                    for (; begin != end; begin++) {
                        auto& fpath = *begin;
                        if (path_exists(fpath)) {
                            fmt::print("cannot output to a non-empty directory: {}\n", out_directory);
                            return -1;
                        }
                    }
                }
            }
            else {
                if (dry_run) {
                    fmt::print("mkdir {}\n", out_directory);
                }
                else {
                    fmt::print("creating output directory: {}\n", out_directory);
                    std::filesystem::create_directory(out_directory);
                }
            }
        }

        std::filesystem::path p = std::filesystem::path(path);
        if (!path_exists(p)) {
            fmt::print("item does not exist: {}\n", path);
            return -1;
        }
        auto parent = std::filesystem::canonical(std::filesystem::absolute(path));
        if (!parent.has_parent_path()) {
            fmt::print("cannot obtain parent directory of item: {}\n", parent);
            return -1;
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
            return -1;
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
                    if (verbose_files) fmt::print("unpacking directory: {}/{}\n", out_directory, dir);
                    if (!std::filesystem::create_directory(out_directory + "/" + dir)) {
                        fmt::print("failed to create directory: {}/{}\n", out_directory, dir);
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return -1;
                    }
                }
                dirs_vec.emplace_back(std::pair<const char*, std::pair<const char*, std::filesystem::file_time_type::rep>>(dir, std::pair<const char*, std::filesystem::file_time_type::rep>(dir_perms, t)));
            }
            else {
                fmt::print("{} {: >8}   ({: >{}} chunks)   {}\n", dir_perms, 0, 0, mfc, dir);
                free((void*)dir);
                free((void*)dir_perms);
            }
        }
        fmt::print("reading {} files with a total of {} split files consisting of {} chunks\n", files, split_number+1, chunks);
        uintmax_t total = 0;
        uintmax_t totalc = 0;
        uintmax_t current_split = 0;
        bool split_open = false;
        FILE* current_split_file = nullptr;

        for (uintmax_t i = 0; i < files; i++) {
            const char* file = r.read_string();
            const char* file_perms = r.read_string();
            std::filesystem::file_time_type::rep file_time = (std::filesystem::file_time_type::rep)r.read_u64();
            uint64_t file_size = r.read_u64();
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
                    if (verbose_files) fmt::print("unpacking file: {}/{}\n", out_directory, file);
                    std::string out_f = out_directory + "/" + file;
                    FILE * f = fopen(out_f.c_str(), "wb");
                    if (f == nullptr) {
                        fmt::print("failed to create file: {}\n", out_f);
                        r.close();
                        free((void*)SPLIT_PREFIX);
                        free((void*)max_path);
                        free((void*)max_perms);
                        return -1;
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
                                return -1;
                            }
                            fseek(current_split_file, 0, SEEK_SET);
                            split_open = true;
                        }
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        void* buffer = malloc(length);
                        if (buffer == nullptr) {
                            throw std::bad_alloc();
                        }
                        fread(buffer, 1, length, current_split_file);
                        fwrite(buffer, 1, length, f);
                        free(buffer);
                        totalc += length;
                    }
                    fflush(f);
                    fclose(f);
                    f = nullptr;
                    std::filesystem::permissions(out_f, permissions_to_filesystem(string_to_permissions(file_perms)));
                    std::filesystem::last_write_time(out_f, std::filesystem::file_time_type(std::filesystem::file_time_type::duration(file_time)));
                }
            }
            else {
                if (list_chunks) {
                    fmt::print("{} {: >8}   ({: >{}} chunks)   {}\n", file_perms, file_size, file_chunks, mfc, file);
                    for (uintmax_t i = 0; i < file_chunks; i++) {
                        uintmax_t split = r.read_u64();
                        uintmax_t offset = r.read_u64();
                        uintmax_t length = r.read_u64();
                        fmt::print("   [chunk] {}split.{} [{: >{}}-{: >{}}]\n", SPLIT_PREFIX, split, offset, fmt::formatted_size("{}", SPLIT_SIZE), offset + length, fmt::formatted_size("{}", SPLIT_SIZE));
                        totalc += length;
                    }
                }
                else {
                    fmt::print("{} {: >8}   ({: >{}} chunks)   {}\n", file_perms, file_size, file_chunks, mfc, file);
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
        fmt::print("largest file: {: >{}}         {} {: >8}   ({: >{}} chunks)   {}\n", "", fmt::formatted_size("{}", std::max(files, chunks)), max_perms, max_size, max_chunk, mfc, max_path);
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
                    if (verbose_files) fmt::print("unpacking symlink: {}/{}\n", out_directory, symlink);
                    std::filesystem::path sp = out_directory + "/" + symlink;

                    // TODO: set symlink permissions for MacOS
                    //
                    // TODO: resolve a symlink destination path with account for non-existant path
                    // 
                    // attempt to handle the case where some systems require a symlink to
                    // specify that its target is a directory or a file
                    //
                    //if (path_exists(sp)) {
                    //    fmt::print("symlink destination does not exist: {}\n", parent);
                    //    r.close();
                    //    free((void*)SPLIT_PREFIX);
                    //    free((void*)max_path);
                    //    free((void*)max_perms);
                    //    return -1;
                    //}
                    //auto parent = sp;
                    //if (!parent.has_parent_path()) {
                    //    fmt::print("cannot obtain parent directory of item: {}\n", parent);
                    //    r.close();
                    //    free((void*)SPLIT_PREFIX);
                    //    free((void*)max_path);
                    //    free((void*)max_perms);
                    //    return -1;
                    //}
                    //parent = parent.parent_path();
                    //std::filesystem::path t = parent.string() + "/" + symlink_dest;
                    //std::filesystem::path resolved_t = std::filesystem::weakly_canonical(symlink_dest);
                    //if (path_exists(resolved_t)) {
                    //    if (std::filesystem::is_directory(resolved_t)) {
                    //        std::filesystem::create_directory_symlink(symlink_dest, sp);
                    //    }
                    //    else {
                    //        std::filesystem::create_symlink(symlink_dest, sp);
                    //    }
                    //}
                    //else {
                        std::filesystem::create_symlink(symlink_dest, sp);
                    //}
                }
            }
            else {
                fmt::print("{} {: >8}   ({: >{}} chunks)   {} -> {}\n", "lrwxrwxrwx", 0, 0, mfc, symlink, symlink_dest);
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
                    std::filesystem::permissions(out_directory + "/" + dirs.first, permissions_to_filesystem(string_to_permissions(dirs.second.first)));
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
    int playback(const char* path, bool join_files, bool list_chunks) {
        return is_url(path) ? playback_url(path, join_files, list_chunks) : playback_file(path, join_files, list_chunks);
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
    fmt::print("         -v\n");
    fmt::print("                 list each item as it is processed\n");
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
    fmt::print("\n--join   [-n] [-r] [[prefix.]split.map | [http|https|ftp|ftps]://URL ] --out <out_dir>\n");
    fmt::print("         info\n");
    fmt::print("                 join a split map to restore a directory/file\n");
    fmt::print("         -n\n");
    fmt::print("                 list what would have been done, do not create/modify anything\n");
    fmt::print("                 if a URL is given, then only the *split.map is downloaded\n");
    fmt::print("                 if a URL is given, then the downloaded *split.map is automatically\n");
    fmt::print("                 removed as-if [-r] where given\n");
    fmt::print("         -r\n");
    fmt::print("                 remove each *split.* upon its contents being extracted\n");
    fmt::print("                 if -n is given, no *split.* files will be removed unless a URL was given\n");
    fmt::print("         -v\n");
    fmt::print("                 list each item as it is processed\n");
    fmt::print("         [prefix.]\n");
    fmt::print("                 an optional prefix for the split map\n");
    fmt::print("         [http|https|ftp|ftps]://URL\n");
    fmt::print("                 a URL to a split.map file hosted online, local path rules apply\n");
    fmt::print("                   both http and https are accepted\n");
    fmt::print("                   both ftp and ftps are accepted\n");
    fmt::print("                   files CANNOT be split against http* and ftp*\n");
    fmt::print("                   http://url/[prefix.]split.map // usually redirects to https\n");
    fmt::print("                   https://url/[prefix.]split.map // redirected from http\n");
    fmt::print("                   https://url/[prefix.]split.0\n");
    fmt::print("                   https://url/[prefix.]split.1\n");
    fmt::print("                   https://url/[prefix.]split.2\n");
    fmt::print("                   ftp://url/[prefix.]split.2 // redirected\n");
    fmt::print("                   // https://url/[prefix.]split.2 -> ftp://url/[prefix.]split.2\n");
    fmt::print("                   https://url/[prefix.]split.3\n");
    fmt::print("                   and so on\n");
    fmt::print("                   NOTE: the above http/ftp mix cannot occur UNLESS the URL redirects to such\n");
    fmt::print("                   NOTE: the above http -> ftp redirect does not occur normally and requires\n");
    fmt::print("                         specific support from the web host/page\n");
    fmt::print("         --out\n");
    fmt::print("                 the directory to restore a directory/file into\n");
    fmt::print("                 defaults to the current directory\n");
}

void ls_usage() {
    fmt::print("\n--ls     [[prefix.]split.map | [http|https|ftp|ftps]://URL ]\n");
    fmt::print("         info\n");
    fmt::print("                 list the contents of a split map\n");
    fmt::print("         [prefix.]\n");
    fmt::print("                 an optional prefix for the split map\n");
    fmt::print("         [http|https|ftp|ftps]://URL\n");
    fmt::print("                 a URL to a split.map file hosted online, local path rules apply\n");
    fmt::print("                 downloaded files are automatically removed as-if [ --join -n ] where used\n");
    fmt::print("                   both http and https are accepted\n");
    fmt::print("                   both ftp and ftps are accepted\n");
    fmt::print("                   files CANNOT be split against http* and ftp*\n");
    fmt::print("                   http://url/[prefix.]split.map // usually redirects to https\n");
    fmt::print("                   https://url/[prefix.]split.map // redirected from http\n");
    fmt::print("                   https://url/[prefix.]split.0\n");
    fmt::print("                   https://url/[prefix.]split.1\n");
    fmt::print("                   https://url/[prefix.]split.2\n");
    fmt::print("                   ftp://url/[prefix.]split.2 // redirected\n");
    fmt::print("                   // https://url/[prefix.]split.2 -> ftp://url/[prefix.]split.2\n");
    fmt::print("                   https://url/[prefix.]split.3\n");
    fmt::print("                   and so on\n");
    fmt::print("                   NOTE: the above http/ftp mix cannot occur UNLESS the URL redirects to such\n");
    fmt::print("                   NOTE: the above http -> ftp redirect does not occur normally and requires\n");
    fmt::print("                         specific support from the web host/page\n");
}

void usage() {
    split_usage();
    join_usage();
    ls_usage();
}

int main(int argc, const char** argv) {
    if (argc == 1) {
        usage();
        return 0;
    }
    next_is_help = false;
    while (true) {
        if (next_is_help) {
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
            return next_ret;
        }
        argc--;
        argv++;
        next_is_help = false;
        if (command_selected) {
            if (is_ls) {
                if (argc == 0) {
                    // all arguments must have been met
                    if (file.length() == 0) {
                        next_is_help = true;
                        continue;
                    }
                    // the next argument must be a dir/file
                    PathRecorder p;
                    return p.playback(file.c_str(), false, false);
                }
                // any other arg MIGHT be invalid, show help if explicitly requested
                if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
                    next_is_help = true;
                    next_ret = 0;
                    continue;
                }
                file = std::string(argv[0]);
                continue;
            }
            else if (is_split) {
                if (argc == 0) {
                    // all arguments must have been met
                    if (file.length() == 0) {
                        next_is_help = true;
                        continue;
                    }
                    if (!path_exists(file)) {
                        fmt::print("item does not exist: {}\n", file);
                        return -1;
                    }
                    if (remove_files) {
                        // TODO: make this work for a non-existing symlink
                        auto cwd = std::filesystem::current_path();
                        auto current = cwd;
                        auto root = current.root_path();
                        auto par = std::filesystem::path(file);
                        if (par.has_parent_path() && par != root) {
                            par = par.parent_path();
                        }
                        auto p = par.string();
                        char* res = realpath(p.c_str(), nullptr);
                        if (res == nullptr) {
                            auto se = errno;
                            fmt::print("failed to resolve path of {}\nerrno: -{} ({})\n", p, se, fmt::system_error(se, ""));
                            return -1;
                        }
                        std::string r = std::string(res) + "/" + std::filesystem::path(file).filename().string();
                        free((void*)res);
                        std::filesystem::path target = r;
                        if (current.compare(target) == 0) {
                            fmt::print("cannot remove the current working directory\n");
                            return -1;
                        }
                        else if (current.compare(target) == 1) {
                            while (current.has_parent_path() && current != root) {
                                if (current.compare(target) == 0) {
                                    fmt::print("cannot remove a parent directory\n");
                                    return -1;
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
                    continue;
                }
                if (next_is_size) {
                    SPLIT_SIZE = (uintmax_t)atoll(argv[0]);
                    next_is_size = false;
                    continue;
                }
                if (strcmp(argv[0], "-n") == 0) {
                    dry_run = true;
                    continue;
                }
                if (strcmp(argv[0], "-r") == 0) {
                    remove_files = true;
                    continue;
                }
                if (strcmp(argv[0], "-v") == 0) {
                    verbose_files = true;
                    continue;
                }
                if (strcmp(argv[0], "--size") == 0) {
                    next_is_size = true;
                    continue;
                }
                if (strcmp(argv[0], "--name") == 0) {
                    next_is_name = true;
                    continue;
                }
                // any other arg MIGHT be invalid, show help if explicitly requested
                if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
                    next_is_help = true;
                    next_ret = 0;
                    continue;
                }
                file = std::string(argv[0]);
                continue;
            }
            else if (is_join) {
                if (argc == 0) {
                    // all arguments must have been met
                    if (file.length() == 0) {
                        next_is_help = true;
                        continue;
                    }
                    if (out_directory.length() == 0) {
                        out_directory = ".";
                    }
                    PathRecorder p;
                    return p.playback(file.c_str(), true, true);
                }
                if (next_is_name) {
                    out_directory = std::string(argv[0]);
                    next_is_name = false;
                    continue;
                }
                if (strcmp(argv[0], "-n") == 0) {
                    dry_run = true;
                    continue;
                }
                if (strcmp(argv[0], "-r") == 0) {
                    remove_files = true;
                    continue;
                }
                if (strcmp(argv[0], "-v") == 0) {
                    verbose_files = true;
                    continue;
                }
                if (strcmp(argv[0], "--out") == 0) {
                    next_is_name = true;
                    continue;
                }
                // any other arg MIGHT be invalid, show help if explicitly requested
                if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
                    next_is_help = true;
                    next_ret = 0;
                    continue;
                }
                file = std::string(argv[0]);
                continue;
            }
        }
        else {
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
            if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
                next_is_help = true;
                next_ret = 0;
                continue;
            }
            // any other arg is invalid, show help
            next_is_help = true;
        }
    }
    return 0;
}
