ifndef ICELIB_ANIMPROCESSING_ORBISANIM_INCLUDED
ICELIB_ANIMPROCESSING_ORBISANIM_INCLUDED := 1

###################################################################################################
# Dependent projects to compile
###################################################################################################


###################################################################################################
# Source files and settings
###################################################################################################
# (need to reset CURRENT_DIR because it gets altered in the sub makefiles)
CURRENT_DIR	:= $(SHARED_SRC)/ice/src/tools/icelib/animprocessing_orbisanim

ALLOW_WARNINGS := 1


C_SOURCES :=
SOURCES :=
SOURCES += $(wildcard $(CURRENT_DIR)/*.cpp)
C_SOURCES += $(wildcard $(CURRENT_DIR)/*.c)
EXCLUDE_SOURCES :=
SID_SOURCES	:=

EXTRA_HEADERS :=
EXTRA_HEADERS += $(CURRENT_DIR)
EXTRA_HEADERS += $(TOOLS_SRC)/src
EXTRA_HEADERS += $(SHARED_SRC)
EXTRA_HEADERS += $(SHARED_SRC)/ice/src/tools
EXTRA_HEADERS += $(SHARED_SRC)/ice/src/tools/icelib
EXTRA_HEADERS += $(SHARED_SRC)/sharedmath/pc/include

EXTRA_CCDEFS :=
EXTRA_CCDEFS += /DPNG_SETJMP_NOT_SUPPORTED

RELEASE_EXTRA_CCFLAGS :=
DEBUG_EXTRA_CCFLAGS :=


LIBNAME		:= liborbisanim_animprocessing

include $(MAKERULES)/make-library.mk
 

######################################################################
#                          liborbisanim_animprocessing
######################################################################
.PHONY: liborbisanim_animprocessing-debug liborbisanim_animprocessing-release liborbisanim_animprocessing-hybrid liborbisanim_animprocessing
liborbisanim_animprocessing: liborbisanim_animprocessing-release
-include x $(DEPS-debug)
liborbisanim_animprocessing-debug: $(DEBUG_MODULE_TARGETS)
-include x $(DEPS-release)
liborbisanim_animprocessing-release: $(RELEASE_MODULE_TARGETS)
-include x $(DEPS-hybrid)
liborbisanim_animprocessing-hybrid: $(HYBRID_MODULE_TARGETS)

 

endif # ICELIB_ANIMPROCESSING_ORBISANIM_INCLUDED