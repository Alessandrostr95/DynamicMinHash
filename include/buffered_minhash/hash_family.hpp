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

/**
 * @brief Restricts element identities to the currently supported unsigned widths.
 * @tparam T Candidate type; must be exactly uint32_t or uint64_t.
 */
template<class T>
concept ElementType = std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>;

/**
 * @brief Restricts hash outputs independently of the element identity type.
 * @tparam T Candidate output type; must be exactly uint32_t or uint64_t.
 */
template<class T>
concept HashValueType = std::same_as<T, std::uint32_t> || std::same_as<T, std::uint64_t>;

/**
 * @brief Hashes unsigned elements by XORing tables indexed by 4-bit chunks.
 * @tparam Element Input identity type: uint32_t (default) or uint64_t.
 * @tparam Value Output type: uint64_t (default) or uint32_t.
 *
 * Uses eight tables of sixteen values for 32-bit inputs, or sixteen such tables
 * for 64-bit inputs. Tables are initialized reproducibly from a seed and remain
 * unchanged during evaluation. Output width need not match input width.
 * @note This is the library's engineering default; no particular statistical
 * quality or probabilistic performance guarantee is asserted for this family.
 */
template<ElementType Element = std::uint32_t, HashValueType Value = std::uint64_t>
class TabulationHash {
public:
    /// Element identity type accepted by this function.
    using element_type = Element;
    /// Output value type used for signature coordinates.
    using hash_type = Value;

    /**
     * @brief Initializes all lookup tables using a seeded mt19937_64 generator.
     * @param seed Seed defining this concrete function's table contents.
     * @note The same seed and template arguments reproduce the same function.
     */
    explicit TabulationHash(std::uint64_t seed) {
        std::mt19937_64 generator(seed);
        for (auto& table : tables_) {
            for (auto& value : table) {
                value = static_cast<Value>(generator());
            }
        }
    }

    /**
     * @brief Evaluates the function without changing its configuration.
     * @param element Element identity to hash; every representable value is valid.
     * @return Hash output, which may include zero or the maximum Value value.
     */
    [[nodiscard]] Value operator()(Element element) const noexcept {
        Value value = 0;
        for (std::size_t i = 0; i < tables_.size(); ++i) {
            const auto nibble = static_cast<std::size_t>((element >> (4 * i)) & Element{15});
            value ^= tables_[i][nibble];
        }
        return value;
    }

    /**
     * @brief Compares the complete table contents of two functions.
     * @param other Function to compare, with the same input/output types.
     * @return True if all table entries are equal.
     */
    bool operator==(const TabulationHash& other) const = default;

private:
    std::array<std::array<Value, 16>, std::numeric_limits<Element>::digits / 4> tables_;
};

/**
 * @brief Requirements for a concrete hash function used by BufferedMinhash.
 * @tparam Hash Copyable, equality-comparable type declaring element_type and
 * hash_type, and callable through a const reference with the exact output type.
 *
 * Beyond the checked type requirements, evaluation must be deterministic and
 * preserve configuration. Equality must compare the complete configuration, not
 * merely an algorithm name or seed label. Externally mutable referenced state
 * violates this contract. Concurrent use requires safe const evaluation.
 */
template<class Hash>
concept HashFunction = std::copy_constructible<Hash> && std::equality_comparable<Hash> &&
    requires(const Hash& hash, typename Hash::element_type element) {
        requires ElementType<typename Hash::element_type>;
        requires HashValueType<typename Hash::hash_type>;
        { hash(element) } -> std::same_as<typename Hash::hash_type>;
    };

/**
 * @brief Owns an immutable, nonempty, ordered collection of concrete hash functions.
 * @tparam Hash Function value type satisfying HashFunction.
 *
 * Share this object through shared_ptr<const HashFamily<Hash>> to use identical
 * hash functions across sketches without copying tables. Coordinate order is
 * significant for signature compatibility. No virtual dispatch is required.
 */
template<HashFunction Hash>
class HashFamily {
public:
    /// Concrete type of every function in this configuration.
    using function_type = Hash;
    /// Identity type accepted by all stored functions.
    using element_type = typename Hash::element_type;
    /// Output type produced by all stored functions.
    using hash_type = typename Hash::hash_type;

    /**
     * @brief Takes ownership of an ordered collection of hash functions.
     * @param functions Nonempty vector of configured function values. Pass an
     * rvalue to transfer its storage; an lvalue is copied into the parameter.
     * @throws std::invalid_argument If functions is empty.
     * @note The vector becomes immutable; all function configuration must be
     * self-contained or otherwise satisfy the HashFunction immutability contract.
     */
    explicit HashFamily(std::vector<Hash> functions) : functions_(std::move(functions)) {
        if (functions_.empty()) {
            throw std::invalid_argument("a hash family must contain at least one function");
        }
    }

    /**
     * @brief Returns the number of configured functions.
     * @return Positive function count, also the signature length k.
     */
    [[nodiscard]] std::size_t size() const noexcept { return functions_.size(); }

    /**
     * @brief Exposes the ordered functions without copying them.
     * @return Read-only view valid for the lifetime of this configuration object.
     */
    [[nodiscard]] std::span<const Hash> functions() const noexcept { return functions_; }

    /**
     * @brief Accesses the function corresponding to one signature coordinate.
     * @param index Zero-based coordinate index.
     * @return Const function reference valid for this configuration's lifetime.
     * @pre index is less than size(); access is not bounds-checked.
     */
    [[nodiscard]] const Hash& operator[](std::size_t index) const noexcept {
        return functions_[index];
    }

    /**
     * @brief Compares function counts and complete configurations in order.
     * @param other Configuration with the same concrete hash type.
     * @return True if both contain equal functions in the same order.
     */
    bool operator==(const HashFamily& other) const { return functions_ == other.functions_; }

private:
    // Const storage also prevents mutation through a separate non-const owner.
    const std::vector<Hash> functions_;
};

/**
 * @brief Creates a shared immutable hash configuration from a reproducible seed.
 * @tparam Element Input identity type; defaults to uint32_t.
 * @tparam Hash Function type with matching element_type and a uint64_t seed
 * constructor; defaults to TabulationHash<Element> with uint64_t outputs.
 * @param count Number of functions to generate (k); must be positive.
 * @param seed Seed for the mt19937_64 generator supplying per-function seeds.
 * @return Shared ownership of the new ordered hash configuration.
 * @throws std::invalid_argument If count is zero.
 * @note Reproducibility requires deterministic construction of custom hashes.
 * Reuse the returned handle to share tables across compatible sketches.
 */
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

/**
 * @brief Creates a shared hash configuration with a seed from random_device.
 * @tparam Element Input identity type; defaults to uint32_t.
 * @tparam Hash Function type with matching element_type and a uint64_t seed
 * constructor; defaults to TabulationHash<Element> with uint64_t outputs.
 * @param count Number of functions to generate (k); must be positive.
 * @return Shared ownership of a newly generated ordered hash configuration.
 * @throws std::invalid_argument If count is zero, assuming seed generation succeeds.
 * @note Independent calls need not produce compatible configurations. Use the
 * seeded overload for reproducibility or reuse a handle to share functions.
 */
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
