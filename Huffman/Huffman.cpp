#include "Huffman.hpp"
#include "../BitStream/BitStream.hpp"
#include "../BWTorMTF/BWTorMTFSplitting.hpp"
#include <fstream>
#include <queue>

namespace {
    struct TempFile {
        std::filesystem::path path;
        ~TempFile() {
            if (!path.empty() && std::filesystem::exists(path))
                std::filesystem::remove(path);
        }
    };
}

std::string_view HuffmanError_to_string(HuffmanError err) {
    switch (err) {
    case HuffmanError::FileNotFound:    return "Файл не знайдено за вказаним шляхом.";
    case HuffmanError::FileReadError:   return "Помилка доступу: не вдалося прочитати вхідний файл.";
    case HuffmanError::FileWriteError:  return "Помилка запису: не вдалося зберегти вихідний файл.";
    case HuffmanError::InvalidFormat:   return "Некоректний формат: файл не є архівом Гаффмана або пошкоджений.";
    case HuffmanError::EmptyFile:       return "Вхідний файл порожній. Стиснення неможливе.";
    case HuffmanError::UserCancelled:   return "Операцію скасовано користувачем.";
    case HuffmanError::FileSameAsInput: return "Вихідний файл не може бути тим самим, що і вхідний.";
    case HuffmanError::TransformFailed: return "Помилка при застосуванні перетворень BWT/MTF.";
    default:                            return "Сталася невідома помилка при роботі з архіватором.";
    }
}

std::vector<uint8_t> HuffmanCoder::PackBits(const std::vector<bool>& bits) {
    std::vector<uint8_t> bytes((bits.size() + 7) / 8, 0);
    for (size_t i = 0; i < bits.size(); ++i)
        if (bits[i]) bytes[i / 8] |= (1 << (i % 8));
    return bytes;
}

void HuffmanCoder::BuildCodes(Node* node, std::vector<bool>& current_path, std::array<Code, 256>& codes) {
    if (node->is_leaf()) {
        codes[node->symbol] = { PackBits(current_path), current_path.size() };
        return;
    }
    current_path.push_back(false);
    BuildCodes(node->left, current_path, codes);
    current_path.pop_back();

    current_path.push_back(true);
    BuildCodes(node->right, current_path, codes);
    current_path.pop_back();
}

std::expected<std::string, HuffmanError> HuffmanCoder::ExtractOriginalFilename(const std::filesystem::path& in_path) {
    std::ifstream in(in_path, std::ios::binary);
    if (!in) return std::unexpected(HuffmanError::FileNotFound);
    uint8_t name_len = 0;
    if (!in.read(reinterpret_cast<char*>(&name_len), 1)) return std::unexpected(HuffmanError::InvalidFormat);
    if (name_len == 0) return "";
    std::string orig_name(name_len, '\0');
    if (!in.read(orig_name.data(), name_len)) return std::unexpected(HuffmanError::InvalidFormat);
    return orig_name;
}

std::expected<HuffmanStats, HuffmanError> HuffmanCoder::Compress(
    const std::filesystem::path& in_path, std::filesystem::path out_path,
    bool use_bwt, bool use_mtf)
{
    if (out_path.empty()) out_path = in_path.string() + ".huff";

    std::filesystem::path data_to_compress = in_path;
    TempFile temp_;

    if (use_bwt || use_mtf) {
        auto temp_file = std::filesystem::temp_directory_path() / (in_path.filename().string() + ".huff.tmp");
        if (!TransformSplitting::ApplyForward(in_path, temp_file, use_bwt, use_mtf))
            return std::unexpected(HuffmanError::TransformFailed);
        data_to_compress = temp_file;
        temp_.path = temp_file;
    }

    std::ifstream in(data_to_compress, std::ios::binary);
    if (!in) return std::unexpected(HuffmanError::FileNotFound);

    std::array<uint32_t, 256> freqs = { 0 };
    std::vector<char> buf(256 * 1024);
    uint32_t total_bytes = 0;
    uint32_t unique_count = 0;

    while (in.read(buf.data(), buf.size()) || in.gcount() > 0) {
        for (int i = 0; i < in.gcount(); ++i) {
            uint8_t c = static_cast<uint8_t>(buf[i]);
            if (freqs[c] == 0) unique_count++;
            freqs[c]++;
            total_bytes++;
        }
    }

    if (total_bytes == 0) return std::unexpected(HuffmanError::EmptyFile);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) return std::unexpected(HuffmanError::FileWriteError);

    std::string orig_name = in_path.filename().string();
    if (orig_name.length() > 255) orig_name = orig_name.substr(0, 255);
    uint8_t name_len = static_cast<uint8_t>(orig_name.length());
    out.write(reinterpret_cast<const char*>(&name_len), 1);
    out.write(orig_name.data(), name_len);

    bool is_single_symbol = (unique_count == 1);
    uint8_t transform_flags = (use_bwt ? 1 : 0) | (use_mtf ? 2 : 0) | (is_single_symbol ? 4 : 0);
    out.write(reinterpret_cast<const char*>(&transform_flags), 1);

    uintmax_t meta_size = 1 + name_len + 1 + 32;

    uint8_t bitmask[32] = { 0 };
    for (int i = 0; i < 256; ++i)
        if (freqs[i] > 0) bitmask[i / 8] |= (1 << (i % 8));
    out.write(reinterpret_cast<char*>(bitmask), 32);

    for (int i = 0; i < 256; ++i) {
        if (freqs[i] > 0) {
            out.write(reinterpret_cast<char*>(&freqs[i]), sizeof(uint32_t));
            meta_size += sizeof(uint32_t);
        }
    }

    if (!is_single_symbol) {
        std::vector<std::unique_ptr<Node>> arena;
        std::priority_queue<Node*, std::vector<Node*>, CompareNode> pq;

        for (int i = 0; i < 256; ++i) {
            if (freqs[i] > 0) {
                arena.push_back(std::make_unique<Node>(static_cast<uint8_t>(i), freqs[i]));
                pq.push(arena.back().get());
            }
        }

        while (pq.size() > 1) {
            auto left = pq.top(); pq.pop();
            auto right = pq.top(); pq.pop();
            arena.push_back(std::make_unique<Node>(0, left->freq + right->freq));
            auto parent = arena.back().get();
            parent->left = left;
            parent->right = right;
            pq.push(parent);
        }

        std::array<Code, 256> codes;
        std::vector<bool> path;
        BuildCodes(pq.top(), path, codes);

        BitWriter bw(out);
        in.clear();
        in.seekg(0);
        while (in.read(buf.data(), buf.size()) || in.gcount() > 0) {
            for (int i = 0; i < in.gcount(); ++i) {
                const auto& code = codes[static_cast<uint8_t>(buf[i])];
                if (code.bit_length > 0) {
                    if (!bw.WriteBitSequence(code.data, code.bit_length))
                        return std::unexpected(HuffmanError::FileWriteError);
                }
            }
        }
    }

    out.close();

    HuffmanStats stats;
    stats.original_size = std::filesystem::file_size(in_path);
    stats.compressed_size = std::filesystem::file_size(out_path);
    stats.metadata_size = meta_size;
    return stats;
}

std::expected<void, HuffmanError> HuffmanCoder::Decompress(
    const std::filesystem::path& in_path, std::filesystem::path out_path)
{
    std::ifstream in(in_path, std::ios::binary);
    if (!in) return std::unexpected(HuffmanError::FileNotFound);

    uint8_t name_len = 0;
    if (!in.read(reinterpret_cast<char*>(&name_len), 1)) return std::unexpected(HuffmanError::InvalidFormat);
    in.seekg(name_len, std::ios::cur);

    uint8_t transform_flags = 0;
    if (!in.read(reinterpret_cast<char*>(&transform_flags), 1)) return std::unexpected(HuffmanError::InvalidFormat);

    bool use_bwt = (transform_flags & 1) != 0;
    bool use_mtf = (transform_flags & 2) != 0;
    bool is_single_symbol = (transform_flags & 4) != 0;

    uint8_t bitmask[32];
    if (!in.read(reinterpret_cast<char*>(bitmask), 32)) return std::unexpected(HuffmanError::InvalidFormat);

    std::array<uint32_t, 256> freqs = { 0 };
    uint32_t total_bytes = 0;
    uint32_t unique_count = 0;
    uint8_t  the_only_symbol = 0;

    for (int i = 0; i < 256; ++i) {
        if (bitmask[i / 8] & (1 << (i % 8))) {
            if (!in.read(reinterpret_cast<char*>(&freqs[i]), sizeof(uint32_t)))
                return std::unexpected(HuffmanError::InvalidFormat);
            total_bytes += freqs[i];
            unique_count++;
            the_only_symbol = static_cast<uint8_t>(i);
        }
    }

    if (total_bytes == 0) return std::unexpected(HuffmanError::EmptyFile);

    if (is_single_symbol && unique_count != 1)
        return std::unexpected(HuffmanError::InvalidFormat);

    std::filesystem::path extracted_data_path = out_path;
    TempFile temp_;

    if (use_bwt || use_mtf) {
        auto temp_file = std::filesystem::temp_directory_path() / (out_path.filename().string() + ".huff.tmp");
        extracted_data_path = temp_file;
        temp_.path = temp_file;
    }

    std::ofstream out(extracted_data_path, std::ios::binary);
    if (!out) return std::unexpected(HuffmanError::FileWriteError);

    if (is_single_symbol) {
        constexpr uint32_t CHUNK = 65536;
        std::vector<char> chunk_buf(CHUNK, static_cast<char>(the_only_symbol));
        uint32_t remaining = total_bytes;
        while (remaining > 0) {
            uint32_t n = std::min(remaining, CHUNK);
            out.write(chunk_buf.data(), n);
            remaining -= n;
        }
    }
    else {
        std::vector<std::unique_ptr<Node>> arena;
        std::priority_queue<Node*, std::vector<Node*>, CompareNode> pq;

        for (int i = 0; i < 256; ++i) {
            if (freqs[i] > 0) {
                arena.push_back(std::make_unique<Node>(static_cast<uint8_t>(i), freqs[i]));
                pq.push(arena.back().get());
            }
        }

        while (pq.size() > 1) {
            auto left = pq.top(); pq.pop();
            auto right = pq.top(); pq.pop();
            arena.push_back(std::make_unique<Node>(0, left->freq + right->freq));
            auto parent = arena.back().get();
            parent->left = left;
            parent->right = right;
            pq.push(parent);
        }

        Node* root = pq.top();
        BitReader br(in);
        uint32_t decoded_bytes = 0;
        std::vector<uint8_t> bit(1);

        while (decoded_bytes < total_bytes) {
            Node* curr = root;
            while (!curr->is_leaf()) {
                auto res = br.ReadBitSequence(bit, 1);
                if (!res) return std::unexpected(HuffmanError::InvalidFormat);
                if (bit[0] & 1) curr = curr->right;
                else            curr = curr->left;
                bit[0] = 0;
            }
            out.put(static_cast<char>(curr->symbol));
            decoded_bytes++;
        }
    }

    out.close();

    if (!temp_.path.empty()) {
        if (!TransformSplitting::ApplyReverse(temp_.path, out_path, use_bwt, use_mtf))
            return std::unexpected(HuffmanError::TransformFailed);
    }

    return {};
}