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

/// Buffered MinHash sketches and their shared hash configurations.
namespace buffered_minhash {

// Defined in minhash.hpp; forward-declared for cross-variant comparisons.
template<ElementType Element, class Hash>
    requires HashFunction<Hash> && std::same_as<typename Hash::element_type, Element>
class Minhash;

/// Storage strategy for each bounded buffer.
enum class BufferKind {
    Array, ///< Unordered contiguous entries; buffer searches take linear time.
    Tree   ///< Ordered entries; searches and updates take logarithmic time.
};

/// Compile-time update contract; use LegalBufferedMinhash for the legal variant.
enum class StreamKind {
    General, ///< Redundant updates are supported through stored element identities.
    Legal    ///< Insert absent elements and erase present elements only; unchecked.
};

/// Outcome of an update, describing sketch validity rather than set membership.
enum class UpdateResult {
    Ok,           ///< The sketch remains ready, including after a redundant update.
    NeedsRecovery ///< A buffer was exhausted; restore the current set before reuse.
};

/// Whether incremental updates and signature access are currently allowed.
enum class SketchState {
    Ready,        ///< Valid sketch, possibly representing a known empty set.
    NeedsRecovery ///< Updates and signature access are blocked until restoration.
};

/**
 * @brief Maintains a k-MinHash signature under set insertions and deletions.
 * @tparam Element Element identity type: uint32_t (default) or uint64_t.
 * @tparam Kind Buffer representation: Array (default) or Tree.
 * @tparam Hash Immutable hash function type satisfying HashFunction, with input type
 * Element. Defaults to TabulationHash<Element> with uint64_t outputs.
 * @tparam Stream General (default) or Legal; selects storage at compile time.
 *
 * For General streams, each hash function has a buffer of at most ell distinct
 * (hash, element) pairs. Duplicate insertions and deletion of absent elements
 * are supported. For Legal streams, buffers retain only hashes, including
 * multiplicities on collisions: insertions must be absent elements and deletions
 * must be present elements. restore() must enumerate each element exactly once.
 * These legal-stream preconditions are not checked; violations can silently
 * produce incorrect signatures. Collisions can cause earlier recovery than in
 * General mode, but signatures remain exact while Ready under the contract.
 * The sketch neither owns nor retains the authoritative set.
 * Update that set before calling insert() or erase(). If a deletion exhausts
 * a buffer, restore() must rebuild the sketch from the current set.
 *
 * Copies have independent buffers and share only the immutable hash family.
 * Access to a single sketch must be externally synchronized.
 * @note Hash evaluation, allocations, and input traversal are assumed to
 * succeed; unexpected-exception recovery is not part of the current contract.
 */
template<ElementType Element = std::uint32_t,
         BufferKind Kind = BufferKind::Array,
         class Hash = TabulationHash<Element>,
         StreamKind Stream = StreamKind::General>
    requires HashFunction<Hash> && std::same_as<typename Hash::element_type, Element> &&
             (Kind == BufferKind::Array || Kind == BufferKind::Tree) &&
             (Stream == StreamKind::General || Stream == StreamKind::Legal)
class BufferedMinhash {
public:
    /// Input element type; identities are retained only for general streams.
    using element_type = Element;
    /// Compile-time update contract; no runtime mode switching is supported.
    static constexpr StreamKind stream_kind = Stream;
    /// Hash output and signature coordinate type, independent of element width.
    using hash_type = typename Hash::hash_type;
    /// Immutable ordered configuration of the k concrete hash functions.
    using hash_family_type = HashFamily<Hash>;
    /// Shared ownership handle keeping the hash configuration alive.
    using hash_family_ptr = std::shared_ptr<const hash_family_type>;
    /// Non-owning, read-only view of the k signature coordinates.
    using signature_view = std::span<const hash_type>;

    /**
     * @brief Constructs a ready, empty sketch using shared hash functions.
     * @param capacity Maximum number of entries per buffer (ell); positive.
     * @param hashes Non-null shared configuration; its function count determines k.
     * @throws std::invalid_argument If capacity is zero or hashes is null.
     * @note The configuration is shared, not cloned. The new signature is empty.
     */
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

    /**
     * @brief Constructs a ready, empty sketch with automatically seeded hashes.
     * @param k Number of hash functions and signature coordinates; positive.
     * @param capacity Maximum number of entries per buffer (ell); positive.
     * @throws std::invalid_argument If k or capacity is zero.
     * @note Available when Hash accepts a uint64_t seed. Reuse hash_family() to
     * create compatible sketches; independent construction need not be compatible.
     */
    BufferedMinhash(std::size_t k, std::size_t capacity)
        requires std::constructible_from<Hash, std::uint64_t>
        : BufferedMinhash(capacity, make_hash_family<Element, Hash>(k)) {}

    /**
     * @brief Constructs a ready, empty sketch with reproducibly seeded hashes.
     * @param k Number of hash functions and signature coordinates; positive.
     * @param capacity Maximum number of entries per buffer (ell); positive.
     * @param seed Seed used to generate the ordered hash configuration.
     * @throws std::invalid_argument If k or capacity is zero.
     * @note Available when Hash accepts a uint64_t seed. Custom hash constructors
     * must be deterministic for seeded construction to be reproducible.
     */
    BufferedMinhash(std::size_t k, std::size_t capacity, std::uint64_t seed)
        requires std::constructible_from<Hash, std::uint64_t>
        : BufferedMinhash(capacity, make_hash_family<Element, Hash>(k, seed)) {}

    /**
     * @brief Copies buffers, thresholds, signature, and state independently.
     * @param other Sketch to copy, including a possible NeedsRecovery state.
     * @note Only the immutable hash configuration is shared with other.
     */
    BufferedMinhash(const BufferedMinhash& other) = default;

    /**
     * @brief Replaces this sketch with an independent copy of another's state.
     * @param other Sketch whose buffers/state are copied and hashes shared.
     * @return This sketch.
     * @note Previously obtained signature views of this sketch must not be reused.
     */
    BufferedMinhash& operator=(const BufferedMinhash& other) = default;

    /**
     * @brief Transfers another sketch's buffers, configuration, and state.
     * @param other Source sketch, left unconfigured in NeedsRecovery state.
     * @note Assign a configured sketch to other before reusing it; restore()
     * cannot configure a moved-from sketch. Discard its prior signature views.
     */
    BufferedMinhash(BufferedMinhash&& other) noexcept
        : hashes_(std::move(other.hashes_)),
          capacity_(std::exchange(other.capacity_, 0)),
          buffers_(std::move(other.buffers_)), thresholds_(std::move(other.thresholds_)),
          signature_(std::move(other.signature_)),
          state_(std::exchange(other.state_, SketchState::NeedsRecovery)) {}

    /**
     * @brief Replaces this sketch by transferring another sketch's state.
     * @param other Source, left unconfigured in NeedsRecovery unless self-assigned.
     * @return This sketch.
     * @note Discard prior signature views of both objects. A moved-from source
     * must be assigned a configured sketch before reuse.
     */
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

    /**
     * @brief Returns the configured number of hash functions (k).
     * @return Signature length, or zero for a moved-from sketch.
     */
    [[nodiscard]] std::size_t signature_length() const noexcept {
        return hashes_ ? hashes_->size() : 0;
    }
    /**
     * @brief Returns the maximum number of entries in each buffer (ell).
     * @return Buffer capacity, or zero for a moved-from sketch.
     */
    [[nodiscard]] std::size_t buffer_capacity() const noexcept { return capacity_; }

    /**
     * @brief Reports whether signature access and incremental updates are allowed.
     * @return Ready or NeedsRecovery; an empty set can still be Ready.
     */
    [[nodiscard]] SketchState state() const noexcept { return state_; }

    /**
     * @brief Shares the current immutable hash configuration with the caller.
     * @return An owning shared handle, or null for a moved-from sketch.
     * @note Passing this handle to another sketch preserves hash compatibility
     * without copying tables, even if the sketches use different capacities.
     */
    [[nodiscard]] hash_family_ptr hash_family() const noexcept { return hashes_; }

    /**
     * @brief Incorporates an element insertion into the represented set.
     * @param element Exact element identity inserted into the authoritative set.
     * @return UpdateResult::Ok; this does not indicate whether the set changed.
     * @pre The authoritative set has already been updated.
     * @pre For Legal streams, element was absent before this update (unchecked).
     * @throws std::logic_error If recovery is required or the sketch is moved-from.
     * @note For General streams, repeated insertion is harmless. Discard previous
     * signature views after calling this method.
     */
    [[nodiscard]] UpdateResult insert(Element element) {
        require_ready();
        insert_unchecked(element);
        return UpdateResult::Ok;
    }

    /**
     * @brief Incorporates an element deletion from the represented set.
     * @param element Exact element identity removed from the authoritative set.
     * @return Ok if no buffer is exhausted; NeedsRecovery otherwise. The result
     * does not indicate whether the element previously belonged to the set.
     * @pre The authoritative set has already been updated.
     * @pre For Legal streams, element was present before this update (unchecked).
     * @throws std::logic_error If recovery is required or the sketch is moved-from.
     * @note On NeedsRecovery, call restore() with the current set after this
     * deletion, even if that set is empty. Discard previous signature views.
     * For General streams, deletion of an absent element is harmless.
     */
    [[nodiscard]] UpdateResult erase(Element element) {
        require_ready();
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            const auto entry = make_entry((*hashes_)[i](element), element);
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
            if (signature_[i] == entry_hash(entry)) {
                signature_[i] = entry_hash(buffer.minimum());
            }
        }
        return UpdateResult::Ok;
    }

    /**
     * @brief Rebuilds every buffer from a single pass over an iterator range.
     * @tparam Iterator Input iterator whose value type is exactly element_type.
     * @tparam Sentinel End sentinel compatible with Iterator.
     * @param first Iterator to the first element of the current authoritative set.
     * @param last Past-the-end sentinel; [first, last) may be empty.
     * @pre The range is a coherent scan of the set after any fault-triggering
     * deletion, with no concurrent updates to that scan or this sketch.
     * For Legal streams, each element must occur exactly once (unchecked).
     * @post The sketch is Ready and uses the same hash configuration.
     * @throws std::logic_error If this is a moved-from, unconfigured sketch.
     * @note Duplicates are harmless only for General streams. No full set or
     * reference to the range is retained. Also usable on a ready sketch to
     * replace its represented set.
     * Discard previous signature views; unexpected traversal/allocation failures
     * are outside the supported recovery contract.
     */
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

    /**
     * @brief Rebuilds every buffer from a single pass over the current set.
     * @tparam Range Input range whose value type is exactly element_type.
     * @param elements Coherent current set, including the fault-triggering
     * deletion. Empty ranges are supported; repeated elements only for General streams.
     * @pre For Legal streams, elements contains no duplicates (unchecked).
     * @post The sketch is Ready and uses the same hash configuration.
     * @throws std::logic_error If this is a moved-from, unconfigured sketch.
     * @note Does not retain the range or copy the entire set. Discard previous
     * signature views. The same scan and failure contract as the iterator
     * overload applies; the range must not change during traversal.
     */
    template<std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Element>
    void restore(Range&& elements) {
        restore(std::ranges::begin(elements), std::ranges::end(elements));
    }

    /**
     * @brief Returns a read-only view of the current k-MinHash signature.
     * @return A span of k hash values, or nullopt for a known empty set.
     * @throws std::logic_error If recovery is required or the sketch is moved-from.
     * @note The view borrows internal memory and must not be used after any
     * mutation, assignment, move, or destruction of this sketch. It performs no
     * live validity checks. Use copy_signature() to retain an independent value.
     */
    [[nodiscard]] std::optional<signature_view> signature() const {
        require_ready();
        if (buffers_.front().empty()) {
            return std::nullopt;
        }
        return signature_view{signature_};
    }

    /**
     * @brief Copies the current signature into independently owned storage.
     * @return A vector of k hash values, or nullopt for a known empty set.
     * @throws std::logic_error If recovery is required or the sketch is moved-from.
     * @note The returned value survives subsequent changes to or destruction of
     * this sketch. Copying a nonempty signature takes O(k) time and space.
     */
    [[nodiscard]] std::optional<std::vector<hash_type>> copy_signature() const {
        if (!signature()) {
            return std::nullopt;
        }
        return signature_;
    }

    /**
     * @brief Checks whether two sketches use the same ordered hash configuration.
     * @tparam OtherKind Buffer representation of the other sketch.
     * @tparam OtherStream Update contract of the other sketch; it may differ.
     * @param other Sketch with the same element and concrete hash types.
     * @return True for shared or value-equal configurations; false if either
     * sketch is unconfigured. Buffer capacities and represented sets may differ.
     * @note This checks configuration only, not readiness. It is O(1) for a
     * shared configuration; otherwise it compares the complete hash parameters.
     */
    template<BufferKind OtherKind, StreamKind OtherStream>
    [[nodiscard]] bool compatible_with(
        const BufferedMinhash<Element, OtherKind, Hash, OtherStream>& other) const {
        const auto other_hashes = other.hash_family();
        return hashes_ && other_hashes &&
               (hashes_ == other_hashes || *hashes_ == *other_hashes);
    }

    /**
     * @brief Checks hash compatibility with a vanilla sketch.
     * @param other Vanilla sketch with the same element and concrete hash types.
     * @return True for shared or equal configurations, independently of state.
     * @note Include minhash.hpp to construct the vanilla sketch.
     */
    [[nodiscard]] bool compatible_with(const Minhash<Element, Hash>& other) const {
        return other.compatible_with(*this);
    }

private:
    static constexpr bool unique_entries = Stream == StreamKind::General;
    using entry_type = std::conditional_t<unique_entries,
        detail::Entry<Element, hash_type>, hash_type>;
    using buffer_type = std::conditional_t<Kind == BufferKind::Array,
        detail::ArrayBuffer<entry_type, unique_entries>,
        detail::TreeBuffer<entry_type, unique_entries>>;

    static entry_type make_entry(hash_type hash, Element element) {
        if constexpr (unique_entries) {
            return {hash, element};
        } else {
            return hash;
        }
    }

    static hash_type entry_hash(const entry_type& entry) {
        if constexpr (unique_entries) {
            return entry.hash;
        } else {
            return entry;
        }
    }

    void require_ready() const {
        if (state_ != SketchState::Ready || !hashes_) {
            throw std::logic_error("the sketch requires recovery before updates or signature access");
        }
    }

    void insert_unchecked(Element element) {
        for (std::size_t i = 0; i < buffers_.size(); ++i) {
            const auto entry = make_entry((*hashes_)[i](element), element);
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
            if (buffer.size() == 1 || entry_hash(entry) < signature_[i]) {
                signature_[i] = entry_hash(entry);
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

/**
 * @brief Compact buffered MinHash specialized for legal set-update streams.
 * @tparam Element Input type: uint32_t (default) or uint64_t.
 * @tparam Kind Array (default) or Tree buffer storage.
 * @tparam Hash Hash function type, defaulting to 64-bit-output tabulation hashing.
 *
 * Stores only hash values, with multiplicities for distinct colliding elements.
 * Array insertion omits the duplicate search; trees use a multiset. The API and
 * recovery protocol match BufferedMinhash. Hash families and comparisons can be
 * shared with general sketches. No authoritative set is retained or checked.
 * @pre Insert only absent elements, erase only present elements, and supply
 * duplicate-free ranges to restore(). Reinsertion after a deletion is allowed.
 * @note Violating these unchecked preconditions can silently corrupt signatures.
 * Collisions at the threshold can cause earlier recovery than in general sketches.
 */
template<ElementType Element = std::uint32_t,
         BufferKind Kind = BufferKind::Array,
         class Hash = TabulationHash<Element>>
using LegalBufferedMinhash = BufferedMinhash<Element, Kind, Hash, StreamKind::Legal>;

/**
 * @brief Estimates Jaccard similarity by comparing corresponding hash values.
 * @tparam Element Common element identity type.
 * @tparam Left Buffer strategy of the first sketch.
 * @tparam Right Buffer strategy of the second sketch.
 * @tparam Hash Common concrete hash function type.
 * @tparam LeftStream Update contract of the first sketch.
 * @tparam RightStream Update contract of the second sketch; it may differ.
 * @param left First sketch, with a compatible hash configuration and ready state.
 * @param right Second sketch; buffer capacity may differ from left.
 * @return Fraction of equal coordinates for nonempty sets. By convention,
 * returns 1 for two empty sets and 0 when exactly one set is empty.
 * @throws std::invalid_argument If the ordered hash configurations are incompatible.
 * @throws std::logic_error If configurations are compatible but a sketch needs recovery.
 * @note Statistical accuracy depends on the chosen hash family and k.
 */
template<ElementType Element, BufferKind Left, BufferKind Right, class Hash,
         StreamKind LeftStream, StreamKind RightStream>
[[nodiscard]] double similarity(const BufferedMinhash<Element, Left, Hash, LeftStream>& left,
                                const BufferedMinhash<Element, Right, Hash, RightStream>& right) {
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
