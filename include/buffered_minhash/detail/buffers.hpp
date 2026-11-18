#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

/// Internal storage policies; not part of the supported application-facing API.
namespace buffered_minhash::detail {

/**
 * @brief Identifies a buffered element and orders it by hash, then identity.
 * @tparam Element Exact element identity type.
 * @tparam HashValue Hash output type.
 */
template<class Element, class HashValue>
struct Entry {
    HashValue hash;   ///< Primary ordering key and candidate signature value.
    Element element; ///< Identity used to distinguish collisions and duplicates.

    /**
     * @brief Compares entries lexicographically by hash and then element.
     * @param other Entry to compare.
     * @return The ordering of this entry relative to other.
     */
    auto operator<=>(const Entry& other) const = default;
};

/**
 * @brief Bounded set of distinct entries stored in an unordered, reserved vector.
 * @tparam Entry Equality-comparable, totally ordered buffered pair type.
 * @note Searches and extrema scans take linear time in the active size.
 * Threshold admission and fault/recovery decisions belong to the owning sketch.
 */
template<class Entry>
class ArrayBuffer {
public:
    /**
     * @brief Creates an empty buffer with reserved storage.
     * @param capacity Maximum number of distinct entries to retain.
     * @pre capacity is positive; the owning sketch validates this.
     */
    explicit ArrayBuffer(std::size_t capacity) : capacity_(capacity) {
        entries_.reserve(capacity);
    }

    /**
     * @brief Copies active entries while preserving reserved capacity.
     * @param other Buffer whose entries and capacity are copied independently.
     */
    ArrayBuffer(const ArrayBuffer& other) : ArrayBuffer(other.capacity_) {
        entries_.assign(other.entries_.begin(), other.entries_.end());
    }
    /**
     * @brief Replaces this buffer with an independent copy.
     * @param other Source buffer.
     * @return This buffer.
     */
    ArrayBuffer& operator=(const ArrayBuffer& other) {
        if (this != &other) {
            ArrayBuffer copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    /// Transfers storage; the source is only used for assignment or destruction.
    ArrayBuffer(ArrayBuffer&&) noexcept = default;
    /// Transfers storage; the source is only used for assignment or destruction.
    ArrayBuffer& operator=(ArrayBuffer&&) noexcept = default;

    /// Returns true when there are no active entries.
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    /// Returns the number of active entries, excluding reserved storage.
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    /// Removes all active entries while retaining the reserved capacity.
    void clear() noexcept { entries_.clear(); }

    /**
     * @brief Inserts a distinct entry, retaining the smallest entries if full.
     * @param entry Pair to consider; duplicates do not consume capacity.
     * @return True if a new entry was retained; false for a duplicate or an entry
     * greater than the maximum of an already-full buffer.
     * @note The caller must apply the sketch's retained threshold first.
     */
    bool insert(const Entry& entry) {
        if (std::find(entries_.begin(), entries_.end(), entry) != entries_.end()) {
            return false;
        }
        if (size() < capacity_) {
            entries_.push_back(entry);
        } else {
            auto maximum = std::max_element(entries_.begin(), entries_.end());
            if (entry >= *maximum) {
                return false;
            }
            *maximum = entry;
        }
        return true;
    }

    /**
     * @brief Removes a pair by exact identity, filling its slot with the last pair.
     * @param entry Exact (hash, element) pair to remove.
     * @return True if an entry was removed; false if absent.
     */
    bool erase(const Entry& entry) {
        auto found = std::find(entries_.begin(), entries_.end(), entry);
        if (found == entries_.end()) {
            return false;
        }
        *found = entries_.back();
        entries_.pop_back();
        return true;
    }

    /**
     * @brief Scans for the smallest active pair.
     * @return Reference to the minimum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& minimum() const {
        return *std::min_element(entries_.begin(), entries_.end());
    }
    /**
     * @brief Scans for the largest active pair.
     * @return Reference to the maximum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& maximum() const {
        return *std::max_element(entries_.begin(), entries_.end());
    }

private:
    std::size_t capacity_;
    std::vector<Entry> entries_;
};

/**
 * @brief Bounded set of distinct pairs stored in an ordered tree.
 * @tparam Entry Equality-comparable, totally ordered buffered pair type.
 * @note Updates take logarithmic time in the buffer size. Threshold admission
 * and fault/recovery decisions belong to the owning sketch.
 */
template<class Entry>
class TreeBuffer {
public:
    /**
     * @brief Creates an empty tree buffer.
     * @param capacity Maximum number of distinct entries to retain.
     * @pre capacity is positive; the owning sketch validates this.
     */
    explicit TreeBuffer(std::size_t capacity) : capacity_(capacity) {}

    /// Returns true when the tree contains no entries.
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    /// Returns the number of stored distinct entries.
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    /// Removes all tree nodes while preserving the configured capacity.
    void clear() noexcept { entries_.clear(); }

    /**
     * @brief Inserts a distinct pair, removing the maximum if capacity is exceeded.
     * @param entry Pair to consider; duplicates do not consume capacity.
     * @return True if a new entry was retained; false for a duplicate or an entry
     * discarded as the maximum of an over-capacity buffer.
     * @note The caller must apply the sketch's retained threshold first.
     */
    bool insert(const Entry& entry) {
        const auto [position, inserted] = entries_.insert(entry);
        if (!inserted) {
            return false;
        }
        if (size() > capacity_) {
            auto maximum = std::prev(entries_.end());
            const bool retained = position != maximum;
            entries_.erase(maximum);
            return retained;
        }
        return true;
    }

    /**
     * @brief Removes a pair by its exact hash and element identity.
     * @param entry Pair to remove.
     * @return True if the pair existed and was removed; false if absent.
     */
    bool erase(const Entry& entry) { return entries_.erase(entry) != 0; }

    /**
     * @brief Returns the smallest stored pair.
     * @return Reference to the minimum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& minimum() const { return *entries_.begin(); }
    /**
     * @brief Returns the largest stored pair.
     * @return Reference to the maximum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& maximum() const { return *entries_.rbegin(); }

private:
    std::size_t capacity_;
    std::set<Entry> entries_;
};

} // namespace buffered_minhash::detail
