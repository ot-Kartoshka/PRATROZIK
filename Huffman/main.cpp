#include "./Huffman.hpp"
#include <iostream>
#include <print>
#include <string>
#include <filesystem>

bool askUser(const std::string& filename) {
    std::print("Output filename not specified. Use '{}'? [y/n]: ", filename);
    std::string input;
    if (!std::getline(std::cin, input)) return false;
    if (!input.empty()) {
        char response = input[0];
        return (response == 'y' || response == 'Y');
    }
    return false;
}

void PrintHelp(const char* prog_name) {
    std::println("Usage:");
    std::println("  Compress:   {} -c <input_file> [output_file] [--bwt] [--mtf]", prog_name);
    std::println("  Decompress: {} -d <input_file> [output_file]", prog_name);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        PrintHelp(argv[0]);
        return 1;
    }

    std::string mode = argv[1];
    std::filesystem::path in_file;
    std::filesystem::path out_file;
    bool use_bwt = false;
    bool use_mtf = false;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--bwt") use_bwt = true;
        else if (arg == "--mtf") use_mtf = true;
        else if (arg[0] != '-') {
            if (in_file.empty()) in_file = arg;
            else if (out_file.empty()) out_file = arg;
            else { PrintHelp(argv[0]); return 1; }
        }
        else { PrintHelp(argv[0]); return 1; }
    }

    if (in_file.empty()) {
        std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::NoPathProvided));
        PrintHelp(argv[0]);
        return 1;
    }

    if (!std::filesystem::exists(in_file)) {
        std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::FileNotFound));
        return 1;
    }

    if (mode == "-c") {
        if (out_file.empty()) {
            out_file = in_file.string() + ".huff";
            std::println("Output file not provided. Creating: {}", out_file.string());
        }

        if (std::filesystem::exists(out_file)) {
            if (std::filesystem::equivalent(in_file, out_file)) {
                std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::FileSameAsInput));
                return 1;
            }
            std::print("Warning: Output file '{}' already exists. Overwrite? [y/n]: ", out_file.string());
            std::string confirm;
            if (!std::getline(std::cin, confirm) || confirm.empty() || (confirm[0] != 'y' && confirm[0] != 'Y')) {
                std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::UserCancelled));
                return 1;
            }
        }

        std::println("Compressing '{}' with BWT={}, MTF={}...", in_file.string(), use_bwt, use_mtf);

        auto result = HuffmanCoder::Compress(in_file, out_file, use_bwt, use_mtf);

        if (result) {
            const auto& stats = result.value();
            std::println("Success!");
            std::println("Original size:   {} bytes", stats.original_size);
            std::println("Compressed size: {} bytes", stats.compressed_size);
            std::println("Metadata size:   {} bytes ", stats.metadata_size);
            double ratio = static_cast<double>(stats.compressed_size) / stats.original_size * 100.0;
            std::println("Compression:     {:.2f}% of original", ratio);
        }
        else {
            std::println(stderr, "Error: {}", HuffmanError_to_string(result.error()));
            return 1;
        }
    }
    else if (mode == "-d") {
        if (out_file.empty()) {
            auto meta_res = HuffmanCoder::ExtractOriginalFilename(in_file);
            if (!meta_res || meta_res.value().empty()) {
                std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::InvalidFormat));
                return 1;
            }
            std::string suggestedName = meta_res.value();
            if (askUser(suggestedName)) out_file = suggestedName;
            else {
                std::print("Please enter output filename: ");
                std::string userFilename;
                std::getline(std::cin, userFilename);
                if (userFilename.empty()) {
                    std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::UserCancelled));
                    return 1;
                }
                out_file = userFilename;
            }
        }

        if (std::filesystem::exists(out_file)) {
            if (std::filesystem::equivalent(in_file, out_file)) {
                std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::FileSameAsInput));
                return 1;
            }
            std::print("Warning: File '{}' exists. Overwrite? [y/n]: ", out_file.string());
            std::string confirm;
            if (!std::getline(std::cin, confirm) || confirm.empty() || (confirm[0] != 'y' && confirm[0] != 'Y')) {
                std::println(stderr, "Error: {}", HuffmanError_to_string(HuffmanError::UserCancelled));
                return 1;
            }
        }

        auto result = HuffmanCoder::Decompress(in_file, out_file);
        if (result) std::println("Decompression successful!");
        else {
            std::println(stderr, "Error: {}", HuffmanError_to_string(result.error()));
            return 1;
        }
    }
    else { PrintHelp(argv[0]); return 1; }

    return 0;
}