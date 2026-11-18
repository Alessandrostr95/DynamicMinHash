#pragma once

#include "detail/buffers.hpp"
#include "hash_family.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace buffered_minhash {

enum class BufferKind { Array, Tree };
enum class UpdateResult { Ok, NeedsRecovery };
enum class SketchState { Ready, NeedsRecovery };

template<ElementType Element = std::uint32_t,
         BufferKind Kind = BufferKind::Array,
         class Hash = TabulationHash<Element>>
    requires HashFunction<Hash> && std::same_as<typename Hash::element_type, Element> &&
             (Kind == BufferKind::Array || Kind == BufferKind::Tree)
class BufferedMinhash {
public:
    using element_type = Element;
    using hash_type = typename Hash::hash_type;
    using hash_family_type = HashFamily<Hash>;
    using hash_family_ptr = std::shared_ptr<const hash_family_type>;
    using signature_view = std::span<const hash_type>;

    // The external configuration determines k; capacity is ell.
    BufferedMinhash(std::size_t capacity, hash_family_ptr hashes)
        : hashes_(std::move(hashes)), capacity_(capacity) {
        if (!hashes_ || capacity == 0) {
            throw std::invalid_argument("hash family must be non-null and capacity positive");
        }
        buffers_.reserve(hashes_->size());
        for (std::size_t i = 0; i < hashes_->size(); ++i) {
            buffers_.emplace_back(capacity);
        }
        thresholds_.resize(hashes_->size());
        signature_.resize(hashes_->size());
    }

    BufferedMinhash(std::size_t k, std::size_t capacity)
        requires std::constructible_from<Hash, std::uint64_t>
        : BufferedMinhash(capacity, make_hash_family<Element, Hash>(k)) {}

    BufferedMinhash(std::size_t k, std::size_t capacity, std::uint64_t seed)
        requires std::constructible_from<Hash, std::uint64_t>
        : BufferedMinhash(capacity, make_hash_family<Element, Hash>(k, seed)) {}

    BufferedMinhash(const BufferedMinhash&) = default;
    BufferedMinhash& operator=(const BufferedMinhash&) = default;

    BufferedMinhash(BufferedMinhash&& other) noexcept
        : hashes_(std::move(other.hashes_)),
          capacity_(std::exchange(other.capacity_, 0)),
          buffers_(std::move(other.buffers_)), thresholds_(std::move(other.thresholds_)),
          signature_(std::move(other.signature_)),
          state_(std::exchange(other.state_, SketchState::NeedsRecovery)) {}

    BufferedMinhash& operator=(BufferedMinhash&& other) noexcept {
        if (this != &other) {
            hashes_ = std::move(other.hashes_);
            capacity_ = std::exchange(other.capacity_, 0);
            buffers_ = std::move(other.buffers_);
            thresholds_ = std::move(other.thresholds_);
            signature_ = std::move(other.signature_);
            state_ = std::exchange(other.state_, SketchState::NeedsRecovery);
        }
        return *this;
    }

    [[nodiscard]] std::size_t signature_length() const noexcept {
        return hashes_ ? hashes_->size() : 0;
    }
    [[nodiscard]] std::size_t buffer_capacity() const noexcept { return capacity_; }
    [[nodiscard]] SketchState state() const noexcept { return state_; }
    [[nodiscard]] hash_family_ptr hash_family() const noexcept { return hashes_; }

    [[nodiscard]] UpdateResult insert(Element element) {
        require_ready();
        insert_unchecked(element);
        return UpdateResult::Ok;
    }

    [[nodiscard]] UpdateResult erase(Element element) {
        require_ready();
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            const entry_type entry{(*hashes_)[i](element), element};
            if (thresholds_[i] && entry > *thresholds_[i]) {
                continue;
            }
            auto& buffer = buffers_[i];
            if (!buffer.erase(entry)) {
                continue;
            }
            // Deletions retain the threshold, even if its entry was removed.
            if (buffer.empty()) {
                state_ = SketchState::NeedsRecovery;
                return UpdateResult::NeedsRecovery;
            }
            if (signature_[i] == entry.hash) {
                signature_[i] = buffer.minimum().hash;
            }
        }
        return UpdateResult::Ok;
    }

    // Read one pass over the authoritative current set, after the triggering
    // deletion. No reference or copy of the full set is retained. Repeated
    // elements are harmless. Restore may also replace a ready sketch's set.
    template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        requires std::same_as<std::iter_value_t<Iterator>, Element>
    void restore(Iterator first, Sentinel last) {
        if (!hashes_) {
            throw std::logic_error("assign a configured sketch before using a moved-from object");
        }
        state_ = SketchState::NeedsRecovery;
        for (auto& buffer : buffers_) {
            buffer.clear();
        }
        std::fill(thresholds_.begin(), thresholds_.end(), std::nullopt);
        std::fill(signature_.begin(), signature_.end(), hash_type{});
        for (; first != last; ++first) {
            insert_unchecked(*first);
        }
        state_ = SketchState::Ready;
    }

    template<std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Element>
    void restore(Range&& elements) {
        restore(std::ranges::begin(elements), std::ranges::end(elements));
    }

    // Views must not be used after mutation, assignment, move, or destruction.
    // nullopt means a known empty set, never a fault.
    [[nodiscard]] std::optional<signature_view> signature() const {
        require_ready();
        if (buffers_.front().empty()) {
            return std::nullopt;
        }
        return signature_view{signature_};
    }

    [[nodiscard]] std::optional<std::vector<hash_type>> copy_signature() const {
        if (!signature()) {
            return std::nullopt;
        }
        return signature_;
    }

    template<BufferKind OtherKind>
    [[nodiscard]] bool compatible_with(const BufferedMinhash<Element, OtherKind, Hash>& other) const {
        const auto other_hashes = other.hash_family();
        return hashes_ && other_hashes &&
               (hashes_ == other_hashes || *hashes_ == *other_hashes);
    }

private:
    using entry_type = detail::Entry<Element, hash_type>;
    using buffer_type = std::conditional_t<Kind == BufferKind::Array,
        detail::ArrayBuffer<entry_type>, detail::TreeBuffer<entry_type>>;

    void require_ready() const {
        if (state_ != SketchState::Ready || !hashes_) {
            throw std::logic_error("the sketch requires recovery before updates or signature access");
        }
    }

    void insert_unchecked(Element element) {
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            const entry_type entry{(*hashes_)[i](element), element};
            if (thresholds_[i] && entry > *thresholds_[i]) {
                continue;
            }
            auto& buffer = buffers_[i];
            if (!buffer.insert(entry)) {
                continue;
            }
            if (buffer.size() == capacity_) {
                thresholds_[i] = buffer.maximum();
            }
            if (buffer.size() == 1 || entry.hash < signature_[i]) {
                signature_[i] = entry.hash;
            }
        }
    }

    hash_family_ptr hashes_;
    std::size_t capacity_;
    std::vector<buffer_type> buffers_;
    std::vector<std::optional<entry_type>> thresholds_;
    std::vector<hash_type> signature_;
    SketchState state_ = SketchState::Ready;
};

// Comparison is available across buffer strategies, with the same element/hash
// types. Convention: two empty sets have similarity 1, exactly one empty set 0.
template<ElementType Element, BufferKind Left, BufferKind Right, class Hash>
[[nodiscard]] double similarity(const BufferedMinhash<Element, Left, Hash>& left,
                                const BufferedMinhash<Element, Right, Hash>& right) {
    if (!left.compatible_with(right)) {
        throw std::invalid_argument("cannot compare incompatible hash configurations");
    }
    const auto a = left.signature();
    const auto b = right.signature();
    if (!a || !b) {
        return (!a && !b) ? 1.0 : 0.0;
    }
    std::size_t equal = 0;
    for (std::size_t i = 0; i < a->size(); ++i) {
        equal += ((*a)[i] == (*b)[i]);
    }
    return static_cast<double>(equal) / static_cast<double>(a->size());
}

} // namespace buffered_minhash
