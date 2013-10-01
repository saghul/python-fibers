# coding=utf8

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension
from glob import glob
import sys


__version__ = "0.1.0"


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
                              sources = glob('src/*.c'),
                              extra_objects=extra_objects,
                              define_macros=[('MODULE_VERSION', __version__)],
                             )]


setup(name             = "fibers",
      version          = __version__,
      author           = "Saúl Ibarra Corretgé",
      author_email     = "saghul@gmail.com",
      url              = "http://github.com/saghul/python-fibers",
      description      = "Lightweight cooperative microthreads for Pyhton",
      long_description = open("README.rst").read(),
      packages         = ['fibers'],
      platforms        = ["POSIX", "Microsoft Windows"],
      classifiers      = [
          "Development Status :: 3 - Alpha",
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
          "Programming Language :: Python :: 3.3"
      ],
      ext_modules  = ext_modules
     )

