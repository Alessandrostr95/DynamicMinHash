#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace buffered_minhash {

template<class T>
concept ElementType = std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>;

template<class T>
concept HashValueType = std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>;

// Nibble tabulation: 8 tables for 32-bit elements, 16 for 64-bit elements.
// Output width is independent of element width. This is an engineering default;
// the sketch does not assume a particular statistical guarantee for this family.
template<ElementType Element = std::uint32_t, HashValueType Value = std::uint64_t>
class TabulationHash {
public:
    using element_type = Element;
    using hash_type = Value;

    explicit TabulationHash(std::uint64_t seed) {
        std::mt19937_64 generator(seed);
        for (auto& table : tables_) {
            for (auto& value : table) {
                value = static_cast<Value>(generator());
            }
        }
    }

    [[nodiscard]] Value operator()(Element element) const noexcept {
        Value value = 0;
        for (std::size_t i = 0; i < tables_.size(); ++i) {
            const auto nibble = static_cast<std::size_t>((element >> (4 * i)) & Element{15});
            value ^= tables_[i][nibble];
        }
        return value;
    }

    bool operator==(const TabulationHash&) const = default;

private:
    std::array<std::array<Value, 16>, std::numeric_limits<Element>::digits / 4> tables_;
};

// Custom hashes must behave as immutable, deterministic value objects. Equality
// must compare their full configuration, not just the algorithm or a seed label.
template<class Hash>
concept HashFunction = std::copy_constructible<Hash> && std::equality_comparable<Hash> &&
    requires(const Hash& hash, typename Hash::element_type element) {
        requires ElementType<typename Hash::element_type>;
        requires HashValueType<typename Hash::hash_type>;
        { hash(element) } -> std::same_as<typename Hash::hash_type>;
    };

template<HashFunction Hash>
class HashFamily {
public:
    using function_type = Hash;
    using element_type = typename Hash::element_type;
    using hash_type = typename Hash::hash_type;

    explicit HashFamily(std::vector<Hash> functions) : functions_(std::move(functions)) {
        if (functions_.empty()) {
            throw std::invalid_argument("a hash family must contain at least one function");
        }
    }

    [[nodiscard]] std::size_t size() const noexcept { return functions_.size(); }
    [[nodiscard]] std::span<const Hash> functions() const noexcept { return functions_; }
    [[nodiscard]] const Hash& operator[](std::size_t index) const noexcept {
        return functions_[index];
    }

    bool operator==(const HashFamily& other) const { return functions_ == other.functions_; }

private:
    // Const storage also prevents mutation through a separate non-const owner.
    const std::vector<Hash> functions_;
};

template<ElementType Element = std::uint32_t, class Hash = TabulationHash<Element>>
    requires HashFunction<Hash> && std::same_as<typename Hash::element_type, Element> &&
             std::constructible_from<Hash, std::uint64_t>
[[nodiscard]] std::shared_ptr<const HashFamily<Hash>>
make_hash_family(std::size_t count, std::uint64_t seed) {
    if (count == 0) {
        throw std::invalid_argument("the signature length must be positive");
    }
    std::mt19937_64 generator(seed);
    std::vector<Hash> functions;
    functions.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        functions.emplace_back(generator());
    }
    return std::make_shared<const HashFamily<Hash>>(std::move(functions));
}

template<ElementType Element = std::uint32_t, class Hash = TabulationHash<Element>>
    requires HashFunction<Hash> && std::same_as<typename Hash::element_type, Element> &&
             std::constructible_from<Hash, std::uint64_t>
[[nodiscard]] std::shared_ptr<const HashFamily<Hash>> make_hash_family(std::size_t count) {
    std::random_device source;
    const auto seed = (static_cast<std::uint64_t>(source()) << 32) ^
                      static_cast<std::uint64_t>(source());
    return make_hash_family<Element, Hash>(count, seed);
}

} // namespace buffered_minhash
