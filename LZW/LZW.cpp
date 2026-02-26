#include "LZW.hpp"
#include "../BitStream/BitStream.hpp"
#include "../BWTorMTF/BWTorMTFSplitting.hpp"
#include <fstream>
#include <iostream>
#include <array>

namespace {
    struct TempFile {
        std::filesystem::path path;
        ~TempFile() {
            if (!path.empty() && std::filesystem::exists(path))
                std::filesystem::remove(path);
        }
    };
}

std::string_view LZWError_to_string(LZWError err) {
    switch (err) {
    case LZWError::FileNotFound:    return "Файл не знайдено за вказаним шляхом.";
    case LZWError::FileReadError:   return "Помилка читання вхідного файлу.";
    case LZWError::FileWriteError:  return "Помилка запису вихідного файлу.";
    case LZWError::InvalidFormat:   return "Некоректний формат або пошкоджений архів.";
    case LZWError::UserCancelled:   return "Операцію скасовано користувачем.";
    case LZWError::EmptyFile:       return "Файл порожній. Стиснення неможливе.";
    case LZWError::FileSameAsInput: return "Вихідний файл не може бути тим самим, що і вхідний.";
    case LZWError::LovHighMaxBit:   return "Некоректне значення max_bits. Дозволено діапазон 9-32 бітів.";
    case LZWError::NoMaxBit:        return "Не вказано значення max_bits після --max-bits.";
    case LZWError::TransformFailed: return "Помилка конвеєра перетворень BWT/MTF.";
    case LZWError::NoPathProvided:  return "Не вказано шлях до файлу.";
    default:                        return "Невідома помилка.";
    }
}

std::expected<LZWHeader, LZWError> LZWCoder::ReadHeader(std::ifstream& in) {
    char magic[3];
    if (!in.read(magic, 3) || std::string_view(magic, 3) != "LZW")
        return std::unexpected(LZWError::InvalidFormat);

    uint8_t name_len = 0;
    if (!in.read(reinterpret_cast<char*>(&name_len), 1))
        return std::unexpected(LZWError::InvalidFormat);

    std::string name;
    if (name_len > 0) {
        name.resize(name_len);
        if (!in.read(name.data(), name_len))
            return std::unexpected(LZWError::InvalidFormat);
    }

    uint8_t max_bits, behavior, transform_flags;
    if (!in.read(reinterpret_cast<char*>(&max_bits), 1) ||
        !in.read(reinterpret_cast<char*>(&behavior), 1) ||
        !in.read(reinterpret_cast<char*>(&transform_flags), 1))
        return std::unexpected(LZWError::InvalidFormat);

    return LZWHeader{
        name, max_bits, (behavior == 1),
        (transform_flags & 1) != 0,
        (transform_flags & 2) != 0
    };
}

std::expected<std::string, LZWError> LZWCoder::ExtractOriginalFilename(const std::filesystem::path& in_path) {
    std::ifstream in(in_path, std::ios::binary);
    if (!in) return std::unexpected(LZWError::FileNotFound);
    auto header_res = ReadHeader(in);
    if (!header_res) return std::unexpected(header_res.error());
    return header_res->original_name;
}

std::expected<LZWStats, LZWError> LZWCoder::Compress(
    const std::filesystem::path& in_path, std::filesystem::path out_path,
    uint8_t max_bits, bool clear_on_overflow, bool use_bwt, bool use_mtf)
{
    if (max_bits < 9 || max_bits > 32) return std::unexpected(LZWError::LovHighMaxBit);
    if (out_path.empty()) out_path = in_path.string() + ".lzw";

    std::filesystem::path data_to_compress = in_path;
    TempFile temp_;

    if (use_bwt || use_mtf) {
        auto temp_file = std::filesystem::temp_directory_path() / (in_path.filename().string() + ".lzw.tmp");
        if (!TransformSplitting::ApplyForward(in_path, temp_file, use_bwt, use_mtf))
            return std::unexpected(LZWError::TransformFailed);
        data_to_compress = temp_file;
        temp_.path = temp_file;
    }

    std::ifstream in(data_to_compress, std::ios::binary);
    if (!in) return std::unexpected(LZWError::FileNotFound);

    in.seekg(0, std::ios::end);
    if (in.tellg() == 0) return std::unexpected(LZWError::EmptyFile);
    uintmax_t orig_size = std::filesystem::file_size(in_path);
    in.seekg(0, std::ios::beg);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) return std::unexpected(LZWError::FileWriteError);

    std::string orig_name = in_path.filename().string();
    if (orig_name.length() > 255) orig_name = orig_name.substr(0, 255);
    uint8_t name_len = static_cast<uint8_t>(orig_name.length());

    out.write("LZW", 3);
    out.write(reinterpret_cast<const char*>(&name_len), 1);
    out.write(orig_name.data(), name_len);
    out.write(reinterpret_cast<const char*>(&max_bits), 1);

    uint8_t behavior_flag = clear_on_overflow ? 1 : 0;
    out.write(reinterpret_cast<const char*>(&behavior_flag), 1);

    uint8_t transform_flags = (use_bwt ? 1 : 0) | (use_mtf ? 2 : 0);
    out.write(reinterpret_cast<const char*>(&transform_flags), 1);

    uintmax_t meta_size = 3 + 1 + name_len + 1 + 1 + 1;

    std::unordered_map<uint64_t, uint32_t> dict;
    if (max_bits <= 24) dict.reserve(1ULL << max_bits);

    uint32_t current_code = FIRST_CODE;
    uint8_t  bit_length = 9;
    bool     is_frozen = false;

    {
        BitWriter bw(out);

        auto write_code = [&](uint32_t code) -> bool {
            std::array<uint8_t, 4> data = {
                static_cast<uint8_t>(code & 0xFF),
                static_cast<uint8_t>((code >> 8) & 0xFF),
                static_cast<uint8_t>((code >> 16) & 0xFF),
                static_cast<uint8_t>((code >> 24) & 0xFF)
            };
            return bw.WriteBitSequence(data, bit_length).has_value();
            };

        if (!write_code(CLEAR_CODE)) return std::unexpected(LZWError::FileWriteError);

        int first_byte = in.get();
        if (first_byte == EOF) return std::unexpected(LZWError::EmptyFile);
        uint32_t prefix = static_cast<uint32_t>(first_byte);

        int c;
        while ((c = in.get()) != EOF) {
            uint64_t key = (static_cast<uint64_t>(prefix) << 8) | static_cast<uint8_t>(c);
            auto it = dict.find(key);

            if (it != dict.end()) {
                prefix = it->second;
            }
            else {
                if (!write_code(prefix)) return std::unexpected(LZWError::FileWriteError);
                if (!is_frozen) {
                    dict[key] = current_code++;
                    if (current_code == (1ULL << bit_length)) {
                        if (bit_length < max_bits) {
                            bit_length++;
                        }
                        else {
                            if (clear_on_overflow) {
                                if (!write_code(CLEAR_CODE)) return std::unexpected(LZWError::FileWriteError);
                                dict.clear();
                                current_code = FIRST_CODE;
                                bit_length = 9;
                            }
                            else {
                                is_frozen = true;
                            }
                        }
                    }
                }
                prefix = static_cast<uint32_t>(c);
            }
        }
        if (!write_code(prefix))   return std::unexpected(LZWError::FileWriteError);
        if (!write_code(EOF_CODE)) return std::unexpected(LZWError::FileWriteError);
    }

    return LZWStats{ orig_size, std::filesystem::file_size(out_path), meta_size };
}

std::expected<void, LZWError> LZWCoder::Decompress(
    const std::filesystem::path& in_path, std::filesystem::path out_path)
{
    std::ifstream in(in_path, std::ios::binary);
    if (!in) return std::unexpected(LZWError::FileNotFound);

    auto header_res = ReadHeader(in);
    if (!header_res) return std::unexpected(header_res.error());

    const auto& header = header_res.value();
    if (out_path.empty()) out_path = header.original_name;

    std::filesystem::path extracted_data_path = out_path;
    TempFile temp_;

    if (header.use_bwt || header.use_mtf) {
        auto temp_file = std::filesystem::temp_directory_path() / (out_path.filename().string() + ".lzw.tmp");
        extracted_data_path = temp_file;
        temp_.path = temp_file;
    }

    std::ofstream out(extracted_data_path, std::ios::binary);
    if (!out) return std::unexpected(LZWError::FileWriteError);

    uint8_t max_bits = header.max_bits;
    bool clear_on_overflow = header.clear_on_overflow;

    std::vector<DictEntry> dict;
    if (max_bits <= 24) dict.reserve(1ULL << max_bits);
    dict.resize(FIRST_CODE);

    uint32_t current_code = FIRST_CODE;
    uint8_t  bit_length = 9;
    bool     is_frozen = false;
    uint32_t old_code = EOF_CODE;
    uint8_t  first_char = 0;

    {
        BitReader br(in);

        auto read_code = [&]() -> std::expected<uint32_t, LZWError> {
            std::array<uint8_t, 4> data = { 0, 0, 0, 0 };
            if (!br.ReadBitSequence(data, bit_length))
                return std::unexpected(LZWError::InvalidFormat);
            return static_cast<uint32_t>(data[0])
                | (static_cast<uint32_t>(data[1]) << 8)
                | (static_cast<uint32_t>(data[2]) << 16)
                | (static_cast<uint32_t>(data[3]) << 24);
            };

        while (true) {
            auto code_res = read_code();
            if (!code_res) break;
            uint32_t code = code_res.value();

            if (code == EOF_CODE) break;

            if (code == CLEAR_CODE) {
                dict.clear();
                dict.resize(FIRST_CODE);
                current_code = FIRST_CODE;
                bit_length = 9;
                is_frozen = false;

                auto next = read_code();
                if (!next || next.value() == EOF_CODE) break;

                first_char = static_cast<uint8_t>(next.value());
                out.put(static_cast<char>(first_char));
                old_code = next.value();
                continue;
            }

            uint32_t curr = code;
            std::vector<uint8_t> stack;

            if (code >= current_code) {
                if (old_code == EOF_CODE) return std::unexpected(LZWError::InvalidFormat);
                curr = old_code;
                stack.push_back(first_char);
            }

            while (curr >= 256) {
                if (curr >= dict.size()) return std::unexpected(LZWError::InvalidFormat);
                stack.push_back(dict[curr].ch);
                curr = dict[curr].prefix;
            }

            first_char = static_cast<uint8_t>(curr);
            stack.push_back(first_char);

            for (auto it = stack.rbegin(); it != stack.rend(); ++it)
                out.put(static_cast<char>(*it));

            if (!is_frozen && old_code != EOF_CODE) {
                dict.push_back({ old_code, first_char });
                current_code++;
                if (current_code == (1ULL << bit_length) - 1) {
                    if (bit_length < max_bits) bit_length++;
                    else if (!clear_on_overflow) is_frozen = true;
                }
            }
            old_code = code;
        }
    }

    out.close();

    if (!temp_.path.empty()) {
        if (!TransformSplitting::ApplyReverse(temp_.path, out_path, header.use_bwt, header.use_mtf))
            return std::unexpected(LZWError::TransformFailed);
    }

    return {};
}