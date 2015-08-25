
import unittest

import os
import sys

if 'IS_TOX' not in os.environ:
    sys.path.insert(0, '../')

import fibers
from fibers import Fiber


class genlet(Fiber):

    def __init__(self, *args, **kwds):
        self.args = args
        self.kwds = kwds
        Fiber.__init__(self, target=self.run)

    def run(self):
        fn, = self.fn
        fn(*self.args, **self.kwds)

    def __iter__(self):
        return self

    def __next__(self):
        self.parent = fibers.current()
        result = self.switch()
        if self.is_alive():
            return result
        else:
            raise StopIteration

    # Hack: Python < 2.6 compatibility
    next = __next__


def Yield(value):
    g = fibers.current()
    while not isinstance(g, genlet):
        if g is None:
            raise RuntimeError('yield outside a genlet')
        g = g.parent
    g.parent.switch(value)


def generator(func):
    class generator(genlet):
        fn = (func,)
    return generator

# ____________________________________________________________


class GeneratorTests(unittest.TestCase):
    def test_generator(self):
        seen = []

        def g(n):
            for i in range(n):
                seen.append(i)
                Yield(i)
        g = generator(g)
        for k in range(3):
            for j in g(5):
                seen.append(j)
        self.assertEqual(seen, 3 * [0, 0, 1, 1, 2, 2, 3, 3, 4, 4])


if __name__ == '__main__':
    unittest.main(verbosity=2)

