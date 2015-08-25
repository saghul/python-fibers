
import gc
import unittest
import weakref

import os
import sys

if 'IS_TOX' not in os.environ:
    sys.path.insert(0, '../')

from fibers import Fiber, current


class GCTests(unittest.TestCase):

    def test_circular_fiber(self):
        class circular_fiber(Fiber):
            pass
        o = circular_fiber()
        o.self = o
        o = weakref.ref(o)
        gc.collect()
        self.assertTrue(o() is None)
        self.assertFalse(gc.garbage, gc.garbage)

    def test_dead_circular_ref(self):
        o = weakref.ref(Fiber(current).switch())
        gc.collect()
        self.assertTrue(o() is None)
        self.assertFalse(gc.garbage, gc.garbage)

    def test_inactive_ref(self):
        o = Fiber(lambda: None)
        o = weakref.ref(o)
        gc.collect()
        self.assertTrue(o() is None)
        self.assertFalse(gc.garbage, gc.garbage)

    def test_finalizer_crash(self):
        # This test is designed to crash when active greenlets
        # are made garbage collectable, until the underlying
        # problem is resolved. How does it work:
        # - order of object creation is important
        # - array is created first, so it is moved to unreachable first
        # - we create a cycle between a greenlet and this array
        # - we create an object that participates in gc, is only
        #   referenced by a greenlet, and would corrupt gc lists
        #   on destruction, the easiest is to use an object with
        #   a finalizer
        # - because array is the first object in unreachable it is
        #   cleared first, which causes all references to greenlet
        #   to disappear and causes greenlet to be destroyed, but since
        #   it is still live it causes a switch during gc, which causes
        #   an object with finalizer to be destroyed, which causes stack
        #   corruption and then a crash
        class object_with_finalizer(object):
            def __del__(self):
                pass
        array = []
        parent = current()
        def greenlet_body():
            current().object = object_with_finalizer()
            try:
                parent.switch()
            finally:
                del current().object
        g = Fiber(greenlet_body)
        g.array = array
        array.append(g)
        g.switch()
        del array
        del g
        current()
        gc.collect()


if __name__ == '__main__':
    unittest.main(verbosity=2)

