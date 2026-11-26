#include <buffered_minhash/minhash.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace buffered_minhash;

// Keep assertions enabled in Release builds as well.
#define CHECK(...) do { if (!(__VA_ARGS__)) { \
    throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + \
                             ": " #__VA_ARGS__); } } while (false)

template<class Exception, class Function>
void check_throws(Function&& function) {
    bool caught = false;
    try {
        function();
    } catch (const Exception&) {
        caught = true;
    }
    CHECK(caught);
}

template<ElementType Element, HashValueType Value = std::uint64_t>
struct TestHash {
    using element_type = Element;
    using hash_type = Value;
    Value mask = std::numeric_limits<Value>::max();
    Value xor_value = 0;

    Value operator()(Element element) const noexcept {
        return (static_cast<Value>(element) ^ xor_value) & mask;
    }
    bool operator==(const TestHash&) const = default;
};

template<class Hash>
auto family(std::vector<Hash> hashes) {
    return std::make_shared<const HashFamily<Hash>>(std::move(hashes));
}

template<class Sketch>
void check_signature(const Sketch& sketch, const std::set<typename Sketch::element_type>& elements) {
    CHECK(sketch.state() == SketchState::Ready);
    const auto actual = sketch.signature();
    CHECK(actual.has_value() == !elements.empty());
    if (elements.empty()) {
        CHECK(!sketch.copy_signature());
        return;
    }
    const auto hashes = sketch.hash_family();
    CHECK(actual->size() == hashes->size());
    for (std::size_t i = 0; i < hashes->size(); ++i) {
        auto expected = std::numeric_limits<typename Sketch::hash_type>::max();
        for (auto element : elements) {
            expected = std::min(expected, (*hashes)[i](element));
        }
        CHECK((*actual)[i] == expected);
    }
    const auto copy = sketch.copy_signature();
    CHECK(std::equal(copy->begin(), copy->end(), actual->begin(), actual->end()));
}

template<BufferKind Kind, ElementType Element, HashValueType Value>
void deterministic_cases() {
    using Hash = TestHash<Element, Value>;
    using Sketch = BufferedMinhash<Element, Kind, Hash>;
    const auto hashes = family<Hash>({Hash{}});
    Sketch sketch(2, hashes);
    CHECK(sketch.signature_length() == 1);
    CHECK(sketch.buffer_capacity() == 2);
    check_signature(sketch, {});
    CHECK(sketch.erase(Element{7}) == UpdateResult::Ok);

    // The original duplicate-insertion counterexample must now be correct.
    for (Element value : {Element{10}, Element{20}, Element{10}}) {
        CHECK(sketch.insert(value) == UpdateResult::Ok);
    }
    CHECK(sketch.erase(Element{10}) == UpdateResult::Ok);
    check_signature(sketch, {Element{20}});
    CHECK(sketch.erase(Element{10}) == UpdateResult::Ok);
    check_signature(sketch, {Element{20}});

    const auto saved = sketch.copy_signature();
    CHECK(sketch.erase(Element{20}) == UpdateResult::NeedsRecovery);
    CHECK(sketch.state() == SketchState::NeedsRecovery);
    CHECK((*saved)[0] == Value{20});
    check_throws<std::logic_error>([&] { (void)sketch.signature(); });
    check_throws<std::logic_error>([&] { (void)sketch.copy_signature(); });
    check_throws<std::logic_error>([&] { (void)sketch.insert(Element{30}); });
    check_throws<std::logic_error>([&] { (void)sketch.erase(Element{30}); });
    sketch.restore(std::vector<Element>{});
    CHECK(sketch.hash_family() == hashes);
    check_signature(sketch, {});

    // Threshold must not grow when deleting a buffered maximum. In particular,
    // 3 must remain outside the partial buffer, causing recovery after erasing 1.
    sketch.restore(std::vector<Element>{Element{1}, Element{2}, Element{3}});
    CHECK(sketch.erase(Element{2}) == UpdateResult::Ok);
    CHECK(sketch.insert(Element{3}) == UpdateResult::Ok);
    CHECK(sketch.erase(Element{1}) == UpdateResult::NeedsRecovery);
    sketch.restore(std::vector<Element>{Element{3}});
    check_signature(sketch, {Element{3}});

    // Nor may it shrink merely because its old maximum was deleted.
    sketch.restore(std::vector<Element>{Element{1}, Element{5}});
    CHECK(sketch.erase(Element{5}) == UpdateResult::Ok);
    CHECK(sketch.insert(Element{4}) == UpdateResult::Ok);
    CHECK(sketch.erase(Element{1}) == UpdateResult::Ok);
    check_signature(sketch, {Element{4}});

    // A finite threshold may still admit a pair between the current maximum
    // and the retained threshold while the buffer is not full.
    sketch.restore(std::vector<Element>{Element{2}, Element{8}, Element{10}});
    CHECK(sketch.erase(Element{8}) == UpdateResult::Ok);
    CHECK(sketch.insert(Element{5}) == UpdateResult::Ok);
    CHECK(sketch.erase(Element{2}) == UpdateResult::Ok);
    check_signature(sketch, {Element{5}, Element{10}});

    // Same hash, different identity: absent and repeated deletions are no-ops.
    const auto collisions = family<Hash>({Hash{Value{0}, Value{0}}});
    Sketch colliding(2, collisions);
    CHECK(colliding.insert(Element{1}) == UpdateResult::Ok);
    CHECK(colliding.insert(Element{2}) == UpdateResult::Ok);
    CHECK(colliding.erase(Element{3}) == UpdateResult::Ok);
    CHECK(colliding.erase(Element{1}) == UpdateResult::Ok);
    CHECK(colliding.erase(Element{1}) == UpdateResult::Ok);
    check_signature(colliding, {Element{2}});
    CHECK(colliding.erase(Element{2}) == UpdateResult::NeedsRecovery);

    // Threshold comparison must include identity, even when all hashes collide.
    colliding.restore(std::vector<Element>{Element{3}, Element{2}, Element{1}});
    CHECK(colliding.erase(Element{2}) == UpdateResult::Ok);
    CHECK(colliding.insert(Element{3}) == UpdateResult::Ok);
    CHECK(colliding.erase(Element{1}) == UpdateResult::NeedsRecovery);

    // Maximum-valued identities and hash outputs must never mean empty/infinite.
    const auto maximum = std::numeric_limits<Element>::max();
    sketch.restore(std::vector<Element>{maximum});
    check_signature(sketch, {maximum});
    CHECK(sketch.erase(maximum) == UpdateResult::NeedsRecovery);
    const auto max_hashes = family<Hash>({Hash{std::numeric_limits<Value>::max(),
                                             std::numeric_limits<Value>::max()}});
    Sketch max_hash(1, max_hashes);
    CHECK(max_hash.insert(Element{0}) == UpdateResult::Ok);
    CHECK((*max_hash.signature())[0] == std::numeric_limits<Value>::max());
    CHECK(max_hash.erase(Element{1}) == UpdateResult::Ok);
    CHECK(max_hash.erase(Element{0}) == UpdateResult::NeedsRecovery);

    // Restore accepts duplicates and input iterators, retains the hash family,
    // and can replace the set of an already-ready sketch.
    const std::vector<Element> repeated{Element{4}, Element{4}, Element{9}, Element{9}};
    sketch.restore(repeated.begin(), repeated.end());
    check_signature(sketch, {Element{4}, Element{9}});
    CHECK(sketch.erase(Element{4}) == UpdateResult::Ok);
    check_signature(sketch, {Element{9}});
    std::istringstream input("7 2 7 11");
    sketch.restore(std::istream_iterator<Element>{input}, std::istream_iterator<Element>{});
    check_signature(sketch, {Element{2}, Element{7}, Element{11}});
    CHECK(sketch.hash_family() == hashes);

    Sketch copied(sketch);
    CHECK(copied.hash_family() == sketch.hash_family());
    CHECK(copied.erase(Element{2}) == UpdateResult::Ok);
    check_signature(copied, {Element{7}, Element{11}});
    check_signature(sketch, {Element{2}, Element{7}, Element{11}});
    Sketch assigned(1, hashes);
    assigned = sketch;
    CHECK(assigned.buffer_capacity() == 2);
    CHECK(assigned.erase(Element{2}) == UpdateResult::Ok);
    check_signature(sketch, {Element{2}, Element{7}, Element{11}});

    Sketch moved(std::move(copied));
    check_signature(moved, {Element{7}, Element{11}});
    CHECK(copied.state() == SketchState::NeedsRecovery);
    check_throws<std::logic_error>([&] { (void)copied.signature(); });
    check_throws<std::logic_error>([&] { copied.restore(repeated); });
    copied = sketch;
    check_signature(copied, {Element{2}, Element{7}, Element{11}});
    assigned = std::move(copied);
    check_signature(assigned, {Element{2}, Element{7}, Element{11}});

    // Fault in a later buffer: restore must rebuild every coordinate, including
    // ones processed before the fault and ones never visited by that erase.
    const auto different_hashes = family<Hash>({Hash{}, Hash{Value{15}, Value{15}}, Hash{}});
    Sketch multi(1, different_hashes);
    multi.restore(std::vector<Element>{Element{1}, Element{2}});
    CHECK(multi.erase(Element{2}) == UpdateResult::NeedsRecovery);
    Sketch fault_copy(multi);
    check_throws<std::logic_error>([&] { (void)fault_copy.signature(); });
    fault_copy.restore(std::vector<Element>{Element{1}});
    CHECK(multi.state() == SketchState::NeedsRecovery);
    multi.restore(std::vector<Element>{Element{1}});
    check_signature(multi, {Element{1}});
}

void configuration_cases() {
    static_assert(!ElementType<std::uint8_t> && !ElementType<std::uint16_t>);
    static_assert(!ElementType<std::int32_t> && !ElementType<float>);
    static_assert(std::same_as<BufferedMinhash<>::element_type, std::uint32_t>);
    static_assert(std::same_as<BufferedMinhash<>::hash_type, std::uint64_t>);
    static_assert(!std::default_initializable<BufferedMinhash<>>);
    static_assert(!std::is_copy_assignable_v<HashFamily<TabulationHash<>>>);

    const auto hashes = make_hash_family<>(3, 42);
    const auto equivalent = make_hash_family<>(3, 42);
    const auto different = make_hash_family<>(3, 43);
    CHECK(hashes != equivalent);
    CHECK(*hashes == *equivalent);
    CHECK(!(*hashes == *different));
    BufferedMinhash<> a(2, hashes);
    BufferedMinhash<std::uint32_t, BufferKind::Tree> b(5, equivalent);
    CHECK(a.compatible_with(b));
    CHECK(similarity(a, b) == 1.0);
    CHECK(a.insert(5) == UpdateResult::Ok);
    CHECK(similarity(a, b) == 0.0);
    CHECK(b.insert(5) == UpdateResult::Ok);
    CHECK(similarity(a, b) == 1.0);
    CHECK(a.erase(5) == UpdateResult::NeedsRecovery);
    check_throws<std::logic_error>([&] { (void)similarity(a, b); });
    a.restore(std::vector<std::uint32_t>{5});

    BufferedMinhash<> c(2, different);
    CHECK(!a.compatible_with(c));
    check_throws<std::invalid_argument>([&] { (void)similarity(a, c); });
    BufferedMinhash<> short_signature(2, make_hash_family<>(2, 42));
    CHECK(!a.compatible_with(short_signature));
    auto reversed_functions = std::vector(hashes->functions().begin(), hashes->functions().end());
    std::reverse(reversed_functions.begin(), reversed_functions.end());
    BufferedMinhash<> reversed(2, family(std::move(reversed_functions)));
    CHECK(!a.compatible_with(reversed));

    BufferedMinhash<> automatic(3, 2);
    BufferedMinhash<> shared(2, automatic.hash_family());
    CHECK(automatic.compatible_with(shared));
    BufferedMinhash<> seeded(3, 2, 42);
    CHECK(a.compatible_with(seeded));
    const auto retained = [&] {
        auto local = make_hash_family<>(2, 123);
        return BufferedMinhash<>(2, local);
    }();
    CHECK(retained.signature_length() == 2);
    CHECK(!retained.signature());

    check_throws<std::invalid_argument>([] { (void)make_hash_family<>(0, 42); });
    check_throws<std::invalid_argument>([] { BufferedMinhash<> invalid(0, 2); });
    check_throws<std::invalid_argument>([&] { BufferedMinhash<> invalid(0, hashes); });
    check_throws<std::invalid_argument>([] {
        BufferedMinhash<> invalid(2, BufferedMinhash<>::hash_family_ptr{});
    });
    check_throws<std::invalid_argument>([] { (void)family<TestHash<std::uint32_t>>({}); });

    // Input width and output width are independent, including the upper input bits.
    const TabulationHash<std::uint64_t> wide_hash(123);
    CHECK(wide_hash(1) != wide_hash((std::uint64_t{1} << 40) | 1));
    const auto narrow_output = make_hash_family<std::uint64_t,
        TabulationHash<std::uint64_t, std::uint32_t>>(2, 42);
    BufferedMinhash<std::uint64_t, BufferKind::Array,
        TabulationHash<std::uint64_t, std::uint32_t>> wide_input(2, narrow_output);
    const auto value = (std::uint64_t{1} << 48) + 7;
    CHECK(wide_input.insert(value) == UpdateResult::Ok);
    check_signature(wide_input, {value});
}

// Exhaust all length-6 general or length-9 legal streams on three elements.
// Each collision pattern runs separately so a constant hash cannot mask a bug
// in another buffer by always triggering an earlier recovery.
template<BufferKind Kind, StreamKind Stream = StreamKind::General>
void exhaustive_cases(RecoveryPolicy recovery = RecoveryPolicy::Full) {
    using Hash = TestHash<std::uint32_t>;
    using Sketch = BufferedMinhash<std::uint32_t, Kind, Hash, Stream>;
    const std::vector<std::vector<Hash>> configurations = Stream == StreamKind::Legal
        ? std::vector<std::vector<Hash>>{{Hash{}}, {Hash{0, 0}}, {Hash{1, 0}},
                                        {Hash{1, 1}}, {Hash{}, Hash{1, 0}, Hash{3, 3}}}
        : std::vector<std::vector<Hash>>{{Hash{}, Hash{1, 0}, Hash{3, 3}}};
    for (const auto& configuration : configurations) {
        const auto hashes = family<Hash>(configuration);
        for (std::size_t capacity : {1u, 2u, 3u}) {
            auto visit = [&](auto&& self, const Sketch& sketch,
                             const std::set<std::uint32_t>& elements, int depth) -> void {
                check_signature(sketch, elements);
                if (depth == 0) {
                    return;
                }
                for (std::uint32_t value = 0; value < 3; ++value) {
                    for (bool insertion : {false, true}) {
                        if constexpr (Stream == StreamKind::Legal) {
                            if (insertion == elements.contains(value)) { continue; }
                        }
                        Sketch next(sketch);
                        auto expected = elements;
                        if (insertion) {
                            expected.insert(value);
                            CHECK(next.insert(value) == UpdateResult::Ok);
                        } else {
                            expected.erase(value);
                            if (next.erase(value) == UpdateResult::NeedsRecovery) {
                                next.restore(expected);
                            }
                        }
                        self(self, next, expected, depth - 1);
                    }
                }
            };
            visit(visit, Sketch(capacity, hashes, recovery), {}, Stream == StreamKind::Legal ? 9 : 6);
        }
    }
}

template<BufferKind Kind, ElementType Element, HashValueType Value>
void legal_deterministic_cases() {
    using Hash = TestHash<Element, Value>;
    using Sketch = LegalBufferedMinhash<Element, Kind, Hash>;
    static_assert(Sketch::stream_kind == StreamKind::Legal);
    const auto hashes = family<Hash>({Hash{}});
    Sketch sketch(2, hashes);
    check_signature(sketch, {});

    // A partial buffer keeps its old finite threshold. Above-threshold new
    // elements stay excluded; values between current max and threshold enter.
    sketch.restore(std::vector<Element>{1, 2, 3});
    CHECK(sketch.erase(2) == UpdateResult::Ok);
    CHECK(sketch.insert(4) == UpdateResult::Ok);
    CHECK(sketch.erase(1) == UpdateResult::NeedsRecovery);
    check_throws<std::logic_error>([&] { (void)sketch.signature(); });
    check_throws<std::logic_error>([&] { (void)sketch.copy_signature(); });
    check_throws<std::logic_error>([&] { (void)sketch.insert(5); });
    check_throws<std::logic_error>([&] { (void)sketch.erase(3); });
    Sketch fault_copy(sketch);
    sketch.restore(std::vector<Element>{3, 4});
    check_signature(sketch, {3, 4});
    CHECK(fault_copy.state() == SketchState::NeedsRecovery);
    fault_copy.restore(std::vector<Element>{});
    check_signature(fault_copy, {});

    sketch.restore(std::vector<Element>{2, 8, 10});
    CHECK(sketch.erase(8) == UpdateResult::Ok);
    CHECK(sketch.insert(5) == UpdateResult::Ok);
    CHECK(sketch.erase(2) == UpdateResult::Ok);
    check_signature(sketch, {5, 10});
    CHECK(sketch.insert(2) == UpdateResult::Ok); // Reinsertion is legal.
    check_signature(sketch, {2, 5, 10});

    // Equal hashes are distinct occurrences, even though identities are absent.
    const auto collisions = family<Hash>({Hash{Value{0}, Value{0}}});
    Sketch colliding(3, collisions);
    colliding.restore(std::vector<Element>{1, 2, 3});
    CHECK(colliding.erase(2) == UpdateResult::Ok);
    CHECK(colliding.erase(1) == UpdateResult::Ok);
    check_signature(colliding, {3});
    CHECK(colliding.insert(2) == UpdateResult::Ok);
    CHECK(colliding.erase(3) == UpdateResult::Ok);
    check_signature(colliding, {2});
    CHECK(colliding.erase(2) == UpdateResult::NeedsRecovery);
    colliding.restore(std::vector<Element>{});
    check_signature(colliding, {});

    // An unretained element tied at the threshold consumes one stored hash.
    // This causes a conservative early fault, not an incorrect ready signature.
    Sketch compact(2, collisions);
    BufferedMinhash<Element, Kind, Hash> general(2, collisions);
    compact.restore(std::vector<Element>{1, 2, 3});
    general.restore(std::vector<Element>{1, 2, 3});
    CHECK(compact.erase(3) == UpdateResult::Ok);
    CHECK(general.erase(3) == UpdateResult::Ok);
    check_signature(compact, {1, 2});
    CHECK(compact.erase(1) == UpdateResult::NeedsRecovery);
    CHECK(general.erase(1) == UpdateResult::Ok);
    check_throws<std::logic_error>([&] { (void)similarity(general, compact); });
    compact.restore(std::vector<Element>{2});
    CHECK(similarity(compact, general) == 1.0);
    CHECK(similarity(general, compact) == 1.0);

    // A truncated collision bucket cannot yield a stale smaller minimum after
    // every element of that bucket is deleted: a fault must happen first.
    const auto parity = family<Hash>({Hash{Value{1}, Value{0}}});
    Sketch tied(2, parity);
    std::set<Element> remaining{0, 2, 4, 1};
    tied.restore(remaining);
    CHECK(tied.erase(4) == UpdateResult::Ok);
    remaining.erase(4);
    check_signature(tied, remaining);
    CHECK(tied.erase(2) == UpdateResult::NeedsRecovery);
    remaining.erase(2);
    tied.restore(remaining);
    CHECK(tied.erase(0) == UpdateResult::Ok);
    check_signature(tied, {1});

    // No sentinel values: maximum hash and element values are ordinary data.
    const auto maximum = std::numeric_limits<Element>::max();
    sketch.restore(std::vector<Element>{0, maximum});
    CHECK(sketch.erase(0) == UpdateResult::Ok);
    check_signature(sketch, {maximum});
    CHECK(sketch.erase(maximum) == UpdateResult::NeedsRecovery);
    Sketch max_hash(1, family<Hash>({Hash{std::numeric_limits<Value>::max(),
                                             std::numeric_limits<Value>::max()}}));
    CHECK(max_hash.insert(0) == UpdateResult::Ok);
    CHECK((*max_hash.signature())[0] == std::numeric_limits<Value>::max());
    CHECK(max_hash.erase(0) == UpdateResult::NeedsRecovery);

    // One-pass restore, copy/move semantics and hash ownership match general mode.
    std::istringstream input("7 2 11");
    sketch.restore(std::istream_iterator<Element>{input}, std::istream_iterator<Element>{});
    check_signature(sketch, {2, 7, 11});
    CHECK(sketch.hash_family() == hashes);
    Sketch copied(sketch);
    CHECK(copied.erase(2) == UpdateResult::Ok);
    check_signature(copied, {7, 11});
    check_signature(sketch, {2, 7, 11});
    Sketch assigned(1, hashes);
    assigned = sketch;
    CHECK(assigned.buffer_capacity() == 2);
    CHECK(assigned.erase(2) == UpdateResult::Ok);
    check_signature(sketch, {2, 7, 11});
    Sketch moved(std::move(copied));
    check_signature(moved, {7, 11});
    CHECK(copied.state() == SketchState::NeedsRecovery);
    check_throws<std::logic_error>([&] { copied.restore(std::vector<Element>{}); });
    assigned = std::move(moved);
    CHECK(moved.state() == SketchState::NeedsRecovery);
    check_signature(assigned, {7, 11});

    // Fault in a later coordinate must block reads until every buffer is rebuilt.
    Sketch multi(1, family<Hash>({Hash{}, Hash{Value{15}, Value{15}}, Hash{}}));
    multi.restore(std::vector<Element>{1, 2});
    CHECK(multi.erase(2) == UpdateResult::NeedsRecovery);
    multi.restore(std::vector<Element>{1});
    check_signature(multi, {1});
}

template<ElementType Element, class Hash>
void legal_randomized_cases(std::shared_ptr<const HashFamily<Hash>> hashes) {
    for (std::size_t capacity : {1u, 2u, 7u, 128u}) {
        LegalBufferedMinhash<Element, BufferKind::Array, Hash> array(capacity, hashes);
        LegalBufferedMinhash<Element, BufferKind::Tree, Hash> tree(capacity, hashes);
        BufferedMinhash<Element, BufferKind::Array, Hash> general_array(capacity, hashes);
        BufferedMinhash<Element, BufferKind::Tree, Hash> general_tree(capacity, hashes);
        std::set<Element> elements;
        std::mt19937_64 generator(1234 + capacity);
        for (std::size_t step = 0; step < 2500; ++step) {
            Element value = static_cast<Element>(generator() % 64);
            if (step % 11 == 0) {
                value = static_cast<Element>(std::numeric_limits<Element>::max() - value);
            }
            const bool inserting = !elements.contains(value);
            if (inserting) { elements.insert(value); }
            else { elements.erase(value); }
            auto update = [&](auto& sketch) {
                if (inserting) { CHECK(sketch.insert(value) == UpdateResult::Ok); }
                else if (sketch.erase(value) == UpdateResult::NeedsRecovery) {
                    sketch.restore(elements);
                }
                check_signature(sketch, elements);
            };
            // Fault timing is allowed to differ between contracts.
            update(array);
            update(tree);
            update(general_array);
            update(general_tree);
            CHECK(similarity(array, general_tree) == 1.0);
            CHECK(similarity(general_array, tree) == 1.0);
            CHECK(array.copy_signature() == tree.copy_signature());
        }
    }
}

template<ElementType Element, class Hash>
void randomized_cases(std::shared_ptr<const HashFamily<Hash>> hashes) {
    for (std::size_t capacity : {1u, 2u, 7u, 128u}) {
        BufferedMinhash<Element, BufferKind::Array, Hash> array(capacity, hashes);
        BufferedMinhash<Element, BufferKind::Tree, Hash> tree(capacity, hashes);
        std::set<Element> elements;
        std::mt19937_64 generator(1234 + capacity);
        for (std::size_t step = 0; step < 2500; ++step) {
            Element value = static_cast<Element>(generator() % 64);
            if (step % 11 == 0) {
                value = static_cast<Element>(std::numeric_limits<Element>::max() - value);
            }
            if ((generator() & 1) != 0) {
                elements.insert(value);
                CHECK(array.insert(value) == UpdateResult::Ok);
                CHECK(tree.insert(value) == UpdateResult::Ok);
            } else {
                elements.erase(value);
                const auto a = array.erase(value);
                const auto b = tree.erase(value);
                CHECK(a == b);
                if (a == UpdateResult::NeedsRecovery) {
                    array.restore(elements);
                    tree.restore(elements);
                }
            }
            check_signature(array, elements);
            check_signature(tree, elements);
            CHECK(array.copy_signature() == tree.copy_signature());
        }
    }
}

// Test-only counters do not affect hash outputs or configuration equality.
template<ElementType Element, HashValueType Value>
struct CountingHash {
    using element_type = Element;
    using hash_type = Value;
    TestHash<Element, Value> function;
    std::shared_ptr<std::array<std::size_t, 4>> calls;
    std::size_t index;

    Value operator()(Element element) const {
        ++(*calls)[index];
        return function(element);
    }
    bool operator==(const CountingHash& other) const { return function == other.function; }
};

template<BufferKind Kind, StreamKind Stream, ElementType Element, HashValueType Value>
void buffered_policy_cases() {
    using Hash = CountingHash<Element, Value>;
    using Sketch = BufferedMinhash<Element, Kind, Hash, Stream>;
    auto calls = std::make_shared<std::array<std::size_t, 4>>();
    constexpr Value maximum = std::numeric_limits<Value>::max();
    const auto hashes = family<Hash>({
        {{maximum, 0}, calls, 0}, {{maximum, 3}, calls, 1},
        {{maximum, 2}, calls, 2}, {{maximum, 0}, calls, 3}});
    check_throws<std::invalid_argument>([&] {
        Sketch invalid(2, hashes, static_cast<RecoveryPolicy>(99));
    });
    Sketch full(2, hashes);
    CHECK(full.recovery_policy() == RecoveryPolicy::Full);
    Sketch lazy(2, hashes, RecoveryPolicy::EmptyOnly);
    CHECK(lazy.compatible_with(full));
    for (auto* sketch : {&full, &lazy}) {
        CHECK(sketch->erase(9) == UpdateResult::Ok); // Known empty is not a fault.
        sketch->restore(std::vector<Element>{0, 1, 2, 3});
        CHECK(sketch->erase(3) == UpdateResult::Ok);
        CHECK(sketch->erase(1) == UpdateResult::Ok);
        calls->fill(0);
        CHECK(sketch->erase(0) == UpdateResult::NeedsRecovery);
        const bool selective = sketch->recovery_policy() == RecoveryPolicy::EmptyOnly;
        CHECK(*calls == (selective ? std::array<std::size_t, 4>{1, 1, 1, 1}
                                  : std::array<std::size_t, 4>{1, 0, 0, 0}));
        check_throws<std::logic_error>([&] { (void)sketch->signature(); });
        check_throws<std::logic_error>([&] { (void)sketch->insert(4); });
        check_throws<std::logic_error>([&] { (void)sketch->erase(2); });
    }
    Sketch fault_copy(lazy);
    Sketch assigned(2, hashes);
    assigned = lazy;
    Sketch moved(std::move(assigned));
    check_throws<std::logic_error>([&] { assigned.restore(std::vector<Element>{2}); });
    assigned = std::move(moved);
    CHECK(assigned.recovery_policy() == RecoveryPolicy::EmptyOnly);
    for (auto* sketch : {&full, &lazy, &fault_copy, &assigned}) {
        calls->fill(0);
        sketch->restore(std::vector<Element>{2});
        CHECK(*calls == (sketch->recovery_policy() == RecoveryPolicy::Full
            ? std::array<std::size_t, 4>{1, 1, 1, 1} : std::array<std::size_t, 4>{1, 0, 0, 1}));
        check_signature(*sketch, {2});
    }
    CHECK(similarity(full, lazy) == 1.0);

    // Surviving buffers 1 and 2 are PARTIAL and retain finite thresholds.
    // Rebuilt buffers 0 and 3 are partial with infinite thresholds. Inserting
    // 4 must therefore enter only 0 and 3; deleting 2 faults only 1 and 2.
    CHECK(lazy.insert(4) == UpdateResult::Ok);
    check_signature(lazy, {2, 4});
    CHECK(lazy.erase(2) == UpdateResult::NeedsRecovery);
    calls->fill(0);
    auto failing_scan = std::views::iota(0, 2) | std::views::transform([](int i) -> Element {
        if (i == 1) { throw std::runtime_error("interrupted recovery"); }
        return Element{4};
    });
    check_throws<std::runtime_error>([&] { lazy.restore(failing_scan); });
    CHECK(lazy.state() == SketchState::NeedsRecovery);
    CHECK(*calls == std::array<std::size_t, 4>{0, 1, 1, 0});
    Sketch interrupted_copy(lazy);
    for (auto* sketch : {&lazy, &interrupted_copy}) {
        calls->fill(0);
        sketch->restore(std::vector<Element>{4});
        CHECK(*calls == std::array<std::size_t, 4>{0, 1, 1, 0});
        check_signature(*sketch, {4});
        CHECK(sketch->erase(4) == UpdateResult::NeedsRecovery);
        sketch->restore(std::vector<Element>{});
        check_signature(*sketch, {});
    }

    // A Ready-state replacement must target ALL coordinates, also on retry.
    check_throws<std::runtime_error>([&] { lazy.restore(failing_scan); });
    calls->fill(0);
    lazy.restore(std::vector<Element>{4});
    CHECK(*calls == std::array<std::size_t, 4>{1, 1, 1, 1});
    check_signature(lazy, {4});
    CHECK(lazy.erase(4) == UpdateResult::NeedsRecovery);
    lazy.restore(std::vector<Element>{});
    check_signature(lazy, {});

    // Fixed repair targets must receive the WHOLE one-pass input, not just its
    // first element. Full survivors also remain untouched by selective repair.
    Sketch many(2, hashes, RecoveryPolicy::EmptyOnly);
    many.restore(std::vector<Element>{0, 1, 2, 3});
    CHECK(many.erase(1) == UpdateResult::Ok);
    CHECK(many.erase(0) == UpdateResult::NeedsRecovery);
    calls->fill(0);
    std::istringstream input("3 2");
    many.restore(std::istream_iterator<Element>{input}, std::istream_iterator<Element>{});
    CHECK(*calls == std::array<std::size_t, 4>{2, 0, 0, 2});
    check_signature(many, {2, 3});
    CHECK(many.erase(2) == UpdateResult::Ok);
    check_signature(many, {3});
}

template<ElementType Element, HashValueType Value>
void buffered_policy_width_cases() {
    buffered_policy_cases<BufferKind::Array, StreamKind::General, Element, Value>();
    buffered_policy_cases<BufferKind::Tree, StreamKind::General, Element, Value>();
    buffered_policy_cases<BufferKind::Array, StreamKind::Legal, Element, Value>();
    buffered_policy_cases<BufferKind::Tree, StreamKind::Legal, Element, Value>();
}

template<ElementType Element, HashValueType Value>
void vanilla_selective_recovery_cases() {
    using Hash = CountingHash<Element, Value>;
    using Sketch = Minhash<Element, Hash>;
    auto calls = std::make_shared<std::array<std::size_t, 4>>();
    const auto hashes = family<Hash>({
        {TestHash<Element, Value>{}, calls, 0},
        {{Value{3}, Value{3}}, calls, 1},
        {{Value{3}, Value{2}}, calls, 2},
        {TestHash<Element, Value>{}, calls, 3}});
    Sketch sketch(hashes);
    check_signature(sketch, {});
    CHECK(sketch.erase(0) == UpdateResult::Ok);
    sketch.restore(std::vector<Element>{0, 1, 2, 3});
    CHECK(*calls == std::array<std::size_t, 4>{4, 4, 4, 4});
    CHECK(*sketch.copy_signature() == std::vector<Value>({0, 0, 0, 0}));
    calls->fill(0);
    CHECK(sketch.erase(0) == UpdateResult::NeedsRecovery);
    CHECK(*calls == std::array<std::size_t, 4>{0, 0, 0, 0});
    check_throws<std::logic_error>([&] { (void)sketch.signature(); });
    check_throws<std::logic_error>([&] { (void)sketch.copy_signature(); });
    check_throws<std::logic_error>([&] { (void)sketch.insert(4); });
    check_throws<std::logic_error>([&] { (void)sketch.erase(3); });

    // Both the first AND last coordinates must be recovered. The two valid
    // coordinates must not evaluate their hashes at all, even while scanning.
    Sketch fault_copy(sketch);
    sketch.restore(std::vector<Element>{1, 2, 3});
    CHECK(*calls == std::array<std::size_t, 4>{3, 0, 0, 3});
    CHECK(*sketch.copy_signature() == std::vector<Value>({1, 0, 0, 1}));
    check_signature(sketch, {1, 2, 3});
    CHECK(fault_copy.state() == SketchState::NeedsRecovery);
    calls->fill(0);
    fault_copy.restore(std::vector<Element>{1, 2, 3});
    CHECK(*calls == std::array<std::size_t, 4>{3, 0, 0, 3});

    calls->fill(0);
    CHECK(sketch.erase(3) == UpdateResult::NeedsRecovery);
    sketch.restore(std::vector<Element>{1, 2});
    CHECK(*calls == std::array<std::size_t, 4>{0, 2, 0, 0});
    CHECK(*sketch.copy_signature() == std::vector<Value>({1, 1, 0, 1}));
    check_signature(sketch, {1, 2});

    // An unsuccessful one-pass scan keeps exactly the original repair targets.
    // Its temporary candidates must be discarded when retrying the same set.
    CHECK(sketch.erase(1) == UpdateResult::NeedsRecovery);
    calls->fill(0);
    auto failing_scan = std::views::iota(0, 2) | std::views::transform([](int index) -> Element {
        if (index == 1) { throw std::runtime_error("test traversal error"); }
        return Element{2};
    });
    check_throws<std::runtime_error>([&] { sketch.restore(failing_scan); });
    CHECK(sketch.state() == SketchState::NeedsRecovery);
    CHECK(*calls == std::array<std::size_t, 4>{1, 0, 0, 1});
    calls->fill(0);
    sketch.restore(std::vector<Element>{2});
    CHECK(*calls == std::array<std::size_t, 4>{1, 0, 0, 1});
    check_signature(sketch, {2});
    CHECK(sketch.erase(2) == UpdateResult::NeedsRecovery);
    calls->fill(0);
    sketch.restore(std::vector<Element>{});
    CHECK(*calls == std::array<std::size_t, 4>{0, 0, 0, 0});
    check_signature(sketch, {});
    CHECK(sketch.insert(0) == UpdateResult::Ok);
    check_signature(sketch, {0});
}

template<ElementType Element, HashValueType Value>
void vanilla_general_cases() {
    using Hash = TestHash<Element, Value>;
    using Sketch = Minhash<Element, Hash>;
    const auto hashes = family<Hash>({Hash{Value{0}, Value{0}}, Hash{}});
    Sketch sketch(hashes);
    CHECK(sketch.hash_family() == hashes);
    CHECK(sketch.signature_length() == 2);
    CHECK(sketch.insert(2) == UpdateResult::Ok);
    CHECK(sketch.insert(1) == UpdateResult::Ok);
    CHECK(sketch.insert(1) == UpdateResult::Ok);
    CHECK(sketch.erase(3) == UpdateResult::Ok); // Absent element with a colliding hash.
    CHECK(sketch.erase(2) == UpdateResult::Ok); // Present, but not a winner.
    CHECK(sketch.erase(2) == UpdateResult::Ok);
    check_signature(sketch, {1});
    const auto saved = sketch.copy_signature();
    CHECK(sketch.erase(1) == UpdateResult::NeedsRecovery);
    sketch.restore(std::vector<Element>{});
    check_signature(sketch, {});
    CHECK((*saved)[0] == 0 && (*saved)[1] == 1);

    // Ready-state restore is full initialization/replacement; duplicates allowed.
    std::istringstream input("7 2 7 11");
    sketch.restore(std::istream_iterator<Element>{input}, std::istream_iterator<Element>{});
    check_signature(sketch, {2, 7, 11});
    Sketch copied(sketch);
    CHECK(copied.erase(2) == UpdateResult::NeedsRecovery);
    copied.restore(std::vector<Element>{7, 11, 7});
    check_signature(copied, {7, 11});
    check_signature(sketch, {2, 7, 11});
    Sketch assigned(hashes);
    assigned = copied;
    CHECK(assigned.hash_family() == hashes);
    CHECK(assigned.erase(7) == UpdateResult::NeedsRecovery);
    Sketch moved(std::move(assigned));
    CHECK(assigned.state() == SketchState::NeedsRecovery);
    CHECK(assigned.signature_length() == 0);
    CHECK(!assigned.hash_family());
    CHECK(!assigned.compatible_with(moved));
    check_throws<std::logic_error>([&] { assigned.restore(std::vector<Element>{}); });
    check_throws<std::logic_error>([&] { (void)assigned.signature(); });
    moved.restore(std::vector<Element>{11});
    assigned = std::move(moved);
    check_signature(assigned, {11});
    CHECK(moved.state() == SketchState::NeedsRecovery);
    moved = sketch;
    check_signature(moved, {2, 7, 11});

    const Element maximum = std::numeric_limits<Element>::max();
    sketch.restore(std::vector<Element>{maximum});
    check_signature(sketch, {maximum});
    CHECK(sketch.erase(maximum) == UpdateResult::NeedsRecovery);
    sketch.restore(std::vector<Element>{});
    Sketch max_hash(family<Hash>({Hash{std::numeric_limits<Value>::max(),
                                         std::numeric_limits<Value>::max()}}));
    CHECK(max_hash.insert(0) == UpdateResult::Ok);
    CHECK((*max_hash.signature())[0] == std::numeric_limits<Value>::max());
    CHECK(max_hash.erase(0) == UpdateResult::NeedsRecovery);
    max_hash.restore(std::vector<Element>{});
    CHECK(!max_hash.signature());
}

void vanilla_configuration_cases() {
    static_assert(std::same_as<Minhash<>::element_type, std::uint32_t>);
    static_assert(std::same_as<Minhash<>::hash_type, std::uint64_t>);
    static_assert(!std::default_initializable<Minhash<>>);
    Minhash<> automatic(3);
    Minhash<> shared(automatic.hash_family());
    CHECK(automatic.compatible_with(shared));
    Minhash<> a(3, 42), b(make_hash_family<>(3, 42)), wrong_seed(3, 43), wrong_k(2, 42);
    CHECK(a.compatible_with(b));
    CHECK(similarity(a, b) == 1.0);
    CHECK(!a.compatible_with(wrong_seed));
    CHECK(!a.compatible_with(wrong_k));
    check_throws<std::invalid_argument>([&] { (void)similarity(a, wrong_seed); });
    CHECK(a.insert(5) == UpdateResult::Ok);
    CHECK(similarity(a, b) == 0.0);
    CHECK(a.erase(5) == UpdateResult::NeedsRecovery);
    check_throws<std::logic_error>([&] { (void)similarity(a, b); });
    check_throws<std::invalid_argument>([] { Minhash<> invalid(std::size_t{0}); });
    check_throws<std::invalid_argument>([] { Minhash<> invalid(Minhash<>::hash_family_ptr{}); });
}

template<ElementType Element, class Hash>
void vanilla_stream_cases(std::shared_ptr<const HashFamily<Hash>> hashes) {
    Minhash<Element, Hash> vanilla(hashes);
    BufferedMinhash<Element, BufferKind::Array, Hash> array(1, hashes);
    LegalBufferedMinhash<Element, BufferKind::Tree, Hash> legal(3, hashes);
    CHECK(array.compatible_with(vanilla) && vanilla.compatible_with(array));
    CHECK(legal.compatible_with(vanilla) && vanilla.compatible_with(legal));
    std::set<Element> elements;
    std::mt19937_64 generator(456);
    for (std::size_t step = 0; step < 1500; ++step) {
        Element element = static_cast<Element>(generator() % 32);
        if (step % 11 == 0) { element = std::numeric_limits<Element>::max() - element; }
        const bool insertion = (generator() & 1) != 0;
        const bool changed = insertion ? elements.insert(element).second : elements.erase(element) != 0;
        auto update = [&](auto& sketch) {
            if (insertion) { CHECK(sketch.insert(element) == UpdateResult::Ok); }
            else if (sketch.erase(element) == UpdateResult::NeedsRecovery) { sketch.restore(elements); }
            check_signature(sketch, elements);
        };
        update(vanilla);
        update(array);
        if (changed) { update(legal); }
        CHECK(similarity(vanilla, array) == 1.0 && similarity(array, vanilla) == 1.0);
        CHECK(similarity(vanilla, legal) == 1.0 && similarity(legal, vanilla) == 1.0);
    }
}

void vanilla_exhaustive_cases() {
    using Hash = TestHash<std::uint32_t>;
    using Sketch = Minhash<std::uint32_t, Hash>;
    const auto hashes = family<Hash>({Hash{}, Hash{0, 0}, Hash{3, 3}, Hash{1, 0}});
    auto visit = [&](auto&& self, const Sketch& sketch,
                     const std::set<std::uint32_t>& elements, int depth) -> void {
        check_signature(sketch, elements);
        if (depth == 0) { return; }
        for (std::uint32_t element = 0; element < 3; ++element) {
            for (bool insertion : {false, true}) {
                Sketch next(sketch);
                auto expected = elements;
                if (insertion) {
                    expected.insert(element);
                    CHECK(next.insert(element) == UpdateResult::Ok);
                } else {
                    expected.erase(element);
                    if (next.erase(element) == UpdateResult::NeedsRecovery) { next.restore(expected); }
                }
                self(self, next, expected, depth - 1);
            }
        }
    };
    visit(visit, Sketch(hashes), {}, 6);
}

int main() {
    try {
        deterministic_cases<BufferKind::Array, std::uint32_t, std::uint32_t>();
        deterministic_cases<BufferKind::Tree, std::uint32_t, std::uint32_t>();
        deterministic_cases<BufferKind::Array, std::uint32_t, std::uint64_t>();
        deterministic_cases<BufferKind::Tree, std::uint32_t, std::uint64_t>();
        deterministic_cases<BufferKind::Array, std::uint64_t, std::uint32_t>();
        deterministic_cases<BufferKind::Tree, std::uint64_t, std::uint32_t>();
        deterministic_cases<BufferKind::Array, std::uint64_t, std::uint64_t>();
        deterministic_cases<BufferKind::Tree, std::uint64_t, std::uint64_t>();
        configuration_cases();
        exhaustive_cases<BufferKind::Array>();
        exhaustive_cases<BufferKind::Tree>();
        legal_deterministic_cases<BufferKind::Array, std::uint32_t, std::uint32_t>();
        legal_deterministic_cases<BufferKind::Tree, std::uint32_t, std::uint32_t>();
        legal_deterministic_cases<BufferKind::Array, std::uint32_t, std::uint64_t>();
        legal_deterministic_cases<BufferKind::Tree, std::uint32_t, std::uint64_t>();
        legal_deterministic_cases<BufferKind::Array, std::uint64_t, std::uint32_t>();
        legal_deterministic_cases<BufferKind::Tree, std::uint64_t, std::uint32_t>();
        legal_deterministic_cases<BufferKind::Array, std::uint64_t, std::uint64_t>();
        legal_deterministic_cases<BufferKind::Tree, std::uint64_t, std::uint64_t>();
        exhaustive_cases<BufferKind::Array, StreamKind::Legal>();
        exhaustive_cases<BufferKind::Tree, StreamKind::Legal>();
        exhaustive_cases<BufferKind::Array>(RecoveryPolicy::EmptyOnly);
        exhaustive_cases<BufferKind::Tree>(RecoveryPolicy::EmptyOnly);
        exhaustive_cases<BufferKind::Array, StreamKind::Legal>(RecoveryPolicy::EmptyOnly);
        exhaustive_cases<BufferKind::Tree, StreamKind::Legal>(RecoveryPolicy::EmptyOnly);
        buffered_policy_width_cases<std::uint32_t, std::uint32_t>();
        buffered_policy_width_cases<std::uint32_t, std::uint64_t>();
        buffered_policy_width_cases<std::uint64_t, std::uint32_t>();
        buffered_policy_width_cases<std::uint64_t, std::uint64_t>();
        randomized_cases<std::uint32_t>(make_hash_family<>(5, 42));
        randomized_cases<std::uint64_t>(make_hash_family<std::uint64_t>(5, 42));
        using Colliding = TestHash<std::uint32_t>;
        randomized_cases<std::uint32_t>(family<Colliding>({Colliding{3, 0}, Colliding{1, 1}}));
        legal_randomized_cases<std::uint32_t>(make_hash_family<>(5, 42));
        legal_randomized_cases<std::uint64_t>(make_hash_family<std::uint64_t>(5, 42));
        legal_randomized_cases<std::uint32_t>(family<Colliding>({Colliding{3, 0}, Colliding{1, 1}}));
        vanilla_selective_recovery_cases<std::uint32_t, std::uint32_t>();
        vanilla_selective_recovery_cases<std::uint32_t, std::uint64_t>();
        vanilla_selective_recovery_cases<std::uint64_t, std::uint32_t>();
        vanilla_selective_recovery_cases<std::uint64_t, std::uint64_t>();
        vanilla_general_cases<std::uint32_t, std::uint32_t>();
        vanilla_general_cases<std::uint32_t, std::uint64_t>();
        vanilla_general_cases<std::uint64_t, std::uint32_t>();
        vanilla_general_cases<std::uint64_t, std::uint64_t>();
        vanilla_configuration_cases();
        vanilla_exhaustive_cases();
        vanilla_stream_cases<std::uint32_t>(make_hash_family<>(4, 42));
        vanilla_stream_cases<std::uint64_t>(make_hash_family<std::uint64_t>(4, 42));
        vanilla_stream_cases<std::uint32_t>(family<Colliding>({Colliding{3, 0}, Colliding{1, 1}}));
        std::cout << "All buffered and vanilla Minhash correctness checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
