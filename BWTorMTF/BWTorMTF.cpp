#include "BWTorMTF.hpp"
#include <numeric>
#include <algorithm>
#include <array>
#include <iostream>
#include <print>

std::string_view TransformError_to_string(TransformError err) {
    switch (err) {
    case TransformError::EmptyInput:   return "Порожній вхідний блок для перетворення.";
    case TransformError::InvalidIndex: return "Некоректний index для зворотного BWT.";
    default:                           return "Невідома помилка перетворення.";
    }
}

std::expected<std::vector<uint8_t>, TransformError> BWT::Encode(std::span<const uint8_t> input, uint32_t& out_primary_index) {
    if (input.empty()) {
        std::println(stderr, "BWT Encode Error: {}", TransformError_to_string(TransformError::EmptyInput));
        return std::unexpected(TransformError::EmptyInput);
    }
    if (input.size() == 1) {
        out_primary_index = 0;
        return std::vector<uint8_t>{input[0]};
    }

    const uint32_t N = static_cast<uint32_t>(input.size());

    std::vector<uint32_t> sa(N), rank(N), temp_rank(N);
    for (uint32_t i = 0; i < N; ++i) {
        sa[i] = i;
        rank[i] = input[i];
    }

    for (uint32_t k = 1; k < N; k *= 2) {
        auto cmp_sort = [&](uint32_t a, uint32_t b) {
            if (rank[a] != rank[b]) return rank[a] < rank[b];
            uint32_t rx = rank[(a + k) % N];
            uint32_t ry = rank[(b + k) % N];
            if (rx != ry) return rx < ry;
            return a < b; 
            };
        std::sort(sa.begin(), sa.end(), cmp_sort);

        auto equal_rank = [&](uint32_t a, uint32_t b) {
            return rank[a] == rank[b] && rank[(a + k) % N] == rank[(b + k) % N];
            };

        temp_rank[sa[0]] = 0;
        for (uint32_t i = 1; i < N; ++i) {
            temp_rank[sa[i]] = temp_rank[sa[i - 1]] + (equal_rank(sa[i - 1], sa[i]) ? 0 : 1);
        }
        rank = temp_rank;

        if (rank[sa.back()] == N - 1) break;
    }

    std::vector<uint8_t> L(N);
    for (uint32_t i = 0; i < N; ++i) {
        if (sa[i] == 0) {
            out_primary_index = i;
            L[i] = input[N - 1];
        }
        else {
            L[i] = input[sa[i] - 1];
        }
    }
    return L;
}

std::expected<std::vector<uint8_t>, TransformError> BWT::Decode(std::span<const uint8_t> input, uint32_t primary_index) {
    if (input.empty()) {
		std::println(stderr, "BWT Decode Error: {}", TransformError_to_string(TransformError::EmptyInput));
        return std::unexpected(TransformError::EmptyInput);
    }
    const uint32_t N = static_cast<uint32_t>(input.size());
    if (primary_index >= N) {
		std::println(stderr, "BWT Decode Error: {}", TransformError_to_string(TransformError::InvalidIndex));
        return std::unexpected(TransformError::InvalidIndex);
    }

    std::vector<uint32_t> counts(256, 0);
    for (uint8_t c : input) counts[c]++;

    std::vector<uint32_t> starts(256, 0);
    uint32_t sum = 0;
    for (int i = 0; i < 256; ++i) {
        starts[i] = sum;
        sum += counts[i];
    }

    std::vector<uint32_t> T(N);
    for (uint32_t i = 0; i < N; ++i) {
        T[starts[input[i]]++] = i;
    }

    std::vector<uint8_t> decoded(N);
    uint32_t curr = primary_index;
    for (uint32_t i = 0; i < N; ++i) {
        curr = T[curr];
        decoded[i] = input[curr];
    }
    return decoded;
}

std::expected<std::vector<uint8_t>, TransformError> MTF::Encode(std::span<const uint8_t> input) {
    if (input.empty()) {
		std::println(stderr, "MTF Encode Error: {}", TransformError_to_string(TransformError::EmptyInput));
        return std::unexpected(TransformError::EmptyInput);
    }
    std::vector<uint8_t> output(input.size());
    std::array<uint8_t, 256> alphabet;
    std::iota(alphabet.begin(), alphabet.end(), 0);

    for (size_t i = 0; i < input.size(); ++i) {
        uint8_t c = input[i];
        uint8_t pos = 0;
        while (alphabet[pos] != c) pos++;

        output[i] = pos;
        for (uint8_t j = pos; j > 0; --j) alphabet[j] = alphabet[j - 1];
        alphabet[0] = c;
    }
    return output;
}

std::expected<std::vector<uint8_t>, TransformError> MTF::Decode(std::span<const uint8_t> input) {
    if (input.empty()) {
		std::println(stderr, "MTF Decode Error: {}", TransformError_to_string(TransformError::EmptyInput));
        return std::unexpected(TransformError::EmptyInput);
    }
    std::vector<uint8_t> output(input.size());
    std::array<uint8_t, 256> alphabet;
    std::iota(alphabet.begin(), alphabet.end(), 0);

    for (size_t i = 0; i < input.size(); ++i) {
        uint8_t pos = input[i];
        uint8_t c = alphabet[pos];

        output[i] = c;
        for (uint8_t j = pos; j > 0; --j) alphabet[j] = alphabet[j - 1];
        alphabet[0] = c;
    }
    return output;
}