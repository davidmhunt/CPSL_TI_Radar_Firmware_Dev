FROM ubuntu:24.04

# Avoid interactive prompts during apt installations
ENV DEBIAN_FRONTEND=noninteractive

# Enable multiarch support (needed for 32-bit TI tool components) and install tools
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y \
    build-essential \
    libc6-i386 \
    libxft2:i386 \
    libxss1:i386 \
    libusb-0.1-4 \
    libusb-0.1-4:i386 \
    libncurses6 \
    libtinfo6 \
    lib32ncurses6 \
    lib32tinfo6 \
    mono-runtime \
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
    && ln -s /lib/x86_64-linux-gnu/libncurses.so.6 /lib/x86_64-linux-gnu/libncurses.so.5 2>/dev/null || true \
    && ln -s /lib/x86_64-linux-gnu/libtinfo.so.6 /lib/x86_64-linux-gnu/libtinfo.so.5 2>/dev/null || true \
    && ln -s /lib/i386-linux-gnu/libncurses.so.6 /lib/i386-linux-gnu/libncurses.so.5 2>/dev/null || true \
    && ln -s /lib/i386-linux-gnu/libtinfo.so.6 /lib/i386-linux-gnu/libtinfo.so.5 2>/dev/null || true \
    && ln -s /usr/lib32/libncurses.so.6 /usr/lib32/libncurses.so.5 2>/dev/null || true \
    && ln -s /usr/lib32/libtinfo.so.6 /usr/lib32/libtinfo.so.5 2>/dev/null || true \
    && rm -rf /var/lib/apt/lists/*

# Install python serial flashing libraries
RUN pip install xmodem --break-system-packages

# Create installation directory for TI tools
RUN mkdir -p /opt/ti

# Set up build context mount directory
WORKDIR /build_context

# Define environment variables for compilation tools (installed under /opt/ti).
# Cascade versions match firmware/cascade/src/demo/src/awr2243/*.projectspec.
ENV MMWAVE_MCUPLUS_SDK_PATH=/opt/ti/mmwave_mcuplus_sdk_04_04_00_01
ENV MMWAVE_SDK_PATH=/opt/ti/mmwave_sdk_03_06_02_00-LTS
ENV CGT_TI_ARM_CLANG_PATH=/opt/ti/ti-cgt-armllvm_2.1.1.LTS
ENV CGT_TI_C6000_PATH=/opt/ti/ti-cgt-c6000_8.3.12
ENV CGT_TI_ARM_PATH=/opt/ti/ti-cgt-arm_20.2.7.LTS
ENV SYSCONFIG_PATH=/opt/ti/sysconfig_1.22.0
ENV RADAR_TOOLBOX_INSTALL_PATH=/opt/ti/radar_toolbox_4_00_00_05
ENV PATH="/opt/ti/sysconfig_1.22.0:${PATH}"

# Copy installer executables from host downloads directory
# (Expected to be placed in the local downloads/ folder prior to building)
COPY downloads/ /tmp/downloads/

# Install TI tools in unattended (silent) mode to /opt/ti
RUN chmod +x /tmp/downloads/*.bin /tmp/downloads/*.run 2>/dev/null || true \
    && echo "Installing SysConfig..." \
    && /tmp/downloads/sysconfig-1.22.0_3893-setup.run --mode unattended --prefix /opt/ti/sysconfig_1.22.0 \
    && echo "Installing TI Clang Compiler..." \
    && /tmp/downloads/ti_cgt_armllvm_2.1.1.LTS_linux-x64_installer.bin --mode unattended --prefix /opt/ti \
    && echo "Installing TI C6000 Compiler..." \
    && /tmp/downloads/ti_cgt_c6000_8.3.12_linux-x64_installer.bin --mode unattended --prefix /opt/ti \
    && echo "Installing TI ARM Compiler (Legacy)..." \
    && /tmp/downloads/ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin --mode unattended --prefix /opt/ti \
    && echo "Installing mmWave MCU+ SDK..." \
    && /tmp/downloads/mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin --mode unattended --prefix /opt/ti \
    && echo "Installing legacy mmWave SDK..." \
    && /tmp/downloads/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin --mode unattended --prefix /opt/ti \
    && echo "Installing TI Radar Toolbox..." \
    && unzip -q /tmp/downloads/radar_toolbox_4_00_00_05.zip -d /opt/ti \
    # Clean up temporary installer files to keep image size small
    && rm -rf /tmp/downloads

# Default command launches a bash session
CMD ["/bin/bash"]
