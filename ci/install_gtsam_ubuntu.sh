#!/usr/bin/env bash
set -euo pipefail

# Prefer the prebuilt GTSAM 4.2 package for the fast, usual path.
# The PPA sometimes responds with HTTP 503 for several minutes.
# Keep the 22.04 integration gate meaningful by falling back to building
# the exact upstream 4.2.0 commit rather than skipping this backend.
GTSAM_TAG=4.2.0
GTSAM_COMMIT=4f66a491ffc83cf092d0d818b11dc35135521612

install_source_fallback() {
  echo "GTSAM PPA unavailable; building pinned ${GTSAM_TAG} (${GTSAM_COMMIT}) from source" >&2
  apt-get -o Acquire::Retries=3 update
  apt-get -y install git cmake build-essential libeigen3-dev \
    libboost-serialization-dev libboost-system-dev libboost-filesystem-dev \
    libboost-thread-dev libboost-program-options-dev libboost-date-time-dev \
    libboost-timer-dev libboost-chrono-dev libboost-regex-dev

  local actual
  # Must outlive the function: EXIT traps execute after local variables expire.
  GTSAM_SOURCE_TMPDIR=$(mktemp -d "${TMPDIR:-/tmp}/libaudition-gtsam.XXXXXXXX")
  trap 'rm -rf "${GTSAM_SOURCE_TMPDIR}"' EXIT
  git clone --quiet --depth 1 --branch "${GTSAM_TAG}" \
    https://github.com/borglab/gtsam.git "${GTSAM_SOURCE_TMPDIR}/source"
  actual=$(git -C "${GTSAM_SOURCE_TMPDIR}/source" rev-parse HEAD)
  if [[ "${actual}" != "${GTSAM_COMMIT}" ]]; then
    echo "GTSAM source revision mismatch: expected ${GTSAM_COMMIT}, got ${actual}" >&2
    return 1
  fi
  cmake -S "${GTSAM_SOURCE_TMPDIR}/source" -B "${GTSAM_SOURCE_TMPDIR}/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DGTSAM_BUILD_TESTS=OFF -DGTSAM_BUILD_EXAMPLES_ALWAYS=OFF \
    -DGTSAM_BUILD_UNSTABLE=OFF -DGTSAM_BUILD_PYTHON=OFF \
    -DGTSAM_BUILD_WITH_MARCH_NATIVE=OFF -DGTSAM_USE_SYSTEM_EIGEN=ON
  cmake --build "${GTSAM_SOURCE_TMPDIR}/build" --parallel 2 --target install
  ldconfig
  echo "Installed pinned source GTSAM ${GTSAM_TAG}" >&2
}

apt-get update
apt-get install -y software-properties-common ninja-build libtbb-dev qtbase5-dev
if add-apt-repository -y ppa:borglab/gtsam-release-4.2; then
  # apt-get update may return zero when the PPA index is unavailable (503).
  # Require both package metadata and a successful installation.
  for attempt in 1 2 3; do
    if apt-get -o Acquire::Retries=2 -o Acquire::https::Timeout=25 update \
        && apt-cache show libgtsam-dev >/dev/null 2>&1 \
        && apt-get -o Acquire::Retries=3 -o Acquire::https::Timeout=25 install -y libgtsam-dev; then
      echo "Installed GTSAM from BorgLab PPA" >&2
      exit 0
    fi
    if (( attempt < 3 )); then
      sleep "$((attempt * 5))"
    fi
  done
fi

# A source build still exercises all GTSAM CTest and installed-package
# consumer gates; if its download, revision, build, or install fails, fail CI.
install_source_fallback
