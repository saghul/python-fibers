
import gc
import sys
import threading
import unittest
import weakref

from fibers import Fiber, current


class ArgRefcountTests(unittest.TestCase):

    def test_arg_refs(self):
        args = ('a', 'b', 'c')
        refcount_before = sys.getrefcount(args)
        g = Fiber(target=lambda *x: None, args=args)
        self.assertEqual(sys.getrefcount(args), refcount_before+1)
        g.switch()
        self.assertEqual(sys.getrefcount(args), refcount_before)
        del g
        self.assertEqual(sys.getrefcount(args), refcount_before)

    def test_kwarg_refs(self):
        kwargs = {'a': 1234}
        refcount_before = sys.getrefcount(kwargs)
        g = Fiber(lambda **x: None, kwargs=kwargs)
        self.assertEqual(sys.getrefcount(kwargs), refcount_before+1)
        g.switch()
        self.assertEqual(sys.getrefcount(kwargs), refcount_before)
        del g
        self.assertEqual(sys.getrefcount(kwargs), refcount_before)

    def test_threaded_leak(self):
        gg = []
        def worker():
            # only main greenlet present
            gg.append(weakref.ref(current()))
        for i in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
        current() # update ts_current
        gc.collect()
        for g in gg:
            self.assertTrue(g() is None)

    def test_threaded_adv_leak(self):
        gg = []
        def worker():
            # main and additional *finished* greenlets
            ll = current().ll = []
            def additional():
                ll.append(current())
            for i in range(2):
                Fiber(additional).switch()
            gg.append(weakref.ref(current()))
        for i in range(2):
            t = threading.Thread(target=worker)
            t.start()
            t.join()
        current() # update ts_current
        gc.collect()
        for g in gg:
            self.assertTrue(g() is None)


if __name__ == '__main__':
    unittest.main(verbosity=2)

