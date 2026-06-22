FROM ubuntu:24.04

# Avoid interactive prompts during apt installations
ENV DEBIAN_FRONTEND=noninteractive

# Install core build tools and multiarch support (needed for 32-bit TI tool components)
RUN apt-get update && apt-get install -y \
    build-essential \
    libc6-i386 \
    libxft2:i386 \
    libxss1:i386 \
    libncurses5 \
    libtinfo5 \
    libusb-0.1-4 \
    python3 \
    python3-pip \
    python3-serial \
    python3-tqdm \
    curl \
    wget \
    git \
    unzip \
    file \
    make \
    && rm -rf /var/lib/apt/lists/*

# Install python serial flashing libraries
RUN pip install xmodem --break-system-packages

# Create installation directory for TI tools
RUN mkdir -p /opt/ti

# Set up build context mount directory
WORKDIR /build_context

# Define environment variables for compilation tools
ENV MMWAVE_MCUPLUS_SDK_PATH=/opt/ti/mmwave_mcuplus_sdk_04_04_00_01
ENV MMWAVE_SDK_PATH=/opt/ti/mmwave_sdk_03_06_02_00
ENV CGT_TI_ARM_CLANG_PATH=/opt/ti/ti-cgt-armllvm_2.1.2.LTS
ENV CGT_TI_ARM_PATH=/opt/ti/ti-cgt-arm_16.9.6.LTS
ENV SYSCONFIG_PATH=/opt/ti/sysconfig_1.22.0
ENV PATH="/opt/ti/sysconfig_1.22.0:${PATH}"

# Copy installer executables from host downloads directory
# (Expected to be placed in the local downloads/ folder prior to building)
COPY downloads/ /tmp/downloads/

# Install TI tools in unattended (silent) mode
RUN chmod +x /tmp/downloads/*.bin /tmp/downloads/*.run 2>/dev/null || true \
    && echo "Installing SysConfig..." \
    && /tmp/downloads/sysconfig-1.22.0_3888-setup-hs.run --mode unattended --prefix /opt/ti/sysconfig_1.22.0 \
    && echo "Installing TI ARM LLVM Compiler (TI Clang)..." \
    && /tmp/downloads/ti_cgt_armllvm_2.1.2.LTS_linux-x64_installer.bin --mode unattended --prefix /opt/ti \
    && echo "Installing mmWave MCU+ SDK..." \
    && /tmp/downloads/mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin --mode unattended --prefix /opt/ti \
    && echo "Installing legacy TI ARM Compiler..." \
    && /tmp/downloads/ti_cgt_arm_16.9.6.LTS_linux_installer_x86.bin --mode unattended --prefix /opt/ti \
    && echo "Installing legacy mmWave SDK..." \
    && /tmp/downloads/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin --mode unattended --prefix /opt/ti \
    # Clean up temporary installer files to keep image size small
    && rm -rf /tmp/downloads

# Default command launches a bash session
CMD ["/bin/bash"]
