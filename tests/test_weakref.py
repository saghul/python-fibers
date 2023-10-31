
import gc
import weakref
import unittest

import os
import sys

import fibers
from fibers import Fiber


class WeakRefTests(unittest.TestCase):
    def test_dead_weakref(self):
        def _dead_fiber():
            g = Fiber(lambda: None)
            g.switch()
            return g
        o = weakref.ref(_dead_fiber())
        gc.collect()
        assert o() == None

    def test_inactive_weakref(self):
        o = weakref.ref(Fiber())
        gc.collect()
        assert o() == None


if __name__ == '__main__':
    unittest.main(verbosity=2)

