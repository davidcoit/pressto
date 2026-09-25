# Linux build environment for Pressto (used by .devcontainer).
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Toolchain + the libraries JUCE needs for plugin/standalone builds on Linux.
# (No VST3 SDK needed: JUCE ships it. No webkit/curl: both are disabled.)
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        clang \
        cmake \
        ninja-build \
        git \
        pkg-config \
        ca-certificates \
        curl \
        unzip \
        gdb \
        xvfb \
        libasound2-dev \
        libjack-jackd2-dev \
        libfreetype-dev \
        libfontconfig1-dev \
        libx11-dev \
        libxcomposite-dev \
        libxcursor-dev \
        libxext-dev \
        libxinerama-dev \
        libxrandr-dev \
        libxrender-dev \
        libgl1-mesa-dev \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04 images already contain an 'ubuntu' user with UID 1000; reuse it
# under a friendlier name so bind-mounted files keep the host owner.
ARG USERNAME=dev
RUN usermod -l $USERNAME -d /home/$USERNAME -m ubuntu && groupmod -n $USERNAME ubuntu \
    && mkdir -p /workspaces && chown $USERNAME:$USERNAME /workspaces

USER $USERNAME
WORKDIR /workspaces

ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# Usage:
#   cmake --preset linux-release && cmake --build --preset linux-release
#   xvfb-run -a ctest --preset linux-release
