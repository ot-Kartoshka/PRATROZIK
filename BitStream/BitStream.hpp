#pragma once

#include <iostream>
#include <span>
#include <cstdint>
#include <expected>
#include <vector>
#include <string_view>

enum class BitStreamError {
    EndOfFile,
    WriteError,
    ReadError,
    BufferTooSmall
};

std::string_view error_to_string(BitStreamError err);

class BitWriter {
public:
    explicit BitWriter(std::ostream& out);
    ~BitWriter();

    BitWriter(const BitWriter&) = delete;
    BitWriter& operator=(const BitWriter&) = delete;

    std::expected<void, BitStreamError> WriteBitSequence(std::span<const uint8_t> data, size_t bit_length);

    std::expected<void, BitStreamError> Flush();

private:
    std::ostream& out_;
    uint8_t current_byte_ = 0;
    uint8_t bit_pos_ = 0;
};

class BitReader {
public:
    explicit BitReader(std::istream& in);

    BitReader(const BitReader&) = delete;
    BitReader& operator=(const BitReader&) = delete;

    std::expected<void, BitStreamError> ReadBitSequence(std::span<uint8_t> data, size_t bit_length);

private:
    std::istream& in_;
    uint8_t current_byte_ = 0;
    uint8_t bit_pos_ = 8;
};