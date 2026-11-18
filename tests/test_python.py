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


if __name__ == "__main__":
    unittest.main()
