"""Binding correctness checks; uses only Python's standard library."""
import copy
import gc
import random
import sys
import unittest

# CTest supplies the extension directory; installed-wheel tests need no argument.
if len(sys.argv) > 1:
    sys.path.insert(0, sys.argv.pop(1))

import buffered_minhash as bmh


class BindingTests(unittest.TestCase):
    def variants(self):
        return (
            (bmh.HashFamily, bmh.BufferedMinhash, bmh.TreeBufferedMinhash, 32),
            (bmh.HashFamily64, bmh.BufferedMinhash64, bmh.TreeBufferedMinhash64, 64),
        )

    def legal_variants(self):
        return (
            (bmh.HashFamily, bmh.LegalBufferedMinhash, bmh.LegalTreeBufferedMinhash, 32),
            (bmh.HashFamily64, bmh.LegalBufferedMinhash64, bmh.LegalTreeBufferedMinhash64, 64),
        )

    def vanilla_variants(self):
        return (
            (bmh.HashFamily, bmh.Minhash, bmh.BufferedMinhash, 32),
            (bmh.HashFamily64, bmh.Minhash64, bmh.BufferedMinhash64, 64),
        )

    def test_recovery_and_signature_ownership(self):
        for family, array, tree, bits in self.variants():
            for cls in (array, tree):
                with self.subTest(cls=cls):
                    hashes = family(k=3, seed=42)
                    sketch = cls(capacity=2, hashes=hashes)
                    self.assertIsNone(sketch.signature())
                    self.assertEqual(sketch.erase(9), bmh.UpdateResult.Ok)
                    value = (1 << bits) - 1
                    sketch.insert(value)
                    sketch.insert(value)
                    saved = sketch.signature()
                    self.assertEqual(len(saved), 3)
                    self.assertEqual(sketch.erase(value), bmh.UpdateResult.NeedsRecovery)
                    self.assertEqual(sketch.state(), bmh.SketchState.NeedsRecovery)
                    for action in (sketch.signature, lambda: sketch.insert(3), lambda: sketch.erase(3)):
                        with self.assertRaises(RuntimeError):
                            action()
                    sketch.restore(iter(()))
                    self.assertIsNone(sketch.signature())
                    sketch.restore(x for x in [value, value])
                    self.assertEqual(sketch.signature(), saved)
                    saved[0] ^= 1
                    self.assertNotEqual(sketch.signature(), saved)
                    self.assertEqual(sketch.hash_family(), hashes)

    def test_configuration_and_copy(self):
        for family, array, tree, _ in self.variants() + self.legal_variants():
            hashes = family(4, seed=12)
            a, b = array(2, hashes), tree(5, family(4, seed=12))
            self.assertTrue(a.compatible_with(b))
            self.assertEqual(bmh.similarity(a, b), 1)
            a.restore({1, 2})
            self.assertEqual(bmh.similarity(a, b), 0)
            b.restore({1, 2})
            self.assertEqual(bmh.similarity(a, b), 1)
            for cloned in (a.copy(), copy.copy(a)):
                cloned.restore({7})
                self.assertEqual(a.signature(), b.signature())
            incompatible = array(4, 2, seed=13)
            self.assertFalse(a.compatible_with(incompatible))
            with self.assertRaises(ValueError):
                bmh.similarity(a, incompatible)
            with self.assertRaises(ValueError):
                family(0)
            with self.assertRaises(ValueError):
                array(0, hashes)
            automatic = array(k=4, capacity=2)
            peer = tree(capacity=2, hashes=automatic.hash_family())
            del automatic
            gc.collect()
            peer.restore({3})
            self.assertEqual(peer.signature_length(), 4)
            self.assertEqual(peer.buffer_capacity(), 2)

    def test_integer_validation(self):
        for _, array, tree, bits in self.variants() + self.legal_variants():
            for cls in (array, tree):
                sketch = cls(2, 2, seed=42)
                for value in (-1, 1 << bits):
                    with self.assertRaises(OverflowError):
                        sketch.insert(value)
                    with self.assertRaises(OverflowError):
                        sketch.erase(value)
                for value in (1.5, "2", True, None):
                    with self.assertRaises(TypeError):
                        sketch.insert(value)
                self.assertIsNone(sketch.signature())
                with self.assertRaises(OverflowError):
                    sketch.restore(iter([1, 1 << bits]))
                self.assertEqual(sketch.state(), bmh.SketchState.NeedsRecovery)
                sketch.restore([1])
                self.assertEqual(sketch.state(), bmh.SketchState.Ready)

    def test_streams_against_rebuilt_signatures(self):
        rng = random.Random(42)
        for family, array, tree, bits in self.variants():
            hashes = family(4, seed=42)
            a, b, reference = array(3, hashes), tree(3, hashes), array(3, hashes)
            elements = set()
            for _ in range(200):
                value = rng.randrange(16)
                if rng.randrange(4) == 0:
                    value += 1 << (bits - 1)
                if rng.randrange(2):
                    elements.add(value)
                    a.insert(value)
                    b.insert(value)
                else:
                    elements.discard(value)
                    result = a.erase(value)
                    self.assertEqual(result, b.erase(value))
                    if result == bmh.UpdateResult.NeedsRecovery:
                        a.restore(iter(elements))
                        b.restore(iter(elements))
                reference.restore(elements)
                self.assertEqual(a.signature(), reference.signature())
                self.assertEqual(b.signature(), reference.signature())

    def test_help(self):
        self.assertIn("capacity", bmh.BufferedMinhash.__init__.__doc__)
        self.assertIn("generators", bmh.BufferedMinhash.restore.__doc__)

    def test_legal_recovery(self):
        for family, array, tree, bits in self.legal_variants():
            for cls in (array, tree):
                with self.subTest(cls=cls):
                    hashes = family(k=3, seed=42)
                    sketch = cls(capacity=2, hashes=hashes)
                    value = (1 << bits) - 1
                    self.assertIsNone(sketch.signature())
                    self.assertEqual(sketch.insert(value), bmh.UpdateResult.Ok)
                    saved = sketch.signature()
                    self.assertEqual(sketch.erase(value), bmh.UpdateResult.NeedsRecovery)
                    self.assertEqual(sketch.state(), bmh.SketchState.NeedsRecovery)
                    for action in (sketch.signature, sketch.copy_signature,
                                   lambda: sketch.insert(0), lambda: sketch.erase(0)):
                        with self.assertRaises(RuntimeError):
                            action()
                    cloned = sketch.copy()
                    sketch.restore(iter(()))
                    self.assertIsNone(sketch.signature())
                    self.assertEqual(cloned.state(), bmh.SketchState.NeedsRecovery)
                    # Reinsertion after deletion is legal; no duplicate updates.
                    sketch.insert(value)
                    self.assertEqual(sketch.signature(), saved)
                    sketch.restore(x for x in [0, value])
                    reference = cls(3, 2, seed=42)
                    reference.restore({0, value})
                    self.assertEqual(sketch.signature(), reference.signature())
                    snapshot = sketch.signature()
                    snapshot[0] ^= 1
                    self.assertNotEqual(sketch.signature(), snapshot)
                    self.assertEqual(sketch.copy_signature(), reference.signature())
                    self.assertEqual(sketch.hash_family(), hashes)
                    self.assertIn("unchecked", cls.__doc__)
                    self.assertIn("absent", cls.insert.__doc__)
                    self.assertIn("present", cls.erase.__doc__)
                    self.assertIn("distinct", cls.restore.__doc__)

    def test_legal_streams_and_all_comparison_overloads(self):
        rng = random.Random(17)
        for general, legal in zip(self.variants(), self.legal_variants()):
            family, array, tree, bits = general
            _, legal_array, legal_tree, _ = legal
            classes = (array, tree, legal_array, legal_tree)
            hashes = family(4, seed=42)
            sketches = [cls(3, hashes) for cls in classes]
            reference = array(4, 3, seed=42)
            incompatible = [cls(4, 3, seed=43) for cls in classes]
            elements = set()
            for _ in range(200):
                value = rng.randrange(16)
                if rng.randrange(4) == 0:
                    value += 1 << (bits - 1)
                inserting = value not in elements
                if inserting:
                    elements.add(value)
                else:
                    elements.remove(value)
                for sketch in sketches:
                    if inserting:
                        self.assertEqual(sketch.insert(value), bmh.UpdateResult.Ok)
                    elif sketch.erase(value) == bmh.UpdateResult.NeedsRecovery:
                        sketch.restore(x for x in elements)
                reference.restore(elements)
                for left in sketches:
                    self.assertEqual(left.signature(), reference.signature())
                    for right in sketches:
                        self.assertTrue(left.compatible_with(right))
                        self.assertEqual(bmh.similarity(left, right), 1)
            for left in sketches:
                for right in incompatible:
                    self.assertFalse(left.compatible_with(right))
                    with self.assertRaises(ValueError):
                        bmh.similarity(left, right)
                for right in sketches:
                    left.restore(())
                    right.restore(())
                    self.assertEqual(bmh.similarity(left, right), 1)
                    if left is not right:
                        right.insert(0)
                        self.assertEqual(bmh.similarity(left, right), 0)
                        right.erase(0)
                        with self.assertRaises(RuntimeError):
                            bmh.similarity(left, right)
                        right.restore(())

    def test_vanilla_api_and_recovery(self):
        for family, cls, buffered, bits in self.vanilla_variants():
            with self.subTest(cls=cls):
                hashes = family(8, seed=42)
                sketch = cls(hashes=hashes)
                seeded = cls(k=8, seed=42)
                automatic = cls(k=8)
                self.assertEqual(sketch.signature_length(), 8)
                self.assertFalse(hasattr(sketch, "buffer_capacity"))
                self.assertEqual(sketch.hash_family(), hashes)
                self.assertTrue(sketch.compatible_with(seeded))
                self.assertTrue(automatic.compatible_with(cls(automatic.hash_family())))
                with self.assertRaises(ValueError):
                    cls(k=0)
                self.assertIsNone(sketch.signature())
                self.assertEqual(sketch.erase(0), bmh.UpdateResult.Ok)
                maximum = (1 << bits) - 1
                sketch.insert(maximum)
                sketch.insert(maximum)
                self.assertEqual(sketch.erase(0), bmh.UpdateResult.Ok)
                saved = sketch.signature()
                self.assertEqual(sketch.erase(maximum), bmh.UpdateResult.NeedsRecovery)
                for action in (sketch.signature, sketch.copy_signature,
                               lambda: sketch.insert(0), lambda: sketch.erase(maximum)):
                    with self.assertRaises(RuntimeError):
                        action()
                clone = sketch.copy()
                sketch.restore(iter(()))
                self.assertIsNone(sketch.signature())
                self.assertEqual(clone.state(), bmh.SketchState.NeedsRecovery)
                clone.restore(())
                sketch.restore(x for x in [maximum, maximum])
                self.assertEqual(sketch.signature(), saved)
                saved[0] ^= 1
                self.assertNotEqual(sketch.copy_signature(), saved)
                for clone in (sketch.copy(), copy.copy(sketch)):
                    clone.restore({0})
                    self.assertNotEqual(clone.signature(), sketch.signature())
                self.assertIn("only lost coordinates", cls.restore.__doc__)
                self.assertIn("seed", cls.__init__.__doc__)

    def test_vanilla_validation_and_failed_restore(self):
        for family, cls, buffered, bits in self.vanilla_variants():
            sketch = cls(8, seed=42)
            for value in (-1, 1 << bits):
                for operation in (sketch.insert, sketch.erase):
                    with self.assertRaises(OverflowError):
                        operation(value)
            for value in (True, 1.5, "1", None):
                for operation in (sketch.insert, sketch.erase):
                    with self.assertRaises(TypeError):
                        operation(value)
            with self.assertRaises(OverflowError):
                sketch.restore(iter([0, 1 << bits]))
            self.assertEqual(sketch.state(), bmh.SketchState.NeedsRecovery)
            sketch.restore({0, 1})

            # Pick a deletion that invalidates a proper subset of coordinates.
            hashes = sketch.hash_family()
            probe = cls(hashes)
            elements = set(range(32))
            singleton_signatures = {}
            for value in elements:
                probe.restore([value])
                singleton_signatures[value] = probe.signature()
            sketch.restore(elements)
            signature = sketch.signature()
            winner = next(value for value, hashes_of_value in singleton_signatures.items()
                          if 0 < sum(a == b for a, b in zip(hashes_of_value, signature)) < len(signature))
            elements.remove(winner)
            self.assertEqual(sketch.erase(winner), bmh.UpdateResult.NeedsRecovery)
            fault_copy = copy.copy(sketch)

            def failing_scan():
                yield min(elements)
                raise ValueError("interrupted authoritative scan")

            with self.assertRaisesRegex(ValueError, "interrupted authoritative scan"):
                sketch.restore(failing_scan())
            self.assertEqual(sketch.state(), bmh.SketchState.NeedsRecovery)
            sketch.restore(x for x in elements)
            fault_copy.restore(elements)
            reference = buffered(capacity=1, hashes=hashes)
            reference.restore(elements)
            self.assertEqual(sketch.signature(), reference.signature())
            self.assertEqual(fault_copy.signature(), reference.signature())

    def test_vanilla_streams_and_comparisons(self):
        rng = random.Random(456)
        for general, legal, vanilla in zip(self.variants(), self.legal_variants(), self.vanilla_variants()):
            family, array, tree, bits = general
            _, legal_array, legal_tree, _ = legal
            _, cls, _, _ = vanilla
            hashes = family(8, seed=42)
            sketch = cls(hashes)
            peers = [peer(3, hashes) for peer in (array, tree, legal_array, legal_tree)]
            peers.append(cls(hashes))
            reference = array(1, hashes)
            wrong = cls(8, seed=43)
            elements = set()
            for _ in range(300):
                value = rng.randrange(16)
                if rng.randrange(4) == 0:
                    value += 1 << (bits - 1)
                if rng.randrange(2):
                    elements.add(value)
                    sketch.insert(value)
                else:
                    elements.discard(value)
                    if sketch.erase(value) == bmh.UpdateResult.NeedsRecovery:
                        sketch.restore(x for x in elements)
                reference.restore(elements)
                self.assertEqual(sketch.signature(), reference.signature())
            for peer in peers:
                peer.restore(elements)
                self.assertTrue(sketch.compatible_with(peer))
                self.assertTrue(peer.compatible_with(sketch))
                self.assertEqual(bmh.similarity(sketch, peer), 1)
                self.assertEqual(bmh.similarity(peer, sketch), 1)
                self.assertFalse(peer.compatible_with(wrong))
                with self.assertRaises(ValueError):
                    bmh.similarity(peer, wrong)
                peer.restore(())
                self.assertEqual(bmh.similarity(sketch, peer), 0)
                self.assertEqual(bmh.similarity(peer, sketch), 0)
                peer.insert(0)
                peer.erase(0)
                with self.assertRaises(RuntimeError):
                    bmh.similarity(sketch, peer)
                with self.assertRaises(RuntimeError):
                    bmh.similarity(peer, sketch)
                peer.restore(())
            sketch.restore(())
            for peer in peers:
                self.assertEqual(bmh.similarity(sketch, peer), 1)

            retained = cls(hashes=cls(8).hash_family())
            gc.collect()
            retained.restore({3})
            self.assertEqual(len(retained.signature()), 8)


if __name__ == "__main__":
    unittest.main()
