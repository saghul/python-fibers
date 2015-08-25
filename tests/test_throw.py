
import unittest

import os
import sys

if 'IS_TOX' not in os.environ:
    sys.path.insert(0, '../')

import fibers
from fibers import Fiber


def switch(val):
    return fibers.current().parent.switch(val)


class ThrowTests(unittest.TestCase):
    def test_class(self):
        def f():
            try:
                switch("ok")
            except RuntimeError:
                switch("ok")
                return
            switch("fail")
        g = Fiber(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(RuntimeError)
        self.assertEqual(res, "ok")

    def test_val(self):
        def f():
            try:
                switch("ok")
            except RuntimeError:
                val = sys.exc_info()[1]
                if str(val) == "ciao":
                    switch("ok")
                    return
            switch("fail")

        g = Fiber(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(RuntimeError("ciao"))
        self.assertEqual(res, "ok")

        g = Fiber(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(RuntimeError, "ciao")
        self.assertEqual(res, "ok")

    def test_kill(self):
        def f():
            try:
                switch("ok")
                switch("fail")
            except Exception as e:
                return e
        g = Fiber(f)
        res = g.switch()
        self.assertEqual(res, "ok")
        res = g.throw(ValueError)
        self.assertTrue(isinstance(res, ValueError))
        self.assertFalse(g.is_alive())

    def test_throw_goes_to_original_parent(self):
        main = fibers.current()

        def f1():
            try:
                main.switch("f1 ready to catch")
            except IndexError:
                return "caught"
            else:
                return "normal exit"

        def f2():
            main.switch("from f2")

        g1 = Fiber(f1)
        g2 = Fiber(target=f2, parent=g1)
        self.assertRaises(IndexError, g2.throw, IndexError)
        self.assertFalse(g2.is_alive())
        self.assertTrue(g1.is_alive())    # g1 is skipped because it was not started

    def test_throw_goes_to_original_parent2(self):
        main = fibers.current()

        def f1():
            try:
                main.switch("f1 ready to catch")
            except IndexError:
                return "caught"
            else:
                return "normal exit"

        def f2():
            main.switch("from f2")

        g1 = Fiber(f1)
        g2 = Fiber(target=f2, parent=g1)
        res = g1.switch()
        self.assertEqual(res, "f1 ready to catch")
        res = g2.throw(IndexError)
        self.assertEqual(res, "caught")
        self.assertFalse(g2.is_alive())
        self.assertFalse(g1.is_alive())

    def test_throw_goes_to_original_parent3(self):
        main = fibers.current()

        def f1():
            try:
                main.switch("f1 ready to catch")
            except IndexError:
                return "caught"
            else:
                return "normal exit"

        def f2():
            main.switch("from f2")

        g1 = Fiber(f1)
        g2 = Fiber(target=f2, parent=g1)
        res = g1.switch()
        self.assertEqual(res, "f1 ready to catch")
        res = g2.switch()
        self.assertEqual(res, "from f2")
        res = g2.throw(IndexError)
        self.assertEqual(res, "caught")
        self.assertFalse(g2.is_alive())
        self.assertFalse(g1.is_alive())


if __name__ == '__main__':
    unittest.main(verbosity=2)

