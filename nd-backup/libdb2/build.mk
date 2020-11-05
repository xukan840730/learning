ifndef LIBS_LIBDB2_INCLUDED
LIBS_LIBDB2_INCLUDED := 1

###################################################################################################
# Dependent projects to compile
###################################################################################################


###################################################################################################
# Source files and settings
###################################################################################################
# (need to reset CURRENT_DIR because it gets altered in the sub makefiles)
CURRENT_DIR	:= $(TOOLS_SRC)/src/tools/libs/libdb2

ALLOW_WARNINGS := 1


C_SOURCES :=
SOURCES :=
SOURCES += $(wildcard $(CURRENT_DIR)/*.cpp)
C_SOURCES += $(wildcard $(CURRENT_DIR)/*.c)
EXCLUDE_SOURCES :=
SID_SOURCES	:=

EXTRA_HEADERS :=
EXTRA_HEADERS += $(CURRENT_DIR)
EXTRA_HEADERS += $(SHARED_SRC)
EXTRA_HEADERS += $(SHARED_SRC)/sharedmath/pc/include
EXTRA_HEADERS += $(SHARED_SRC)/scea_shared
EXTRA_HEADERS += $(TOOLS_SRC)/src
EXTRA_HEADERS += $(TOP)
EXTRA_HEADERS += $(TOOLS_SRC)/src/tools/libs/libxml2/include
EXTRA_HEADERS += $(TOP)/common/imsg

EXTRA_CCDEFS :=

RELEASE_EXTRA_CCFLAGS :=
DEBUG_EXTRA_CCFLAGS :=
PCH_FILE := stdafx.h
PCH_DIR := $(CURRENT_DIR)/
FORCE_INCLUDE_FILE	:= ./pre-compiled-header.h


LIBNAME		:= libdb2

include $(MAKERULES)/make-library.mk
 

######################################################################
#                          libdb2
######################################################################
.PHONY: libdb2-debug libdb2-release libdb2-hybrid libdb2
libdb2: libdb2-release
-include x $(DEPS-debug)
libdb2-debug: $(DEBUG_MODULE_TARGETS)
-include x $(DEPS-release)
libdb2-release: $(RELEASE_MODULE_TARGETS)
-include x $(DEPS-hybrid)
libdb2-hybrid: $(HYBRID_MODULE_TARGETS)

 

endif # LIBS_LIBDB2_INCLUDED