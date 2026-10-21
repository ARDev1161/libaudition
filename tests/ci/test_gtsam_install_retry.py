#!/usr/bin/env python3
"""Offline regression for Launchpad PPA 503 / apt-get update false-success."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'ci' / 'install_gtsam_ubuntu.sh'

FAKE_COMMAND = r'''#!/bin/sh
name=${0##*/}
case "$name" in
  apt-get)
    for arg in "$@"; do
      if [ "$arg" = update ]; then
        count=$(cat "$FAKE_APT_STATE")
        count=$((count + 1))
        printf '%s' "$count" > "$FAKE_APT_STATE"
        case ",$FAKE_UPDATE_FAILS," in
          *,$count,*) exit 100 ;;
        esac
        exit 0
      fi
      if [ "$arg" = libgtsam-dev ]; then
        [ "$(cat "$FAKE_APT_STATE")" -ge "$FAKE_READY_AT" ]
        exit $?
      fi
    done
    ;;
  apt-cache)
    [ "$(cat "$FAKE_APT_STATE")" -ge "$FAKE_READY_AT" ]
    exit $? ;;
esac
exit 0
'''


class GtsamInstallRetryTest(unittest.TestCase):
    def run_case(self, ready_at, update_fails=''):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            bin_dir = directory / 'bin'
            bin_dir.mkdir()
            for name in ('apt-get', 'apt-cache', 'add-apt-repository', 'sleep'):
                command = bin_dir / name
                command.write_text(FAKE_COMMAND)
                command.chmod(0o755)
            state = directory / 'update_count'
            state.write_text('0')
            env = os.environ.copy()
            env.update(FAKE_APT_STATE=str(state), FAKE_READY_AT=str(ready_at),
                       FAKE_UPDATE_FAILS=update_fails,
                       PATH=str(bin_dir) + os.pathsep + env['PATH'])
            result = subprocess.run(['bash', str(SCRIPT)], env=env,
                                    capture_output=True, text=True, timeout=20)
            return result, int(state.read_text())

    def test_missing_index_then_recovery(self):
        result, updates = self.run_case(ready_at=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(updates, 3)

    def test_failed_update_then_recovery(self):
        result, updates = self.run_case(ready_at=3, update_fails='2')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(updates, 3)

    def test_persistent_unavailable_package_fails_closed(self):
        result, updates = self.run_case(ready_at=999)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(updates, 6)
        self.assertIn('unavailable after five attempts', result.stderr)


if __name__ == '__main__':
    unittest.main()
