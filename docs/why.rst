
.. _why-fibers:

Why fibers
==========

Fibers was created because I wanted to implement an API which looked pretty much
like a thread but with greenlet, but the API offered by greenlet got in the way,
so I decided to try and extract the minimum ammount of funcionality I needed and
make the API *I* wanted to use.

Note the use of the first person singular here, this was created for myself, I'm
sharing it because it may also help others. If this helped you, let me know, I'd
like to hear your use case!


Early binding
-------------

This was the first thing I wanted to change. In greenlet, the target function can
be specified in ``__init__`` but it can be changed later (before the greenlet is
switched to for the first time) and arguments (and keyword arguments) must be passed
to ``switch``. I wanted to 'bind' the callable and arguments as early as possible
(in ``__init__``, actually) pretty much like threads do.

With greenlet

::

    def run(*args, **kwargs):
        print args
        print kwargs

    g = greenlet(run)
    g.switch(1, 2, 3, foo='bar')

With fibers

::

    def run(*args, **kwargs):
        print args
        print kwargs

    f = Fiber(target=run, args=(1, 2, 3), kwargs={'foo':'bar'})
    f.switch()


This is a bigger change than it seems, because it also means that ``switch`` only
gets one argument, which is what the called will get. In greenlet the caller may
get a tuple, a dictionary or a tuple containing another tuple and a dictionary,
depending on the values passed to ``switch``.


Graceful failures
-----------------

Greenlet tends to be quite graceful with failures, which may lead to unexpected
behavior.

With greenlet

::

    def run():
        return 42

    g = greenlet(run)
    res = g.switch()
    # res is 42
    res = g.switch()
    # res is the empty tuple

With fibers

::

    def run():
        return 42

    f = Fiber(run)
    res = f.switch()
    # res is 42
    res = f.switch()
    # raises fiibers.error exception because f has ended


Garbage collection magic
------------------------

This is a tricky one. When greenlets are garbage collected, they are switched into
(if they where running) and ``GreenletExit`` exception is raised in them. This means
that if the code caught this exception, it could resurrect the object.

In fibers, on the contrary, this doesn't happen and if a fiber is gc'd while alive,
it's not switched into and no exception is raised in it.

This means that the programmer needs to take care and ``throw`` into the fibers which
he/she wishes to kill. It may look like a burden at a first sight, but I believe
it's for the best.


Stack slicing implementation
----------------------------

I'll be honest: I'm not smart enough to write the assembly code required to
perform the actual stack slicing for the different platforms. That's why I stand
on the shoulders of giants, in this case, I leveraged the tiny `stacklet` library
hidden inside PyPy, which is used to implement continuations, as well as
the greenlet module (in PyPy, that is).

`Stacklet` implements save, restore and stack switch operations in assembly, which
compilers won't touch, so there should be no issues regardless of the optimizations
used and the 'smartness' of the compiler. The greenlet implementation, on the other
hand, only implements the stack switch operation in assembly, and workarounds have
been needed every now and then, as compilers have become 'smarter'.

On a personal note, using a library such as `stacklet` makes the code look simpler,
since fibers is a wrapper over it with a cherry on top, and all the scary bits are
hidden inside the library itself.

