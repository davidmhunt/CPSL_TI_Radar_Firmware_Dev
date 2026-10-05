###################################################################################
# mmWave Demo Level makefile
###################################################################################
ifeq ($(CCS_MAKEFILE_BASED_BUILD), 1)
ifeq ($(OS),Windows_NT)
include $(MMWAVE_SDK_INSTALL_PATH)/scripts/windows/setenv.mak
else
include $(MMWAVE_SDK_INSTALL_PATH)/scripts/unix/setenv.mak
endif
endif


include $(MMWAVE_SDK_INSTALL_PATH)/ti/common/mmwave_sdk.mak
include ./mss/mmw_mss.mak

MMW_DEMO_BIN         = $(MMWAVE_SDK_DEVICE_TYPE)_mmw_demo.bin

###################################################################################
# Standard Targets which need to be implemented by each mmWave SDK module. This
# plugs into the release scripts.
###################################################################################
.PHONY: all clean
.NOTPARALLEL:

# MSS-only metaimage: MSS + BSS (radarss) firmware, DSS = NULL (no DSP image).
# SHMEM_ALLOC comes from ti/common/mmwave_sdk_xwr18xx.mak (default 0x00000008).
# The secure (HS) image and the AOP variants of the TI demo are not built.
mmwDemo: mssDemo
	@$(GENERATE_METAIMAGE) $(MMW_DEMO_BIN) $(SHMEM_ALLOC) $(MSS_MMW_DEMO_OUT) $(XWR18XX_RADARSS_IMAGE_BIN) NULL

# Clean: This cleans all the objects
clean: mssDemoClean
	@$(DEL) $(MMW_DEMO_BIN)

# This builds all the supported images
all: mmwDemo

# Help: This displays the MAKEFILE Usage.
help:
	@echo '****************************************************************************************'
	@echo '* Makefile Targets for the MSS-only SAR/LVDS firmware '
	@echo 'all / mmwDemo   -> Builds the MSS image and the MSS+BSS metaimage (no DSS)'
	@echo 'clean           -> Cleans the MSS objects and the metaimage'
	@echo 'mssDemo         -> Builds the MSS binary only'
	@echo 'mssDemoClean    -> Cleans the MSS binary'
	@echo '****************************************************************************************'
