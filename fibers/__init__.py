
__version__ = '1.0.0'

try:
    from fibers._cfibers import *
except ImportError:
    from fibers._pyfibers import *

