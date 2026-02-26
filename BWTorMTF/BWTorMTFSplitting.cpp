#include "BWTorMTFSplitting.hpp"
#include "BWTorMTF.hpp"
#include <fstream>
#include <vector>
#include <iostream>
#include <print>

std::string_view SplittingError_to_string(SplittingError err) {
    switch (err) {
	case SplittingError::FileOpenError:   return "Помилка відкриття файлу для читання або запису.";
	case SplittingError::TransformFailed: return "Помилка при застосуванні перетворень BWT/MTF.";
	default:                              return "Сталася невідома помилка при роботі з перетвореннями BWT/MTF.";
    }
}

std::expected<void, SplittingError> TransformSplitting::ApplyForward(
    const std::filesystem::path& in_path, const std::filesystem::path& out_path,
    bool use_bwt, bool use_mtf)
{
    std::ifstream in(in_path, std::ios::binary);
    std::ofstream out(out_path, std::ios::binary);
    if (!in || !out) {
        std::println(stderr, "Splitting File Error: {}", SplittingError_to_string(SplittingError::FileOpenError));
        return std::unexpected(SplittingError::FileOpenError);
    }

    std::vector<uint8_t> buffer(BLOCK_SIZE);
    while (in.read(reinterpret_cast<char*>(buffer.data()), BLOCK_SIZE) || in.gcount() > 0) {
        size_t bytes_read = in.gcount();
        std::span<const uint8_t> current_span(buffer.data(), bytes_read);
        std::vector<uint8_t> transformed;
        uint32_t bwt_index = 0;

        if (use_bwt) {
            auto bwt_res = BWT::Encode(current_span, bwt_index);
            if (!bwt_res) {
                std::println(stderr, "Splitting Error: {}", SplittingError_to_string(SplittingError::TransformFailed));
                return std::unexpected(SplittingError::TransformFailed);
            }
            transformed = std::move(bwt_res.value());
            current_span = transformed;
        }
        if (use_mtf) {
            auto mtf_res = MTF::Encode(current_span);
            if (!mtf_res) {
                std::println(stderr, "Splitting Error: {}", SplittingError_to_string(SplittingError::TransformFailed));
                return std::unexpected(SplittingError::TransformFailed);
            }
            transformed = std::move(mtf_res.value());
            current_span = transformed;
        }

        uint32_t block_size = static_cast<uint32_t>(current_span.size());
        out.write(reinterpret_cast<const char*>(&block_size), sizeof(block_size));
        if (use_bwt)
            out.write(reinterpret_cast<const char*>(&bwt_index), sizeof(bwt_index));
        out.write(reinterpret_cast<const char*>(current_span.data()), current_span.size());
    }
    return {};
}

std::expected<void, SplittingError> TransformSplitting::ApplyReverse(
    const std::filesystem::path& in_path, const std::filesystem::path& out_path,
    bool use_bwt, bool use_mtf)
{
    std::ifstream in(in_path, std::ios::binary);
    std::ofstream out(out_path, std::ios::binary);
    if (!in || !out) {
        std::println(stderr, "Splitting File Error: {}", SplittingError_to_string(SplittingError::FileOpenError));
        return std::unexpected(SplittingError::FileOpenError);
    }

    while (in.peek() != EOF) {
        uint32_t block_size = 0;
        if (!in.read(reinterpret_cast<char*>(&block_size), sizeof(block_size))) break;

        uint32_t bwt_index = 0;
        if (use_bwt) {
            if (!in.read(reinterpret_cast<char*>(&bwt_index), sizeof(bwt_index))) break;
        }

        std::vector<uint8_t> buffer(block_size);
        if (!in.read(reinterpret_cast<char*>(buffer.data()), block_size)) break;

        std::span<const uint8_t> current_span = buffer;
        std::vector<uint8_t> restored;

        if (use_mtf) {
            auto mtf_res = MTF::Decode(current_span);
            if (!mtf_res) {
                std::println(stderr, "Splitting Error: {}", SplittingError_to_string(SplittingError::TransformFailed));
                return std::unexpected(SplittingError::TransformFailed);
            }
            restored = std::move(mtf_res.value());
            current_span = restored;
        }
        if (use_bwt) {
            auto bwt_res = BWT::Decode(current_span, bwt_index);
            if (!bwt_res) {
                std::println(stderr, "Splitting Error: {}", SplittingError_to_string(SplittingError::TransformFailed));
                return std::unexpected(SplittingError::TransformFailed);
            }
            restored = std::move(bwt_res.value());
            current_span = restored;
        }

        out.write(reinterpret_cast<const char*>(current_span.data()), current_span.size());
    }
    return {};
}