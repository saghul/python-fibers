
__version__ = '0.2.0'

try:
    from fibers._cfibers import *
except ImportError:
    from fibers._pyfibers import *

