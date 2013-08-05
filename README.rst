===========================================
fibers: lightweight concurrent multitasking
===========================================


Overview
========

Fibers are lightweight primitives for cooperative multitasking in Python. They
provide means for running pieces of code that can be paused and resumed. Unlike
threads, which are preemptively scheduled, fibers are scheduled cooperatively,
that is, only one fiber will be running at a given point in time, and no other
fiber will run until the user explicitly decides so.

When a fiber is created it will not run automatically. A fiber must be 'switched'
into for it to run. Fibers can switch control to other fibers by way of the `switch`
or `throw` functions, which switch control or raise and exception in the target
fiber respectively.

Example:

::

    import fibers

    def func1():
        print "1"
        f2.switch()
        print "3"
        f2.switch()

    def func2():
        print "2"
        f1.switch()
        print "4"

    f1 = fibers.Fiber(target=func1)
    f2 = fibers.Fiber(target=func2)
    f1.switch()


The avove example will print "1 2 3 4", but the result was obtained by the
cooperative work of 2 fibers yielding control to each other.


CI status
=========

.. image:: https://secure.travis-ci.org/saghul/python-fibers.png?branch=master
    :target: http://travis-ci.org/saghul/python-fibers


Documentation
=============

http://readthedocs.org/docs/python-fibers/


Installing
==========

python-fibers can be installed via pip as follows:

::

    pip install fibers


Building
========

Get the source:

::

    git clone https://github.com/saghul/python-fibers


Linux:

::

    ./build_inplace

Mac OSX:

::

    (XCode needs to be installed)
    export ARCHFLAGS="-arch x86_64"
    ./build_inplace

Microsoft Windows (with Visual Studio 2008):

::

    ./build_inplace
    (or, with cmd.exe)
    python setup.py build_ext --inplace


Running the test suite
======================

The test suite can be run using nose:

::

    nosetests -v -w tests/


Author
======

Saúl Ibarra Corretgé <saghul@gmail.com>

This project would not have been possible without the previous work done in
the `greenlet <http://greenlet.readthedocs.org>`_ and stacklet (part of
`PyPy <http://pypy.org>`_) projects.


License
=======

Unless stated otherwise on-file python-fibers uses the MIT license, check LICENSE file.


Supported Python versions
=========================

Python >= 2.6 is supported. Yes, that includes Python 3.


Supported architectures
=======================

x86, x86-64 and ARM are supported.


Contributing
============

If you'd like to contribute, fork the project, make a patch and send a pull
request. Have a look at the surrounding code and please, make yours look
alike.

