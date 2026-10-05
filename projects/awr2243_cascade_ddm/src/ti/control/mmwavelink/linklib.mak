###################################################################################
#  mmWave Link Makefile
###################################################################################
.PHONY: link linkClean

###################################################################################
# Setup the VPATH:
###################################################################################
vpath %.c src

###################################################################################
# Driver Source Files:
###################################################################################
MMWAVE_LINK_SOURCES = rl_controller.c 		\
					  rl_device.c			\
					  rl_driver.c 			\
					  rl_monitoring.c 		\
					  rl_sensor.c

###################################################################################
# Library Objects:
# AM273X:
#   mmWave Library is available for both the DSP and R5
###################################################################################
MMWAVE_R5F_LINK_LIB_OBJECTS  = $(addprefix $(PLATFORM_OBJDIR)/, $(MMWAVE_LINK_SOURCES:.c=.$(R5F_OBJ_EXT)))
MMWAVE_C66_LINK_LIB_OBJECTS = $(addprefix $(PLATFORM_OBJDIR)/, $(MMWAVE_LINK_SOURCES:.c=.$(C66_OBJ_EXT)))

###################################################################################
# Lib Dependency:
###################################################################################
MMWAVE_R5F_LINK_DEPENDS	 = $(addprefix $(PLATFORM_OBJDIR)/, $(MMWAVE_LINK_SOURCES:.c=.$(R5F_DEP_EXT)))
MMWAVE_C66_LINK_DEPENDS = $(addprefix $(PLATFORM_OBJDIR)/, $(MMWAVE_LINK_SOURCES:.c=.$(C66_DEP_EXT)))

###################################################################################
# Library Names:
###################################################################################
MMWAVE_R5F_LINK_DRV_LIB  = lib/libmmwavelink_$(MMWAVE_SDK_DEVICE_TYPE).$(R5F_LIB_EXT)
MMWAVE_C66_LINK_DRV_LIB = lib/libmmwavelink_$(MMWAVE_SDK_DEVICE_TYPE).$(C66_LIB_EXT)
MMWAVE_R5F_CASCADE_LINK_DRV_LIB  = lib/libmmwavelink_cascade_$(MMWAVE_SDK_DEVICE_TYPE).$(R5F_LIB_EXT)

###################################################################################
# mmWave Link Build:
###################################################################################
link: buildDirectories $(MMWAVE_R5F_LINK_LIB_OBJECTS) $(MMWAVE_C66_LINK_LIB_OBJECTS)
	if [ ! -d "lib" ]; then mkdir lib; fi
	echo "Archiving $@"
	$(R5F_AR) $(R5F_AR_OPTS) $(MMWAVE_R5F_LINK_DRV_LIB)  $(MMWAVE_R5F_LINK_LIB_OBJECTS)
	$(C66_AR) $(C66_AR_OPTS) $(MMWAVE_C66_LINK_DRV_LIB) $(MMWAVE_C66_LINK_LIB_OBJECTS)

###################################################################################
# mmWave cascade Link Build:
###################################################################################
linkCascade: R5F_CFLAGS  += -DCASCADE_EVM
linkCascade: buildDirectories $(MMWAVE_R5F_LINK_LIB_OBJECTS)
	if [ ! -d "lib" ]; then mkdir lib; fi
	echo "Archiving $@"
	$(R5F_AR) $(R5F_AR_OPTS) $(MMWAVE_R5F_CASCADE_LINK_DRV_LIB)  $(MMWAVE_R5F_LINK_LIB_OBJECTS)

###################################################################################
# Clean the mmWave Link Libraries
###################################################################################
linkClean:
	@echo 'Cleaning the mmWave Link Library Objects'
	@$(DEL) $(MMWAVE_R5F_LINK_LIB_OBJECTS) $(MMWAVE_R5F_LINK_DRV_LIB)
	@$(DEL) $(MMWAVE_R5F_LINK_DEPENDS)
	@$(DEL) $(MMWAVE_C66_LINK_LIB_OBJECTS) $(MMWAVE_C66_LINK_DRV_LIB)
	@$(DEL) $(MMWAVE_C66_LINK_DEPENDS)
	@$(DEL) $(MMWAVE_R5F_CASCADE_LINK_DRV_LIB)

###################################################################################
# Clean the mmWave Link Librarie Objects
###################################################################################
linkObjClean:
	@echo 'Cleaning the mmWave Link Library Objects'
	@$(DEL) $(MMWAVE_R5F_LINK_LIB_OBJECTS)
	@$(DEL) $(MMWAVE_R5F_LINK_DEPENDS)
	@$(DEL) $(MMWAVE_C66_LINK_LIB_OBJECTS)
	@$(DEL) $(MMWAVE_C66_LINK_DEPENDS)

###################################################################################
# Dependency handling
###################################################################################
-include $(MMWAVE_R5F_LINK_DEPENDS)
-include $(MMWAVE_C66_LINK_DEPENDS)

