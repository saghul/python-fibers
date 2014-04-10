# coding=utf-8

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension

import glob
import re
import sys


if hasattr(sys, 'pypy_version_info'):
    ext_modules = []
else:
    if sys.platform == 'win32' and '32 bit' in sys.version:
        extra_objects = ['src/switch_x86_msvc.obj']
    elif sys.platform == 'win32' and '64 bit' in sys.version:
        extra_objects = ['src/switch_x64_msvc.obj']
    else:
        extra_objects = []
    ext_modules  = [Extension('fibers._cfibers',
                              sources = glob.glob('src/*.c'),
                              extra_objects=extra_objects,
                             )]


def get_version():
    return re.search(r"""__version__\s+=\s+(?P<quote>['"])(?P<version>.+?)(?P=quote)""", open('fibers/__init__.py').read()).group('version')


setup(name             = "fibers",
      version          = get_version(),
      author           = "Saúl Ibarra Corretgé",
      author_email     = "saghul@gmail.com",
      url              = "http://github.com/saghul/python-fibers",
      description      = "Lightweight cooperative microthreads for Pyhton",
      long_description = open("README.rst").read(),
      packages         = ['fibers'],
      platforms        = ["POSIX", "Microsoft Windows"],
      classifiers      = [
          "Development Status :: 4 - Beta",
          "Intended Audience :: Developers",
          "License :: OSI Approved :: MIT License",
          "Operating System :: POSIX",
          "Operating System :: Microsoft :: Windows",
          "Programming Language :: Python",
          "Programming Language :: Python :: 2",
          "Programming Language :: Python :: 2.6",
          "Programming Language :: Python :: 2.7",
          "Programming Language :: Python :: 3",
          "Programming Language :: Python :: 3.0",
          "Programming Language :: Python :: 3.1",
          "Programming Language :: Python :: 3.2",
          "Programming Language :: Python :: 3.3",
          "Programming Language :: Python :: 3.4",
          "Programming Language :: Python :: Implementation :: CPython",
          "Programming Language :: Python :: Implementation :: PyPy",
      ],
      ext_modules  = ext_modules
     )

