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
        for family, array, tree, _ in self.variants():
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
        for _, array, tree, bits in self.variants():
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


if __name__ == "__main__":
    unittest.main()
