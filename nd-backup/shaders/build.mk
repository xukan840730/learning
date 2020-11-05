# Kill the default rules!
.SUFFIXES:

ifeq ($(TARGET_PLATFORM),ps4)
  PLATFORM_DIR := ps4
  SHADER_TARGET := orbis
else ifeq ($(TARGET_PLATFORM),win)
  PLATFORM_DIR := pc
  SHADER_TARGET := dx11
else
  $(error TARGET_PLATFORM not specified or not one we can handle [ps4/win])
endif

#$(info [shared/src/ndlib/render/shaders/build.mk $(MAKECMDGOALS)])

# Set up the shader defines that will be set when building the ingame shaders
SHADER_DEFINES :=
SHADER_DEFINES += $(USER_SHADER_DEFINES)
ifdef FINAL_BUILD
  SHADER_DEFINES += FINAL_BUILD=1
endif

SHADER_BUILD_STYLE := build
SHADER_BUILD_STYLE := $(if $(filter shadersdebug,$(MAKECMDGOALS)),debug,$(SHADER_BUILD_STYLE))
SHADER_BUILD_STYLE := $(if $(filter shaderstrace,$(MAKECMDGOALS)),trace,$(SHADER_BUILD_STYLE))
SHADER_BUILD_STYLE := $(if $(filter shadersclean,$(MAKECMDGOALS)),clean,$(SHADER_BUILD_STYLE))

SHADER_ROOT_DIR := $(SHARED_SRC)/ndlib/render/shaders

SHADER_BUILD_SRC := $(BASETARGETDIR)
ifdef SHADERSCOPY_GAMENAME
	SHADER_BUILD_SRC := $(BASEBUILDDIR)/$(SHADERSCOPY_GAMENAME)/$(PLATFORM_DIR)/$(EXECUTABLE_CONFIG)
endif

SHADER_BUILD_ROOT_DIR := $(SHADER_BUILD_SRC)/shaders
SHADER_COPY_DIR := $(SHADER_BUILD_ROOT_DIR)/src
SHADER_DEST_DIR := $(SHADER_BUILD_ROOT_DIR)/bytecode
SHADER_CACHE_DIR := $(SHADER_BUILD_ROOT_DIR)/cache

SHADER_DEST_ROOT_DIR := $(GAMEDIR)/build/$(PLATFORM_DIR)/$(GAMEBRANCH)/bin/shaders

SHADER_PSARC_LOCAL := $(SHADER_BUILD_SRC)/psarc/shaders.psarc
SHADER_PSARC_NETWORK := $(GAMEDIR)/build/$(PLATFORM_DIR)/$(GAMEBRANCH)/bin/shaders.psarc

SHADER_BUILD_SCRIPT := $(SHARED_SRC)/ndlib/render/shaders/build.py

# Need to make sure these are set like this! The buildbot runs off a Python script,
# which means these environment vars are already set by whatever Python environment
# the buildbot is running! Bad news!
export PYTHONPATH = C:\ndibin\python-modules;
export PYTHONHOME = C:\ndibin\Autodesk\Maya2018-x64\Python
PYTHON := C:\ndibin\Autodesk\Maya2018-x64\bin\mayapy.exe

shadersclean:
	$(QUIET)$(PYTHON) $(SHADER_BUILD_SCRIPT) $(SHADER_BUILD_STYLE) $(SHADER_TARGET) $(SHADER_ROOT_DIR) $(SHADER_COPY_DIR) $(SHADER_DEST_DIR) $(SHADER_CACHE_DIR) "$(SHADER_DEFINES)"
	$(QUIET)$(call fs-rm, $(SHADER_PSARC_LOCAL))
	$(QUIET)$(call fs-rm, $(SHADER_PSARC_NETWORK))

shadersbuild:
	$(QUIET)$(call fs-mkdir, $(SHADER_DEST_ROOT_DIR))
	$(QUIET)$(PYTHON) $(SHADER_BUILD_SCRIPT) $(SHADER_BUILD_STYLE) $(SHADER_TARGET) $(SHADER_ROOT_DIR) $(SHADER_COPY_DIR) $(SHADER_DEST_DIR) $(SHADER_CACHE_DIR) "$(SHADER_DEFINES)"
ifeq ($(TARGET_PLATFORM),ps4)
	$(QUIET)echo Creating shaders.psarc archive: $(subst \,/,$(SHADER_PSARC_LOCAL))
	$(QUIET)$(PSARC) create --overwrite --no-compress --quiet --output=$(subst \,/,$(SHADER_PSARC_LOCAL)) $(subst \,/,$(SHADER_BUILD_ROOT_DIR)) --strip=$(subst \,/,$(BASETARGETDIR))
endif

shaderscopy:
	$(QUIET)echo Copying shaders to network...
	$(QUIET)$(call fs-mkdir, $(SHADER_DEST_ROOT_DIR))
	$(QUIET)$(call fs-mkdir, $(SHADER_DEST_ROOT_DIR)/src)
	$(QUIET)$(call fs-mkdir, $(SHADER_DEST_ROOT_DIR)/bytecode)
ifeq ($(TARGET_PLATFORM),ps4)
	$(QUIET)$(call fs-copy, $(SHADER_PSARC_LOCAL), $(SHADER_PSARC_NETWORK))
endif
	$(QUIET)$(call fs-copy, $(SHADER_COPY_DIR)/*.*, $(SHADER_DEST_ROOT_DIR)/src)
	$(QUIET)$(call fs-copy, $(SHADER_DEST_DIR)/*.?xo, $(SHADER_DEST_ROOT_DIR)/bytecode)
ifneq (,$(filter buildbotu4 discbot,$(USERNAME)))
	$(QUIET)$(call fs-mkdir, $(SHADER_DEST_ROOT_DIR)/cache)
	$(QUIET)-$(call fs-mirror, $(SHADER_CACHE_DIR), $(SHADER_DEST_ROOT_DIR)/cache)
endif

shaders shadersdebug shaderstrace: shadersbuild
	$(QUIET)$(MAKE) $(SILENCED_MAKE) $(FORCE_JOB_COUNT) -f $(SHARED_SRC)/Makefile shaderscopy

.PHONY: shaders shadersdebug shaderstrace shadersbuild shaderscopy shadersclean
