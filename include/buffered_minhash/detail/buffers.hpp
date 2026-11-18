#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <iterator>
#include <set>
#include <utility>
#include <vector>

namespace buffered_minhash::detail {

template<class Element, class HashValue>
struct Entry {
    HashValue hash;
    Element element;

    auto operator<=>(const Entry&) const = default;
};

// These internal containers hold distinct entries, with a bounded capacity.
// Thresholds and recovery belong to the sketch, not to the container policy.
template<class Entry>
class ArrayBuffer {
public:
    explicit ArrayBuffer(std::size_t capacity) : capacity_(capacity) {
        entries_.reserve(capacity);
    }

    ArrayBuffer(const ArrayBuffer& other) : ArrayBuffer(other.capacity_) {
        entries_.assign(other.entries_.begin(), other.entries_.end());
    }
    ArrayBuffer& operator=(const ArrayBuffer& other) {
        if (this != &other) {
            ArrayBuffer copy(other);
            *this = std::move(copy);
        }
        return *this;
    }
    ArrayBuffer(ArrayBuffer&&) noexcept = default;
    ArrayBuffer& operator=(ArrayBuffer&&) noexcept = default;

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    void clear() noexcept { entries_.clear(); }

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

    bool erase(const Entry& entry) {
        auto found = std::find(entries_.begin(), entries_.end(), entry);
        if (found == entries_.end()) {
            return false;
        }
        *found = entries_.back();
        entries_.pop_back();
        return true;
    }

    // The caller must ensure the buffer is nonempty.
    [[nodiscard]] const Entry& minimum() const {
        return *std::min_element(entries_.begin(), entries_.end());
    }
    [[nodiscard]] const Entry& maximum() const {
        return *std::max_element(entries_.begin(), entries_.end());
    }

private:
    std::size_t capacity_;
    std::vector<Entry> entries_;
};

template<class Entry>
class TreeBuffer {
public:
    explicit TreeBuffer(std::size_t capacity) : capacity_(capacity) {}

    [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    void clear() noexcept { entries_.clear(); }

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

    bool erase(const Entry& entry) { return entries_.erase(entry) != 0; }

    [[nodiscard]] const Entry& minimum() const { return *entries_.begin(); }
    [[nodiscard]] const Entry& maximum() const { return *entries_.rbegin(); }

private:
    std::size_t capacity_;
    std::set<Entry> entries_;
};

} // namespace buffered_minhash::detail
