#pragma once

#include <string>
#include <vector>
#include <array>
#include <cstdint>
#include <expected>
#include <string_view>
#include <filesystem>
#include <memory>

struct HuffmanStats {
    uintmax_t original_size;
    uintmax_t compressed_size;
    uintmax_t metadata_size;
};

enum class HuffmanError {
    FileNotFound,
    FileReadError,
    FileWriteError,
    InvalidFormat,
    UserCancelled,
    FileSameAsInput,
    EmptyFile,
	NoPathProvided,
    TransformFailed 
};

std::string_view HuffmanError_to_string(HuffmanError err);

class HuffmanCoder {
public:
    static std::expected<HuffmanStats, HuffmanError> Compress(
        const std::filesystem::path& in_path,
        std::filesystem::path out_path = "",
        bool use_bwt = false,
        bool use_mtf = false);

    static std::expected<void, HuffmanError> Decompress(
        const std::filesystem::path& in_path,
        std::filesystem::path out_path);

    static std::expected<std::string, HuffmanError> ExtractOriginalFilename(
        const std::filesystem::path& in_path);

private:
    struct Node {
        uint8_t symbol = 0;
        uint32_t freq = 0;
        Node* left = nullptr;
        Node* right = nullptr;

        bool is_leaf() const { return !left && !right; }
    };

    struct CompareNode {
        bool operator()(const Node* a, const Node* b) const {
            return a->freq > b->freq;
        }
    };

    struct Code {
        std::vector<uint8_t> data;
        size_t bit_length = 0;
    };

    static void BuildCodes(Node* node, std::vector<bool>& current_path, std::array<Code, 256>& codes);
    static std::vector<uint8_t> PackBits(const std::vector<bool>& bits);
};