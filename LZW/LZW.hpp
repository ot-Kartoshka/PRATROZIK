#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <expected>
#include <string_view>
#include <filesystem>
#include <unordered_map>

struct LZWHeader {
    std::string original_name;
    uint8_t max_bits;
    bool clear_on_overflow;
    bool use_bwt;
    bool use_mtf; 
};

struct LZWStats {
    uintmax_t original_size;
    uintmax_t compressed_size;
    uintmax_t metadata_size;
};

enum class LZWError {
    FileNotFound,
    FileReadError,
    FileWriteError,
    InvalidFormat,
    UserCancelled,
    FileSameAsInput,
    EmptyFile,
    LovHighMaxBit,
    NoMaxBit,
    TransformFailed,
    NoPathProvided
};

std::string_view LZWError_to_string(LZWError err);

class LZWCoder {
public:
    static std::expected<LZWStats, LZWError> Compress(
        const std::filesystem::path& in_path,
        std::filesystem::path out_path = "",
        uint8_t max_bits = 16,
        bool clear_on_overflow = true,
        bool use_bwt = false,
        bool use_mtf = false);

    static std::expected<void, LZWError> Decompress(
        const std::filesystem::path& in_path,
        std::filesystem::path out_path = "");

    static std::expected<std::string, LZWError> ExtractOriginalFilename(
        const std::filesystem::path& in_path);

private:
    static std::expected<LZWHeader, LZWError> ReadHeader(std::ifstream& in);
    static constexpr uint32_t CLEAR_CODE = 256;
    static constexpr uint32_t EOF_CODE = 257;
    static constexpr uint32_t FIRST_CODE = 258;

    struct DictEntry {
        uint32_t prefix;
        uint8_t ch;
    };
};