# Simple JUCE + VST3 build environment for compressor dev
FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

# ---------- Base tools ----------
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    pkg-config \
    clang \
    gdb \
    lldb \
    python3 \
    python3-pip \
    curl \
    wget \
    unzip \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# ---------- JUCE / audio / GUI dependencies ----------
RUN apt-get update && apt-get install -y \
    libasound2-dev \
    libjack-jackd2-dev \
    ladspa-sdk \
    libcurl4-openssl-dev \
    libfreetype6-dev \
    libx11-dev \
    libxcomposite-dev \
    libxcursor-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    libxrender-dev \
    libglib2.0-dev \
    libgtk-3-dev \
    libwebkit2gtk-4.0-dev \
    libglu1-mesa-dev \
    freeglut3-dev \
    mesa-common-dev \
    && rm -rf /var/lib/apt/lists/*

# ---------- (Optional but convenient) VST3 SDK ----------
# If you don't want the SDK auto-downloaded in the image, you can delete this block
RUN mkdir -p /opt && \
    cd /opt && \
    git clone --depth=1 https://github.com/steinbergmedia/vst3sdk.git && \
    ln -s /opt/vst3sdk /usr/local/vst3sdk

ENV VST3_SDK_DIR=/usr/local/vst3sdk

# ---------- Non-root user for development ----------
ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd --gid $USER_GID $USERNAME && \
    useradd -s /bin/bash --uid $USER_UID --gid $USER_GID -m $USERNAME && \
    mkdir -p /home/$USERNAME/.vscode-server /workspaces && \
    chown -R $USERNAME:$USERNAME /home/$USERNAME /workspaces

USER $USERNAME
WORKDIR /workspaces

# Useful default envs
ENV LANG=C.UTF-8
ENV LC_ALL=C.UTF-8

# ---------- Hint for you ----------
# Mount your project into /workspaces, e.g.:
#   docker run -it --rm -v /path/to/your/project:/workspaces simple-juce-vst3-dev:latest bash
#
# Then configure & build your JUCE plugin with CMake, e.g.:
#   cmake -B build -S . -G Ninja
#   cmake --build build --config Release
#
# The resulting VST3 will typically be in:
#   build/<YourPluginName_artefact>/VST3/
