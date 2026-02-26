#include "../BitStream/BitStream.hpp"
#include <algorithm>
#include <iostream>
#include <print>

std::string_view error_to_string(BitStreamError err) {
    switch (err) {
    case BitStreamError::EndOfFile:      return "Досягнуто кінець потоку.";
    case BitStreamError::WriteError:     return "Помилка запису в потік.";
    case BitStreamError::ReadError:      return "Помилка читання з пристрою.";
    case BitStreamError::BufferTooSmall: return "Буфер замалий для такої кількості бітів.";
    default:                             return "Невідома помилка.";
    }
}

BitWriter::BitWriter(std::ostream& out) : out_(out) {}

BitWriter::~BitWriter() {
    auto _ = Flush();
}

std::expected<void, BitStreamError> BitWriter::Flush() {
    if (bit_pos_ > 0) {
        out_.put(static_cast<char>(current_byte_));
        if (out_.fail()) {
            std::println(stderr, "BitWriter Flush Error: {}", error_to_string(BitStreamError::WriteError));
            return std::unexpected(BitStreamError::WriteError);
        }
        current_byte_ = 0;
        bit_pos_ = 0;
    }
    return {};
}

std::expected<void, BitStreamError> BitWriter::WriteBitSequence(std::span<const uint8_t> data, size_t bit_length) {
    size_t bits_written = 0;
    for (uint8_t byte : data) {
        for (int i = 0; i < 8; ++i) {
            if (bits_written >= bit_length) return {};
            uint8_t bit = (byte >> i) & 1;
            current_byte_ |= (bit << bit_pos_);
            bit_pos_++;
            bits_written++;

            if (bit_pos_ == 8) {
                out_.put(static_cast<char>(current_byte_));
                if (out_.fail()) {
                    std::println(stderr, "BitWriter Write Error: {}", error_to_string(BitStreamError::WriteError));
                    return std::unexpected(BitStreamError::WriteError);
                }
                current_byte_ = 0;
                bit_pos_ = 0;
            }
        }
    }
    return {};
}

BitReader::BitReader(std::istream& in) : in_(in) {}

std::expected<void, BitStreamError> BitReader::ReadBitSequence(std::span<uint8_t> data, size_t bit_length) {
    std::ranges::fill(data, 0);

    size_t bits_read = 0;
    size_t byte_idx = 0;
    uint8_t dest_bit_pos = 0;

    while (bits_read < bit_length) {
        if (bit_pos_ == 8) {
            char c;
            if (!in_.get(c)) {
                std::println(stderr, "BitReader Read Error: {}", error_to_string(BitStreamError::EndOfFile));
                return std::unexpected(BitStreamError::EndOfFile);
            }
            current_byte_ = static_cast<uint8_t>(c);
            bit_pos_ = 0;
        }

        uint8_t bit = (current_byte_ >> bit_pos_) & 1;
        bit_pos_++;

        if (byte_idx < data.size()) {
            data[byte_idx] |= (bit << dest_bit_pos);
            dest_bit_pos++;

            if (dest_bit_pos == 8) {
                dest_bit_pos = 0;
                byte_idx++;
            }
        }
        else {
			std::println(stderr, "BitReader Read Error: {}", error_to_string(BitStreamError::BufferTooSmall));
            return std::unexpected(BitStreamError::BufferTooSmall);
        }

        bits_read++;
    }
    return {};
}