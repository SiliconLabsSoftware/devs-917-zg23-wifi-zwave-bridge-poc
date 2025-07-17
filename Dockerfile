FROM ubuntu:22.04

ARG ARCH=x86_64
# Install packages

RUN apt-get update && \
    apt-get -y install --no-install-recommends \
    software-properties-common \
    build-essential \
    sudo \
    git \
    curl \
    python3.11 \
    python3-pip \
    ninja-build \
    make \
    unzip \
    wget \
    unzip \
    bzip2 \
    gpg-agent && \
    add-apt-repository ppa:openjdk-r/ppa && \
    apt-get install --no-install-recommends -y \
    openjdk-21-jdk && \
    rm -rf /var/lib/apt/lists/*
RUN pip install cmake --upgrade

ARG SLC_CLI_URL="https://www.silabs.com/documents/login/software/slc_cli_linux.zip"
ARG COMMANDER_URL="https://www.silabs.com/documents/login/software/SimplicityCommander-Linux.zip"
ARG GCC_URL="https://developer.arm.com/-/media/Files/downloads/gnu/12.2.rel1/binrel/arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi.tar.xz"
ARG SIM_REPO="https://github.com/SiliconLabs/simplicity_sdk.git"
ARG SIM_SDK_VER="v2025.6.0"
ARG WISCONNECT_REPO="https://github.com/SiliconLabs/wiseconnect.git"
ARG WISCONNECT_VER="release/v3.5.0"

# install Simplicity SDK
RUN git clone $SIM_REPO && \
    ls -la simplicity_sdk && \
    cd simplicity_sdk && \
    git checkout tags/$SIM_SDK_VER
# Install Wisconnect sdk
RUN git clone $WISCONNECT_REPO && \
    cd wiseconnect && \
    git checkout $WISCONNECT_VER

#Install SLC CLI
RUN wget $SLC_CLI_URL && \
    unzip slc_cli_linux.zip && \
    rm slc_cli_linux.zip

# Install Simplicity Commander
RUN wget $COMMANDER_URL && \
    unzip SimplicityCommander-Linux.zip && \
    mkdir commander && \
    tar -xf SimplicityCommander-Linux/Commander_linux_x86_64_*.tar.bz commander && \
    rm -rf SimplicityCommander-Linux.zip && \
    rm -rf SimplicityCommander-Linux/

# Install GCC
RUN wget $GCC_URL && \
    tar -vxf arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi.tar.xz && \
    rm -rf arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi.tar.xz

# alias python
# Create Python symlink instead of alias
RUN ln -sf /usr/bin/python3.11 /usr/bin/python3

ENV JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
ENV PATH="${JAVA_HOME}/bin:${PATH}"
ENV SIMPLICITY_SDK_DIR=/simplicity_sdk/
ENV WISECONNECT_SDK_DIR=/wiseconnect
ENV ARM_GCC_DIR=/arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi
ENV SLC_CLI_DIR=/slc_cli/bin/slc-cli/slc_cli
ENV POST_BUILD_EXE=/commander/commander
ENV PATH="/commander:${PATH}"
ENV PATH="/slc_cli:${PATH}"
ENV PATH="/usr/bin/:${PATH}"