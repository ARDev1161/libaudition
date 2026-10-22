#!/usr/bin/env python3
"""Offline regressions for GTSAM PPA failure and pinned-source fallback."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[2] / 'ci' / 'install_gtsam_ubuntu.sh'
COMMIT = '4f66a491ffc83cf092d0d818b11dc35135521612'

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
  add-apt-repository)
    [ "$FAKE_PPA_AVAILABLE" = 1 ]
    exit $? ;;
  git)
    if [ "$1" = -C ]; then
      printf '%s\n' "$FAKE_GIT_SHA"
    else
      echo "clone" >> "$FAKE_SOURCE_EVENTS"
      for arg in "$@"; do last="$arg"; done
      printf '%s' "$last" > "$FAKE_CLONE_PATH"
    fi
    exit 0 ;;
  cmake)
    echo "cmake" >> "$FAKE_SOURCE_EVENTS"
    [ "$FAKE_CMAKE_SUCCESS" = 1 ]
    exit $? ;;
  ldconfig)
    exit 0 ;;
  sleep)
    exit 0 ;;
esac
exit 0
'''


class GtsamInstallRetryTest(unittest.TestCase):
    def run_case(self, ready_at, update_fails='', ppa_available=True,
                 source_sha=COMMIT, cmake_success=True):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            bin_dir = directory / 'bin'
            bin_dir.mkdir()
            for name in ('apt-get', 'apt-cache', 'add-apt-repository',
                         'sleep', 'git', 'cmake', 'ldconfig'):
                command = bin_dir / name
                command.write_text(FAKE_COMMAND)
                command.chmod(0o755)
            state = directory / 'update_count'
            state.write_text('0')
            events = directory / 'source_events'
            events.write_text('')
            clone_path = directory / 'clone_path'
            clone_path.write_text('')
            env = os.environ.copy()
            env.update(FAKE_APT_STATE=str(state), FAKE_READY_AT=str(ready_at),
                       FAKE_UPDATE_FAILS=update_fails,
                       FAKE_PPA_AVAILABLE='1' if ppa_available else '0',
                       FAKE_GIT_SHA=source_sha,
                       FAKE_CMAKE_SUCCESS='1' if cmake_success else '0',
                       FAKE_SOURCE_EVENTS=str(events),
                       FAKE_CLONE_PATH=str(clone_path),
                       PATH=str(bin_dir) + os.pathsep + env['PATH'])
            result = subprocess.run(['bash', str(SCRIPT)], env=env,
                                    capture_output=True, text=True, timeout=20)
            cloned = clone_path.read_text()
            cleaned = not cloned or not Path(cloned).parent.exists()
            return result, int(state.read_text()), events.read_text().splitlines(), cleaned

    def test_missing_index_then_recovery(self):
        result, updates, events, cleaned = self.run_case(ready_at=3)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(updates, 3)
        self.assertEqual(events, [])
        self.assertTrue(cleaned)

    def test_failed_update_then_recovery(self):
        result, updates, events, cleaned = self.run_case(ready_at=3, update_fails='2')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(updates, 3)
        self.assertEqual(events, [])
        self.assertTrue(cleaned)

    def test_persistent_unavailable_package_builds_pinned_source(self):
        result, updates, events, cleaned = self.run_case(ready_at=999)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(updates, 5)  # baseline, 3 PPA attempts, fallback
        self.assertEqual(events, ['clone', 'cmake', 'cmake'])
        self.assertTrue(cleaned)
        self.assertIn('Installed pinned source GTSAM', result.stderr)

    def test_unavailable_ppa_registration_builds_pinned_source(self):
        result, updates, events, cleaned = self.run_case(ready_at=999, ppa_available=False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(updates, 2)
        self.assertEqual(events, ['clone', 'cmake', 'cmake'])
        self.assertTrue(cleaned)

    def test_source_revision_mismatch_fails_closed(self):
        result, updates, events, cleaned = self.run_case(ready_at=999, source_sha='0' * 40)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(updates, 5)
        self.assertEqual(events, ['clone'])
        self.assertTrue(cleaned)
        self.assertIn('source revision mismatch', result.stderr)

    def test_source_build_failure_fails_closed(self):
        result, updates, events, cleaned = self.run_case(ready_at=999, cmake_success=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(updates, 5)
        self.assertEqual(events, ['clone', 'cmake'])
        self.assertTrue(cleaned)


if __name__ == '__main__':
    unittest.main()
