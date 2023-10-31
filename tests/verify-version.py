
import sys
import fibers

expected_version = sys.argv[1].strip()
assert(expected_version[0:7] == 'fibers-')
expected_version = expected_version[7:]

print("assert '%s' == '%s'" % (fibers.__version__, expected_version,))
assert fibers.__version__ == expected_version

