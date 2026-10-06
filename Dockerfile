# syntax=docker/dockerfile:1
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
# Cascade versions match projects/awr2243_cascade_ddm/src/*.projectspec.
ENV MMWAVE_MCUPLUS_SDK_PATH=/opt/ti/mmwave_mcuplus_sdk_04_04_00_01
ENV MMWAVE_SDK_PATH=/opt/ti/mmwave_sdk_03_06_02_00-LTS
ENV CGT_TI_ARM_CLANG_PATH=/opt/ti/ti-cgt-armllvm_2.1.1.LTS
ENV CGT_TI_C6000_PATH=/opt/ti/ti-cgt-c6000_8.3.12
ENV CGT_TI_ARM_PATH=/opt/ti/ti-cgt-arm_20.2.7.LTS
ENV SYSCONFIG_PATH=/opt/ti/sysconfig_1.22.0
ENV RADAR_TOOLBOX_INSTALL_PATH=/opt/ti/radar_toolbox_4_00_00_05
ENV PATH="/opt/ti/sysconfig_1.22.0:${PATH}"

# TI installers are bind-mounted from downloads/ (populated by downloads/download.sh) instead of
# COPY'd, so the ~4 GB of installers never become an image layer. Each tool gets its own RUN
# so a failure/upgrade only re-runs that step. install_ti copies the installer out of the
# read-only mount, makes it executable, runs it unattended, and deletes it in the same layer.
RUN printf '#!/bin/sh\nset -e\nsrc="$1"; shift\ncp "$src" /tmp/installer && chmod +x /tmp/installer\n/tmp/installer --mode unattended "$@"\nrm -f /tmp/installer\n' > /usr/local/bin/install_ti \
    && chmod +x /usr/local/bin/install_ti

RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing SysConfig..." \
    && install_ti /downloads/sysconfig-1.22.0_3893-setup.run --prefix /opt/ti/sysconfig_1.22.0

RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing TI Clang + C6000 + legacy ARM compilers..." \
    && install_ti /downloads/ti_cgt_armllvm_2.1.1.LTS_linux-x64_installer.bin --prefix /opt/ti \
    && install_ti /downloads/ti_cgt_c6000_8.3.12_linux-x64_installer.bin --prefix /opt/ti \
    && install_ti /downloads/ti_cgt_tms470_20.2.7.LTS_linux-x64_installer.bin --prefix /opt/ti

RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing mmWave MCU+ SDK..." \
    && install_ti /downloads/mmwave_mcuplus_sdk_04_04_00_01-Linux-x86-Install.bin --prefix /opt/ti

RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing legacy mmWave SDK..." \
    && install_ti /downloads/mmwave_sdk_03_06_02_00-LTS-Linux-x86-Install.bin --prefix /opt/ti

RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing TI Radar Toolbox..." \
    && unzip -q /downloads/radar_toolbox_4_00_00_05.zip -d /opt/ti

# ---------------------------------------------------------------------------------------------
# Code Composer Studio 12.8.1 (headless projectspec builds for the cascade demo)
# CCS 12 isn't officially supported on Ubuntu 24.04; it needs a few libraries dropped from 24.04
# (libtinfo5, libgconf-2-4, libpython2.7), pulled from older Ubuntu pools.
# CCS_COMPONENTS: AM273x device support lives in the mmWave + Sitara MCU (AM2x) families;
# set to PF_ALL if CCS reports an unknown device/product.
# ---------------------------------------------------------------------------------------------
ARG CCS_VERSION=12.8.1.00005
ARG CCS_COMPONENTS=PF_MMWAVE,PF_SITARA_MCU
RUN UBU=http://mirrors.edge.kernel.org/ubuntu/pool/universe \
    && apt-get update && apt-get install -y --no-install-recommends libnsl2 \
    && cd /tmp \
    && wget -q $UBU/n/ncurses/libtinfo5_6.3-2ubuntu0.3_amd64.deb \
               $UBU/g/gconf/gconf2-common_3.2.6-7ubuntu2_all.deb \
               $UBU/g/gconf/libgconf-2-4_3.2.6-7ubuntu2_amd64.deb \
               $UBU/p/python2.7/libpython2.7-minimal_2.7.18-13ubuntu1.5_amd64.deb \
               $UBU/p/python2.7/libpython2.7-stdlib_2.7.18-13ubuntu1.5_amd64.deb \
               $UBU/p/python2.7/libpython2.7_2.7.18-13ubuntu1.5_amd64.deb \
    && (apt-get install -y --no-install-recommends /tmp/*.deb || dpkg -i --force-depends /tmp/*.deb) \
    && rm -f /tmp/*.deb && rm -rf /var/lib/apt/lists/* \
    # CCS's installer calls udev tools that don't exist in a container
    && ln -sf /bin/true /usr/local/bin/udevadm && ln -sf /bin/true /sbin/start_udev && mkdir -p /etc/udev/rules.d

RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing Code Composer Studio ${CCS_VERSION}..." \
    && mkdir -p /tmp/ccs && tar -xzf /downloads/CCS${CCS_VERSION}_linux-x64.tar.gz -C /tmp/ccs \
    && /tmp/ccs/CCS${CCS_VERSION}_linux-x64/ccs_setup_${CCS_VERSION}.run --mode unattended \
         --enable-components ${CCS_COMPONENTS} --prefix /opt/ti \
         --install-BlackHawk false --install-Segger false \
       || (cat /opt/ti/ccs/install_logs/*/*.log; exit 1) \
    && rm -rf /tmp/ccs

ENV CCS_INSTALL_PATH=/opt/ti/ccs
ENV PATH="/opt/ti/ccs/eclipse:${PATH}"

# ---------------------------------------------------------------------------------------------
# UniFlash 9.6.0 (headless serial flashing of xWR18xx via dslite; firmware-15). Kept LAST so the
# ~20 min CCS layer above stays cached. Step 1 probe: the unattended installer needs no display
# (no xvfb) and no extra apt libs for the CLI (DSLite has no missing libs; the "Failed to locate
# system libraries" notice only concerns the GUI). Installs as root; chmod so any host UID can run it.
# Using it implies accepting TI's UniFlash license terms (see downloads/download.sh).
# ---------------------------------------------------------------------------------------------
RUN --mount=type=bind,source=downloads,target=/downloads \
    echo "Installing UniFlash 9.6.0..." \
    && install_ti /downloads/uniflash_sl.9.6.0.5764.run --prefix /opt/ti/uniflash_9.6.0 \
    && chmod -R a+rX /opt/ti/uniflash_9.6.0

ENV UNIFLASH_PATH=/opt/ti/uniflash_9.6.0

# Default command launches a bash session
CMD ["/bin/bash"]
