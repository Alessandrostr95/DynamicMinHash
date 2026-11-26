#pragma once

#include "buffered_minhash.hpp"

#include <numeric>

namespace buffered_minhash {

/**
 * @brief Vanilla k-MinHash with recovery of only the coordinates lost to deletion.
 * @tparam Element Input identity type: uint32_t (default) or uint64_t.
 * @tparam Hash Immutable HashFunction with input type Element; defaults to
 * TabulationHash<Element> with uint64_t output.
 *
 * Stores one minimum hash and its winning element per coordinate, without
 * buffers or thresholds. Ties are broken by element identity. Redundant set
 * updates and hash collisions are supported. The authoritative set belongs to
 * the application, which must update it before notifying this sketch.
 *
 * erase() invalidates every coordinate won by the removed element, then enters
 * NeedsRecovery if any were lost. Further updates and signature reads are
 * blocked until restore() scans the current set and repairs only those entries.
 * Valid entries and their hash functions are not visited during that scan.
 * On a Ready sketch, restore() instead initializes/replaces the entire set, as
 * in BufferedMinhash. Unlike capacity-one BufferedMinhash with Full recovery,
 * surviving minima are preserved. EmptyOnly has the same selective behavior
 * for general streams, but uses buffer containers and thresholds.
 *
 * Copies own independent mutable state and share immutable hashes. Access to
 * a single sketch requires external synchronization. Hash evaluation, storage
 * operations and input traversal are assumed to succeed; general exception
 * recovery is outside the contract.
 */
template<ElementType Element = std::uint32_t, class Hash = TabulationHash<Element>>
    requires HashFunction<Hash> && std::same_as<typename Hash::element_type, Element>
class Minhash {
public:
    /// Exact identity type accepted by updates and restore ranges.
    using element_type = Element;
    /// This class supports general set updates, including redundant updates.
    static constexpr StreamKind stream_kind = StreamKind::General;
    /// Signature coordinate type, independent of element width.
    using hash_type = typename Hash::hash_type;
    /// Immutable ordered configuration of the k concrete hash functions.
    using hash_family_type = HashFamily<Hash>;
    /// Owning shared handle to the hash configuration.
    using hash_family_ptr = std::shared_ptr<const hash_family_type>;
    /// Borrowed read-only view of the k signature values.
    using signature_view = std::span<const hash_type>;

    /**
     * @brief Creates a ready, empty sketch sharing an existing hash configuration.
     * @param hashes Non-null configuration; its function count determines k.
     * @throws std::invalid_argument If hashes is null.
     * @note No buffer-capacity argument is needed for vanilla MinHash.
     */
    explicit Minhash(hash_family_ptr hashes) : hashes_(std::move(hashes)) {
        if (!hashes_) {
            throw std::invalid_argument("hash family must be non-null");
        }
        winners_.resize(hashes_->size());
        signature_.resize(hashes_->size());
        pending_.reserve(hashes_->size());
    }

    /**
     * @brief Creates a ready, empty sketch with automatically seeded hashes.
     * @param k Positive number of signature coordinates.
     * @throws std::invalid_argument If k is zero.
     * @note Reuse hash_family() to create compatible sketches.
     */
    explicit Minhash(std::size_t k)
        requires std::constructible_from<Hash, std::uint64_t>
        : Minhash(make_hash_family<Element, Hash>(k)) {}

    /**
     * @brief Creates a ready, empty sketch with reproducibly seeded hashes.
     * @param k Positive number of signature coordinates.
     * @param seed Seed generating the ordered hash configuration.
     * @throws std::invalid_argument If k is zero.
     */
    Minhash(std::size_t k, std::uint64_t seed)
        requires std::constructible_from<Hash, std::uint64_t>
        : Minhash(make_hash_family<Element, Hash>(k, seed)) {}

    /// Copies all mutable state, including pending recoveries; shares only hashes.
    Minhash(const Minhash& other) = default;
    /// Independently copies state and shares hashes; invalidates old signature views.
    Minhash& operator=(const Minhash& other) = default;

    /**
     * @brief Transfers signature values, winning identities and recovery state.
     * @param other Source, left unconfigured in NeedsRecovery state.
     * @note Assign a configured sketch to the source before reusing it.
     */
    Minhash(Minhash&& other) noexcept
        : hashes_(std::move(other.hashes_)), winners_(std::move(other.winners_)),
          signature_(std::move(other.signature_)), pending_(std::move(other.pending_)),
          state_(std::exchange(other.state_, SketchState::NeedsRecovery)) {}

    /**
     * @brief Replaces state by transferring ownership from another sketch.
     * @param other Source, left unconfigured unless self-assigned.
     * @return This sketch; discard prior signature views of both sketches.
     */
    Minhash& operator=(Minhash&& other) noexcept {
        if (this != &other) {
            hashes_ = std::move(other.hashes_);
            winners_ = std::move(other.winners_);
            signature_ = std::move(other.signature_);
            pending_ = std::move(other.pending_);
            state_ = std::exchange(other.state_, SketchState::NeedsRecovery);
        }
        return *this;
    }

    /// Returns k, or zero for a moved-from, unconfigured sketch.
    [[nodiscard]] std::size_t signature_length() const noexcept {
        return hashes_ ? hashes_->size() : 0;
    }

    /// Returns Ready (including known empty sets) or NeedsRecovery.
    [[nodiscard]] SketchState state() const noexcept { return state_; }

    /// Returns a shared immutable configuration, or null for a moved-from sketch.
    [[nodiscard]] hash_family_ptr hash_family() const noexcept { return hashes_; }

    /**
     * @brief Incorporates a set insertion; redundant insertions are harmless.
     * @param element Element inserted into the authoritative set.
     * @return Ok; this reports validity, not whether the external set changed.
     * @pre Update the authoritative set first.
     * @throws std::logic_error If recovery is required or this sketch was moved-from.
     * @note Discard prior signature views after any mutation.
     */
    [[nodiscard]] UpdateResult insert(Element element) {
        require_ready();
        for (std::size_t i = 0; i < signature_.size(); ++i) {
            consider(i, element);
        }
        return UpdateResult::Ok;
    }

    /**
     * @brief Invalidates all coordinates whose winning element is removed.
     * @param element Element removed from the authoritative set; absent is harmless.
     * @return NeedsRecovery if any minimum was lost, otherwise Ok.
     * @pre Update the authoritative set first.
     * @throws std::logic_error If recovery is required or this sketch was moved-from.
     * @note Scans all coordinates, even after finding a lost minimum. No hash
     * evaluations are needed: winning identities determine exactly which entries
     * to invalidate. A fault blocks reads/updates even if the set became empty.
     */
    [[nodiscard]] UpdateResult erase(Element element) {
        require_ready();
        for (std::size_t i = 0; i < winners_.size(); ++i) {
            if (winners_[i] && *winners_[i] == element) {
                winners_[i].reset();
                pending_.push_back(i);
            }
        }
        if (!pending_.empty()) {
            state_ = SketchState::NeedsRecovery;
            return UpdateResult::NeedsRecovery;
        }
        return UpdateResult::Ok;
    }

    /**
     * @brief Repairs lost minima in one pass, or replaces a Ready sketch's set.
     * @tparam Iterator Input iterator with value type exactly element_type.
     * @tparam Sentinel Compatible end sentinel.
     * @param first Start of the current authoritative set; repeated elements allowed.
     * @param last End of the range; empty ranges are allowed.
     * @pre After a fault, supply the same set after the triggering deletion,
     * with no additional unreported updates. Retained coordinates rely on this.
     * The scan and sketch must not be modified concurrently.
     * @post Ready, with the same hash configuration and a complete signature
     * (or a known empty set). No input range or full set is retained.
     * @throws std::logic_error If this sketch was moved-from.
     * @note In NeedsRecovery, evaluates only the r pending hash functions per
     * input element: O(n*r + r) work after an O(k) deletion. In Ready, rebuilds
     * all k coordinates, allowing initialization or replacement from another set.
     * Retry after a traversal error resets the same pending coordinates; the
     * application must still supply the complete, unchanged authoritative set.
     */
    template<std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        requires std::same_as<std::iter_value_t<Iterator>, Element>
    void restore(Iterator first, Sentinel last) {
        if (!hashes_) {
            throw std::logic_error("assign a configured sketch before using a moved-from object");
        }
        if (state_ == SketchState::Ready) {
            pending_.resize(signature_.size());
            std::iota(pending_.begin(), pending_.end(), std::size_t{0});
        }
        state_ = SketchState::NeedsRecovery;
        // Keep the target list until the whole scan succeeds. Partially computed
        // minima from a failed traversal must not be mistaken for valid entries.
        for (auto i : pending_) {
            winners_[i].reset();
            signature_[i] = hash_type{};
        }
        for (; first != last; ++first) {
            const Element element = *first;
            for (auto i : pending_) {
                consider(i, element);
            }
        }
        pending_.clear();
        state_ = SketchState::Ready;
    }

    /**
     * @brief Range overload of selective recovery or Ready-state initialization.
     * @tparam Range Input range with value type exactly element_type.
     * @param elements Coherent current set, possibly empty or containing repetitions.
     * @note Uses the iterator overload's state-dependent semantics and contract.
     * During fault recovery, this cannot replace the set with an unrelated one.
     * Discard previous signature views.
     */
    template<std::ranges::input_range Range>
        requires std::same_as<std::ranges::range_value_t<Range>, Element>
    void restore(Range&& elements) {
        restore(std::ranges::begin(elements), std::ranges::end(elements));
    }

    /**
     * @brief Borrows the complete k-MinHash signature of a Ready sketch.
     * @return Read-only span, or nullopt for a known empty set.
     * @throws std::logic_error If recovery is required or the sketch was moved-from.
     * @note The view becomes invalid after mutation, assignment, move or destruction.
     */
    [[nodiscard]] std::optional<signature_view> signature() const {
        require_ready();
        if (!winners_.front()) {
            return std::nullopt;
        }
        return signature_view{signature_};
    }

    /**
     * @brief Copies the complete signature into independent storage in O(k).
     * @return Owned vector of hashes, or nullopt for a known empty set.
     * @throws std::logic_error If recovery is required or the sketch was moved-from.
     */
    [[nodiscard]] std::optional<std::vector<hash_type>> copy_signature() const {
        if (!signature()) {
            return std::nullopt;
        }
        return signature_;
    }

    /**
     * @brief Compares ordered hash configurations, independently of sketch state.
     * @tparam Other Vanilla or buffered sketch with the same concrete hash type.
     * @param other Sketch whose hash configuration is compared.
     * @return True for shared or equal configurations; false for unconfigured sketches.
     */
    template<class Other>
        requires std::same_as<typename Other::hash_family_type, hash_family_type>
    [[nodiscard]] bool compatible_with(const Other& other) const {
        const auto other_hashes = other.hash_family();
        return hashes_ && other_hashes &&
               (hashes_ == other_hashes || *hashes_ == *other_hashes);
    }

private:
    void require_ready() const {
        if (state_ != SketchState::Ready || !hashes_) {
            throw std::logic_error("the sketch requires recovery before updates or signature access");
        }
    }

    void consider(std::size_t i, Element element) {
        const auto hash = (*hashes_)[i](element);
        if (!winners_[i] || hash < signature_[i] ||
            (hash == signature_[i] && element < *winners_[i])) {
            signature_[i] = hash;
            winners_[i] = element;
        }
    }

    hash_family_ptr hashes_;
    std::vector<std::optional<Element>> winners_;
    std::vector<hash_type> signature_;
    std::vector<std::size_t> pending_;
    SketchState state_ = SketchState::Ready;
};

/**
 * @brief Compares vanilla MinHash with a compatible vanilla or buffered sketch.
 * @tparam Element Common element type.
 * @tparam Hash Common concrete hash type.
 * @tparam Other Sketch type using that same hash family.
 * @param left Ready vanilla sketch.
 * @param right Ready sketch with compatible hashes; capacity/representation may differ.
 * @return Fraction of equal hashes; empty/empty is 1 and exactly one empty is 0.
 * @throws std::invalid_argument For incompatible configurations.
 * @throws std::logic_error For compatible sketches requiring recovery.
 */
template<ElementType Element, class Hash, class Other>
    requires std::same_as<typename Other::hash_family_type, HashFamily<Hash>>
[[nodiscard]] double similarity(const Minhash<Element, Hash>& left, const Other& right) {
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

/**
 * @brief Symmetric buffered/vanilla overload with the same compatibility contract.
 * @param left Ready buffered sketch with compatible hashes.
 * @param right Ready vanilla sketch.
 * @return The same estimate as similarity(right, left).
 */
template<ElementType Element, BufferKind Kind, class Hash, StreamKind Stream>
[[nodiscard]] double similarity(const BufferedMinhash<Element, Kind, Hash, Stream>& left,
                                const Minhash<Element, Hash>& right) {
    return similarity(right, left);
}

} // namespace buffered_minhash
