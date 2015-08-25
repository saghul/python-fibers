
import copy
import time
import threading
import unittest

import os
import sys

if 'IS_TOX' not in os.environ:
    sys.path.insert(0, '../')

import fibers
from fibers import Fiber, current


is_pypy = hasattr(sys, 'pypy_version_info')


class SomeError(Exception):
    pass


def fmain(seen):
    try:
        current().parent.switch()
    except:
        seen.append(sys.exc_info()[0])
        raise
    raise SomeError


def send_exception(g, exc):
    # note: send_exception(g, exc)  can be now done with  g.throw(exc).
    # the purpose of this test is to explicitely check the propagation rules.
    def crasher(exc):
        raise exc
    g1 = Fiber(target=crasher, args=(exc, ), parent=g)
    g1.switch()


class FiberTests(unittest.TestCase):

    def test_simple(self):
        lst = []

        def f():
            lst.append(1)
            current().parent.switch()
            lst.append(3)
        g = Fiber(f)
        lst.append(0)
        g.switch()
        lst.append(2)
        g.switch()
        lst.append(4)
        self.assertEqual(lst, list(range(5)))

    def test_simple2(self):
        lst = []

        def f():
            lst.append(1)
            current().parent.switch()
            lst.append(3)
        g = Fiber(f)
        lst.append(0)
        g.switch()
        lst.append(2)
        g.switch()
        lst.append(4)
        self.assertEqual(lst, list(range(5)))

    def test_two_children(self):
        lst = []

        def f():
            lst.append(1)
            current().parent.switch()
            lst.extend([1, 1])
        g = Fiber(f)
        h = Fiber(f)
        g.switch()
        self.assertEqual(len(lst), 1)
        h.switch()
        self.assertEqual(len(lst), 2)
        h.switch()
        self.assertEqual(len(lst), 4)
        self.assertEqual(h.is_alive(), False)
        g.switch()
        self.assertEqual(len(lst), 6)
        self.assertEqual(g.is_alive(), False)

    def test_two_recursive_children(self):
        lst = []

        def f():
            lst.append(1)
            current().parent.switch()

        def h():
            lst.append(1)
            i = Fiber(f)
            i.switch()
            lst.append(1)
        g = Fiber(h)
        g.switch()
        self.assertEqual(len(lst), 3)

    def test_exception(self):
        seen = []
        g1 = Fiber(target=fmain, args=(seen, ))
        g2 = Fiber(target=fmain, args=(seen, ))
        g1.switch()
        g2.switch()
        g2.parent = g1
        self.assertEqual(seen, [])
        self.assertRaises(SomeError, g2.switch)
        self.assertEqual(seen, [SomeError])

    def test_send_exception(self):
        seen = []
        g1 = Fiber(target=fmain, args=(seen, ))
        g1.switch()
        self.assertRaises(KeyError, send_exception, g1, KeyError)
        self.assertEqual(seen, [KeyError])

#    def test_frame(self):
#        def f1():
#            f = sys._getframe(0)
#            self.assertEqual(f.f_back, None)
#            current().parent.switch(f)
#            return "meaning of life"
#        g = Fiber(f1)
#        frame = g.switch()
#        self.assertTrue(frame is g.t_frame)
#        self.assertTrue(g)
#        next = g.switch()
#        self.assertFalse(g.is_alive())
#        self.assertEqual(next, 'meaning of life')
#        self.assertEqual(g.t_frame, None)

    def test_threads(self):
        success = []

        def f():
            self.test_simple2()
            success.append(True)
        ths = [threading.Thread(target=f) for i in range(10)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        self.assertEqual(len(success), len(ths))

    def test_thread_bug(self):
        def runner(x):
            g = Fiber(lambda: time.sleep(x))
            g.switch()
        t1 = threading.Thread(target=runner, args=(0.2,))
        t2 = threading.Thread(target=runner, args=(0.3,))
        t1.start()
        t2.start()
        t1.join()
        t2.join()

    def test_switch_after_thread_of_prev_fiber_exited(self):
        def thread1():
            def fiber1():
                time.sleep(0.4)
            f11 = fibers.Fiber(fiber1)
            f11.switch()
        def thread2():
            time.sleep(0.2)
            def fiber1():
                time.sleep(0.4)
                f22.switch()
            def fiber2():
                pass
            f21 = fibers.Fiber(fiber1)
            f22 = fibers.Fiber(fiber2)
            f21.switch()
        t1 = threading.Thread(target=thread1)
        t2 = threading.Thread(target=thread2)
        t1.start()
        t2.start()
        t1.join()
        t2.join()

    def test_switch_to_another_thread(self):
        data = {}
        error = None
        created_event = threading.Event()
        done_event = threading.Event()

        def foo():
            data['g'] = Fiber(lambda: None)
            created_event.set()
            done_event.wait()
        thread = threading.Thread(target=foo)
        thread.start()
        created_event.wait()
        try:
            data['g'].switch()
        except fibers.error:
            error = sys.exc_info()[1]
        self.assertTrue(error != None, "fibers.error was not raised!")
        done_event.set()
        thread.join()

    def test_threaded_reparent(self):
        data = {}
        created_event = threading.Event()
        done_event = threading.Event()

        def foo():
            data['g'] = Fiber(lambda: None)
            created_event.set()
            done_event.wait()

        def blank():
            current().parent.switch()

        def setparent(g, value):
            g.parent = value

        thread = threading.Thread(target=foo)
        thread.start()
        created_event.wait()
        g = Fiber(blank)
        g.switch()
        self.assertRaises(ValueError, setparent, g, data['g'])
        done_event.set()
        thread.join()

    def test_throw_doesnt_crash(self):
        result = []
        def worker():
            current().parent.switch()
        def creator():
            g = Fiber(worker)
            g.switch()
            result.append(g)
        t = threading.Thread(target=creator)
        t.start()
        t.join()
        self.assertRaises(fibers.error, result[0].throw, SomeError())

    def test_threaded_updatecurrent(self):
        # FIXME (hangs?)
        return
        # released when main thread should execute
        lock1 = threading.Lock()
        lock1.acquire()
        # released when another thread should execute
        lock2 = threading.Lock()
        lock2.acquire()
        class finalized(object):
            def __del__(self):
                # happens while in green_updatecurrent() in main greenlet
                # should be very careful not to accidentally call it again
                # at the same time we must make sure another thread executes
                lock2.release()
                lock1.acquire()
                # now ts_current belongs to another thread
        def deallocator():
            current().parent.switch()
        def fthread():
            lock2.acquire()
            current()
            del g[0]
            lock1.release()
            lock2.acquire()
            current()
            lock1.release()
        main = current()
        g = [Fiber(deallocator)]
        g[0].bomb = finalized()
        g[0].switch()
        t = threading.Thread(target=fthread)
        t.start()
        # let another thread grab ts_current and deallocate g[0]
        lock2.release()
        lock1.acquire()
        # this is the corner stone
        # getcurrent() will notice that ts_current belongs to another thread
        # and start the update process, which would notice that g[0] should
        # be deallocated, and that will execute an object's finalizer. Now,
        # that object will let another thread run so it can grab ts_current
        # again, which would likely crash the interpreter if there's no
        # check for this case at the end of green_updatecurrent(). This test
        # passes if getcurrent() returns correct result, but it's likely
        # to randomly crash if it's not anyway.
        self.assertEqual(current(), main)
        # wait for another thread to complete, just in case
        t.join()

    def test_exc_state(self):
        def f():
            try:
                raise ValueError('fun')
            except:
                exc_info = sys.exc_info()
                t = Fiber(h)
                t.switch()
                self.assertEqual(exc_info, sys.exc_info())
                del t

        def h():
            self.assertEqual(sys.exc_info(), (None, None, None))

        g = Fiber(f)
        g.switch()

    def test_instance_dict(self):
        if is_pypy:
            return
        def f():
            current().test = 42
        def deldict(g):
            del g.__dict__
        def setdict(g, value):
            g.__dict__ = value
        g = Fiber(f)
        self.assertEqual(g.__dict__, {})
        g.switch()
        self.assertEqual(g.test, 42)
        self.assertEqual(g.__dict__, {'test': 42})
        g.__dict__ = g.__dict__
        self.assertEqual(g.__dict__, {'test': 42})
        self.assertRaises(AttributeError, deldict, g)
        self.assertRaises(TypeError, setdict, g, 42)

    def test_deepcopy(self):
        self.assertRaises(TypeError, copy.copy, Fiber())
        self.assertRaises(TypeError, copy.deepcopy, Fiber())

    def test_finished_parent(self):
        def f():
            return 42
        g = Fiber(f)
        g.switch()
        self.assertFalse(g.is_alive())
        self.assertRaises(ValueError, Fiber, parent=g)


if __name__ == '__main__':
    unittest.main(verbosity=2)

