#pragma once

#include <filesystem>
#include <expected>

enum class SplittingError {
    FileOpenError,
    TransformFailed
};

class TransformSplitting {
    public:
        static constexpr size_t BLOCK_SIZE = 256 * 1024;

    static std::expected<void, SplittingError> ApplyForward(
        const std::filesystem::path& in_path,
        const std::filesystem::path& out_path,
        bool use_bwt,
        bool use_mtf);

    static std::expected<void, SplittingError> ApplyReverse(
        const std::filesystem::path& in_path,
        const std::filesystem::path& out_path,
        bool use_bwt,
        bool use_mtf);
};