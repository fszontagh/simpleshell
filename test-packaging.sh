#!/usr/bin/env bash
set -euo pipefail

echo "Testing RPM packaging on various distributions..."
RPM_IMAGES=(
  "fedora:latest"
  "opensuse/leap:15.5"
  "centos:7"
)
for image in "${RPM_IMAGES[@]}"; do
  echo "Container: $image"
    docker run --rm -v "$(pwd)":/workspace -w /workspace "$image" bash -eux -c '
    if command -v dnf >/dev/null; then
      dnf -y install cmake make rpm-build gcc gcc-c++ git pkg-config readline-devel
    elif command -v zypper >/dev/null; then
      zypper -n install cmake make rpm-build gcc gcc-c++ git pkg-config readline-devel
    elif command -v yum >/dev/null; then
      yum -y install epel-release
      yum -y install cmake3 make rpm-build gcc gcc-c++ git pkgconfig readline-devel
      ln -sf "$(which cmake3)" /usr/bin/cmake
    else
      echo "No recognized package manager" >&2;
      exit 1;
    fi
    # ensure clean build directory
    rm -rf build && mkdir build && cd build
    # configure project
    cmake ..
    cpack -G RPM
    files=( *.rpm )
    if [ ${#files[@]} -eq 0 ]; then
      echo "FAILED to create RPM" >&2;
      exit 1;
    else
      echo "SUCCESS: created ${files[*]}"
    fi
  '
done

echo "Testing DEB packaging on Ubuntu..."
DEB_IMAGES=(
  "ubuntu:22.04"
  "ubuntu:24.04"
)
for image in "${DEB_IMAGES[@]}"; do
  echo "Container: $image"
  docker run --rm -v "$(pwd)":/workspace -w /workspace "$image" bash -eux -c '
    apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake make dpkg-dev fakeroot;
    # ensure clean build directory
    rm -rf build && mkdir build && cd build
    cmake ..
    cpack -G DEB
    files=( *.deb )
    if [ ${#files[@]} -eq 0 ]; then
      echo "FAILED to create DEB" >&2;
      exit 1;
    else
      echo "SUCCESS: created ${files[*]}"
    fi
  '
done

echo "All packaging tests completed successfully."