#include <buffered_minhash/buffered_minhash.hpp>

#include <cstdint>
#include <iostream>
#include <unordered_set>

int main() {
    using namespace buffered_minhash;
    int buffer_size = 8;
    int seed = 42;
    int k = 8;
    auto hashes = make_hash_family<>(k, seed);
    BufferedMinhash<> sketch(buffer_size, hashes);
    std::unordered_set<std::uint32_t> elements;

    // The application owns the authoritative set and changes it first.
    for (std::uint32_t value : {10u, 20u, 10u, 30u}) {
        elements.insert(value);
        (void)sketch.insert(value); // Redundant insertions are supported.
    }

    BufferedMinhash<std::uint32_t, BufferKind::Tree> peer(4, hashes);
    peer.restore(elements);
    std::cout << "Initial similarity: " << similarity(sketch, peer) << '\n';

    for (std::uint32_t value : {10u, 10u, 20u, 30u}) {
        elements.erase(value);
        if (sketch.erase(value) == UpdateResult::NeedsRecovery) {
            sketch.restore(elements);
            std::cout << "Recovered from the current set\n";
        }
    }
    std::cout << "Known empty: " << std::boolalpha << !sketch.signature() << '\n';
    return sketch.signature() ? 1 : 0;
}
