#pragma once

#include <vector>
#include <span>
#include <cstdint>
#include <expected>
#include <string_view>

enum class TransformError {
    EmptyInput,
    InvalidIndex
};

std::string_view TransformError_to_string(TransformError err);

class BWT {
public:
    static std::expected<std::vector<uint8_t>, TransformError> Encode(std::span<const uint8_t> input, uint32_t& out_primary_index);
    static std::expected<std::vector<uint8_t>, TransformError> Decode(std::span<const uint8_t> input, uint32_t primary_index);
};

class MTF {
public:

    static std::expected<std::vector<uint8_t>, TransformError> Encode(std::span<const uint8_t> input);
    static std::expected<std::vector<uint8_t>, TransformError> Decode(std::span<const uint8_t> input);
};