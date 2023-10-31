
import unittest

import os
import sys

import fibers
from fibers import Fiber
import pytest


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
        assert res == "ok"
        res = g.throw(RuntimeError)
        assert res == "ok"

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
        assert res == "ok"
        res = g.throw(RuntimeError("ciao"))
        assert res == "ok"

        g = Fiber(f)
        res = g.switch()
        assert res == "ok"
        res = g.throw(RuntimeError, "ciao")
        assert res == "ok"

    def test_kill(self):
        def f():
            try:
                switch("ok")
                switch("fail")
            except Exception as e:
                return e
        g = Fiber(f)
        res = g.switch()
        assert res == "ok"
        res = g.throw(ValueError)
        assert isinstance(res, ValueError)
        assert not g.is_alive()

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
        with pytest.raises(IndexError):
            g2.throw(IndexError)
        assert not g2.is_alive()
        assert g1.is_alive()    # g1 is skipped because it was not started

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
        assert res == "f1 ready to catch"
        res = g2.throw(IndexError)
        assert res == "caught"
        assert not g2.is_alive()
        assert not g1.is_alive()

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
        assert res == "f1 ready to catch"
        res = g2.switch()
        assert res == "from f2"
        res = g2.throw(IndexError)
        assert res == "caught"
        assert not g2.is_alive()
        assert not g1.is_alive()


if __name__ == '__main__':
    unittest.main(verbosity=2)

