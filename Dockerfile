### This is a template Dockerfile for the CI/CD pipeline

FROM ubuntu:22.04

ENV TZ=Asia/Ho_Chi_Minh
ENV DEBIAN_FRONTEND=noninteractive

ARG ARCH=x86_64
ENV ARCH=$ARCH

ENV ARCH_SONARQUBE=$ARCH_SONARQUBE
ENV ARCH_BUILD_WRAPPER=$ARCH_BUILD_WRAPPER


# Define the URLs for the tools
##### TODO #####
# Check and update version numbers if necessary

ARG SONAR_SCANNER_URL="https://binaries.sonarsource.com/Distribution/sonar-scanner-cli/sonar-scanner-cli-6.1.0.4477-linux-x64.zip"
ARG SONAR_BUILD_WRAPPER="https://sonarqube.silabs.net/static/cpp/build-wrapper-linux-x86.zip"
ARG SLC_CLI_URL="https://www.silabs.com/documents/login/software/slc_cli_linux.zip"
ARG COMMANDER_URL="https://www.silabs.com/documents/login/software/SimplicityCommander-Linux.zip"
ARG GCC_URL="https://developer.arm.com/-/media/Files/downloads/gnu/12.2.rel1/binrel/arm-gnu-toolchain-12.2.rel1-x86_64-arm-none-eabi.tar.xz"
ARG SIM_REPO="https://github.com/SiliconLabs/simplicity_sdk.git"
ARG SIM_SDK_VER="v2025.6.0"
ARG WISCONNECT_REPO="https://github.com/SiliconLabs/wiseconnect.git"
ARG WISCONNECT_VER="release/v3.5.0"

#add 3rd party repositories
RUN apt-get update  \
    && apt-get install --no-install-recommends -y \
    apt-utils \
    gpg \
    gpg-agent \
    ca-certificates \
    software-properties-common \
    && add-apt-repository ppa:openjdk-r/ppa

#Install necessary packages
RUN apt-get update && \
    apt-get -y install --no-install-recommends \
    sudo \
    git \
    python3.11 \
    python3-pip \
    ninja-build \
    make \
    unzip \
    bzip2 \
    xz-utils \
    openjdk-21-jdk && \
    rm -rf /var/lib/apt/lists/*
RUN pip install cmake --upgrade

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
ADD $SLC_CLI_URL /tmp/slc_cli_linux.zip
RUN unzip /tmp/slc_cli_linux.zip -d / && \
    rm /tmp/slc_cli_linux.zip

# Install Simplicity Commander
RUN wget $COMMANDER_URL && \
    unzip SimplicityCommander-Linux.zip && \
    mkdir /commander && \
    tar -xjf SimplicityCommander-Linux/Commander_linux_x86_64_*.tar.bz /commander && \
    rm -rf SimplicityCommander-Linux.zip && \
    rm -rf SimplicityCommander-Linux/

# Install GCC
ADD $GCC_URL /tmp/gcc-arm.tar.xz
RUN mkdir -p /opt/gcc-arm && \
    tar -xf /tmp/gcc-arm.tar.xz -C /opt/gcc-arm --strip-components=1 && \
    rm /tmp/gcc-arm.tar.xz

# Create Python symlink instead of alias
RUN ln -sf /usr/bin/python3.11 /usr/bin/python3

# Download and install SonarQube scanner
#REGEX: $(find /opt -maxdepth 1 -type d -name 'sonar-scanner-*' | head -n 1)
#This will find the first folder in /opt that starts with 'sonar-scanner-'
#This is necessary because the downloaded archive contains a folder with a version number in the name
#and we don't know what that version number is.

ADD "$SONAR_SCANNER_URL" /tmp/sonar-scanner-cli.zip

RUN unzip /tmp/sonar-scanner-cli.zip -d /opt \
    && SCANNER_FOLDER=$(find /opt -maxdepth 1 -type d -name 'sonar-scanner-*' | head -n 1) \
    && ln -s ${SCANNER_FOLDER}/bin/sonar-scanner /usr/local/bin/sonar-scanner \
    && rm /tmp/sonar-scanner-cli.zip

# Download and install build-wrapper
ADD "$SONAR_BUILD_WRAPPER" /tmp/build-wrapper-linux-x86.zip
RUN unzip /tmp/build-wrapper-linux-x86.zip -d /opt \
    && ln -s /opt/build-wrapper-linux-x86/build-wrapper-linux-x86 /usr/local/bin/build-wrapper \
    && rm /tmp/build-wrapper-linux-x86.zip

ENV SIMPLICITY_SDK_DIR=/simplicity_sdk/
ENV WISECONNECT_SDK_DIR=/wiseconnect
ENV ARM_GCC_DIR=/opt/gcc-arm
ENV SLC_CLI_DIR=/slc_cli/bin/slc-cli/slc_cli
ENV POST_BUILD_EXE=/commander/commander
ENV PATH="/commander/:${PATH}"
ENV PATH="/usr/bin/:${PATH}"
ENV PATH="${PATH}:/opt/build-wrapper-linux-x86/"
ENV PATH="${PATH}:/opt/gcc-arm/bin"

WORKDIR /home
