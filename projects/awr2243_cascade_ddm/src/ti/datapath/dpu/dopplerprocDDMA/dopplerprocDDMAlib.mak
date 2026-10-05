###################################################################################
# dopplerproc Library Makefile
###################################################################################
.PHONY: dopplerprocDDMALib dopplerprocDDMALibClean

###################################################################################
# Setup the VPATH:
###################################################################################
vpath %.c src

###################################################################################
# Library Source Files:
###################################################################################
# HWA applicable only to specific platforms
DOPPLERPROC_HWA_DDMA_LIB_SOURCES = dopplerprochwaDDMA.c
DOPPLERPROC_HWA_CAS_DDMA_LIB_SOURCES = dopplerprochwaDDMA.c

###################################################################################
# Library objects
#     Build for R5F and DSP
###################################################################################
DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB_OBJECTS  = $(addprefix $(PLATFORM_OBJDIR)/, $(DOPPLERPROC_HWA_DDMA_LIB_SOURCES:.c=.$(R5F_OBJ_EXT)))
DOPPLERPROC_HWA_DDMA_C66_DRV_LIB_OBJECTS = $(addprefix $(PLATFORM_OBJDIR)/, $(DOPPLERPROC_HWA_DDMA_LIB_SOURCES:.c=.$(C66_OBJ_EXT)))

DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_LIB_OBJECTS = $(addprefix $(PLATFORM_OBJDIR)/, $(DOPPLERPROC_HWA_CAS_DDMA_LIB_SOURCES:.c=.$(C66_OBJ_EXT)))

###################################################################################
# Library Dependency:
###################################################################################
DOPPLERPROC_HWA_DDMA_R5F_DRV_DEPENDS  = $(addprefix $(PLATFORM_OBJDIR)/, $(DOPPLERPROC_HWA_DDMA_LIB_SOURCES:.c=.$(R5F_DEP_EXT)))
DOPPLERPROC_HWA_DDMA_C66_DRV_DEPENDS = $(addprefix $(PLATFORM_OBJDIR)/, $(DOPPLERPROC_HWA_DDMA_LIB_SOURCES:.c=.$(C66_DEP_EXT)))

DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_DEPENDS = $(addprefix $(PLATFORM_OBJDIR)/, $(DOPPLERPROC_HWA_CAS_DDMA_LIB_SOURCES:.c=.$(C66_DEP_EXT)))

###################################################################################
# Library Names:
###################################################################################
# HWA applicable only to specific platforms
DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB  = lib/libdopplerproc_hwa_ddma_$(MMWAVE_SDK_DEVICE_TYPE).$(R5F_LIB_EXT)
DOPPLERPROC_HWA_DDMA_C66_DRV_LIB = lib/libdopplerproc_hwa_ddma_$(MMWAVE_SDK_DEVICE_TYPE).$(C66_LIB_EXT)

DOPPLERPROC_HWA_DDMA_C66_CASCADE_DRV_LIB = lib/libdopplerproc_hwa_ddma_cascade_$(MMWAVE_SDK_DEVICE_TYPE).$(C66_LIB_EXT)

###################################################################################
# Library Build:
#     - Build the R5 & DSP Library
###################################################################################
dopplerprocHWADDMALib: buildDirectories $(DOPPLERPROC_HWA_DDMA_C66_DRV_LIB_OBJECTS) #$(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB_OBJECTS)
	if [ ! -d "lib" ]; then mkdir lib; fi
	echo "Archiving $@"
	$(C66_AR) $(C66_AR_OPTS) $(DOPPLERPROC_HWA_DDMA_C66_DRV_LIB) $(DOPPLERPROC_HWA_DDMA_C66_DRV_LIB_OBJECTS)
#	$(R5F_AR) $(R5F_AR_OPTS) $(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB) $(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB_OBJECTS)

dopplerprocDDMALib: dopplerprocHWADDMALib

###################################################################################
# Library Build for Cascade EVM:
#     - Build the R5 & DSP Library
###################################################################################
dopplerprocHWADDMALibCascade: C66_CFLAGS += --define=CASCADE_EVM 
dopplerprocHWADDMALibCascade: buildDirectories $(DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_LIB_OBJECTS) #$(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB_OBJECTS)
	if [ ! -d "lib" ]; then mkdir lib; fi
	echo "Archiving $@"
	$(C66_AR) $(C66_AR_OPTS) $(DOPPLERPROC_HWA_DDMA_C66_CASCADE_DRV_LIB) $(DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_LIB_OBJECTS)
#	$(R5F_AR) $(R5F_AR_OPTS) $(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB) $(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB_OBJECTS)

dopplerprocDDMALibCascade: dopplerprocHWADDMALibCascade

###################################################################################
# Clean the Libraries
###################################################################################
dopplerprocHWADDMALibObjClean:
	@echo 'Cleaning the HWA dopplerproc Library Objects'
	@$(DEL) $(DOPPLERPROC_HWA_DDMA_R5F_DRV_LIB_OBJECTS)
	@$(DEL) $(DOPPLERPROC_HWA_DDMA_R5F_DRV_DEPENDS)
	@$(DEL) $(DOPPLERPROC_HWA_DDMA_C66_DRV_LIB_OBJECTS)
	@$(DEL) $(DOPPLERPROC_HWA_DDMA_C66_DRV_DEPENDS)
	@$(DEL) $(PLATFORM_OBJDIR)
	@$(DEL) $(DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_LIB_OBJECTS)
	@$(DEL) $(DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_DEPENDS)

dopplerprocDDMALibObjClean: dopplerprocHWADDMALibObjClean

###################################################################################
# Dependency handling
###################################################################################
-include $(DOPPLERPROC_HWA_DDMA_R5F_DRV_DEPENDS)
-include $(DOPPLERPROC_HWA_DDMA_C66_DRV_DEPENDS)
-include $(DOPPLERPROC_HWA_CAS_DDMA_C66_DRV_DEPENDS)

