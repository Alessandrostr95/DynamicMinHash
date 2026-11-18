#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <set>
#include <type_traits>
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
 * @brief Bounded entries stored in an unordered, reserved vector.
 * @tparam Entry Equality-comparable, totally ordered entry type.
 * @tparam Unique Whether equal entries must be deduplicated. False retains
 * hash multiplicities for legal streams without a duplicate search.
 * @note Searches and extrema scans take linear time in the active size.
 * Threshold admission and fault/recovery decisions belong to the owning sketch.
 */
template<class Entry, bool Unique = true>
class ArrayBuffer {
public:
    /**
     * @brief Creates an empty buffer with reserved storage.
     * @param capacity Maximum number of entries to retain.
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
     * @brief Inserts an entry, retaining the smallest entries if full.
     * @param entry Entry to consider; equal entries consume capacity when !Unique.
     * @return True if storage changed; false for a duplicate when Unique, or a
     * value at least as large as the maximum of an already-full buffer.
     * @note The caller must apply the sketch's retained threshold first.
     */
    bool insert(const Entry& entry) {
        if constexpr (Unique) {
            if (std::find(entries_.begin(), entries_.end(), entry) != entries_.end()) {
                return false;
            }
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
     * @brief Removes one matching entry, filling its slot with the last entry.
     * @param entry Value to remove, even if multiple equal entries are stored.
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
     * @brief Scans for the smallest active entry.
     * @return Reference to the minimum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& minimum() const {
        return *std::min_element(entries_.begin(), entries_.end());
    }
    /**
     * @brief Scans for the largest active entry.
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
 * @brief Bounded entries stored in an ordered tree.
 * @tparam Entry Equality-comparable, totally ordered entry type.
 * @tparam Unique Selects set for distinct entries or multiset for hash multiplicities.
 * @note Updates take logarithmic time in the buffer size. Threshold admission
 * and fault/recovery decisions belong to the owning sketch.
 */
template<class Entry, bool Unique = true>
class TreeBuffer {
public:
    /**
     * @brief Creates an empty tree buffer.
     * @param capacity Maximum number of entries to retain.
     * @pre capacity is positive; the owning sketch validates this.
     */
    explicit TreeBuffer(std::size_t capacity) : capacity_(capacity) {}

    /// Returns true when the tree contains no entries.
    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    /// Returns the number of stored entries, counting multiplicities when !Unique.
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    /// Removes all tree nodes while preserving the configured capacity.
    void clear() noexcept { entries_.clear(); }

    /**
     * @brief Inserts an entry, removing the maximum if capacity is exceeded.
     * @param entry Value to consider; equal entries consume capacity when !Unique.
     * @return True if a new entry was retained; false for a duplicate when Unique
     * or an entry discarded as the maximum of an over-capacity buffer.
     * @note The caller must apply the sketch's retained threshold first.
     */
    bool insert(const Entry& entry) {
        const auto position = [&] {
            if constexpr (Unique) {
                const auto [found, inserted] = entries_.insert(entry);
                return inserted ? found : entries_.end();
            } else {
                return entries_.insert(entry);
            }
        }();
        if (position == entries_.end()) {
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
     * @brief Removes exactly one matching entry, preserving other equal entries.
     * @param entry Value to remove.
     * @return True if an entry existed and was removed; false if absent.
     */
    bool erase(const Entry& entry) {
        const auto found = entries_.find(entry);
        if (found == entries_.end()) {
            return false;
        }
        entries_.erase(found);
        return true;
    }

    /**
     * @brief Returns the smallest stored entry.
     * @return Reference to the minimum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& minimum() const { return *entries_.begin(); }
    /**
     * @brief Returns the largest stored entry.
     * @return Reference to the maximum; do not retain it across buffer mutations.
     * @pre The buffer is nonempty.
     */
    [[nodiscard]] const Entry& maximum() const { return *entries_.rbegin(); }

private:
    std::size_t capacity_;
    std::conditional_t<Unique, std::set<Entry>, std::multiset<Entry>> entries_;
};

} // namespace buffered_minhash::detail
