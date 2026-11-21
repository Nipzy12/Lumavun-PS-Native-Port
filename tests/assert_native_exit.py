"""Verify a generated native PPC demo traps unsafe guest-memory access.

Avoid WILL_FAIL (which accepts *any* non-zero code): require exit status 12.
"""
import subprocess
import sys


def main():
    if len(sys.argv) != 3:
        raise SystemExit('Usage: assert_native_exit.py <expected-code> <native-exe>')
    expected = int(sys.argv[1])
    proc = subprocess.run([sys.argv[2]], capture_output=True, text=True, timeout=10)
    if proc.returncode != expected:
        raise SystemExit(
            f'Expected exit {expected}, got {proc.returncode}. '
            f'stdout={proc.stdout!r} stderr={proc.stderr!r}'
        )
    if 'PPU_HOMEBREW_RESULT=' in proc.stdout:
        raise SystemExit('Unsafe access unexpectedly reached game success output')
    print(f'Native memory guard verified: exit {expected}')


if __name__ == '__main__':
    main()
