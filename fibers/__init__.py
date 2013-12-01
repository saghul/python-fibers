
__version__ = '0.3.0'

try:
    from fibers._cfibers import *
except ImportError:
    from fibers._pyfibers import *

