#include <buffered_minhash/buffered_minhash.hpp>

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
void exhaustive_cases() {
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
            visit(visit, Sketch(capacity, hashes), {}, Stream == StreamKind::Legal ? 9 : 6);
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
        randomized_cases<std::uint32_t>(make_hash_family<>(5, 42));
        randomized_cases<std::uint64_t>(make_hash_family<std::uint64_t>(5, 42));
        using Colliding = TestHash<std::uint32_t>;
        randomized_cases<std::uint32_t>(family<Colliding>({Colliding{3, 0}, Colliding{1, 1}}));
        legal_randomized_cases<std::uint32_t>(make_hash_family<>(5, 42));
        legal_randomized_cases<std::uint64_t>(make_hash_family<std::uint64_t>(5, 42));
        legal_randomized_cases<std::uint32_t>(family<Colliding>({Colliding{3, 0}, Colliding{1, 1}}));
        std::cout << "All BufferedMinhash correctness checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
