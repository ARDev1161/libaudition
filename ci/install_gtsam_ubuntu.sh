#!/usr/bin/env bash
set -euo pipefail

apt-get update
apt-get install -y software-properties-common ninja-build libtbb-dev qtbase5-dev
add-apt-repository -y ppa:borglab/gtsam-release-4.2

# apt-get update may return zero when the PPA index is unavailable (HTTP 503).
# Require both package metadata and a successful install, retrying both.
for attempt in 1 2 3 4 5; do
  if apt-get -o Acquire::Retries=3 -o Acquire::https::Timeout=45 update \
      && apt-cache show libgtsam-dev >/dev/null 2>&1 \
      && apt-get -o Acquire::Retries=5 -o Acquire::https::Timeout=45 install -y libgtsam-dev; then
    exit 0
  fi
  if (( attempt < 5 )); then
    sleep "$((attempt * 5))"
  fi
done

echo 'GTSAM package metadata or download unavailable after five attempts' >&2
exit 1
