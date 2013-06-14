# coding=utf8

try:
    from setuptools import setup, Extension
except ImportError:
    from distutils.core import setup, Extension


__version__ = "0.0.1"

setup(name             = "threadlet",
      version          = __version__,
      author           = "Saúl Ibarra Corretgé",
      author_email     = "saghul@gmail.com",
      url              = "http://github.com/saghul/python-threadlet",
      description      = "TODO",
      #long_description = open("README.rst").read(),
      platforms        = ["POSIX", "Microsoft Windows"],
      classifiers      = [
          "Development Status :: 2 - Experimental",
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
      ext_modules  = [Extension('threadlet',
                                sources = ['src/stacklet.c', 'src/threadlet.c'],
                                define_macros=[('MODULE_VERSION', __version__)],
                               )]
     )

