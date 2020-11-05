ifndef COMMON_TREE_INCLUDED
COMMON_TREE_INCLUDED := 1

###################################################################################################
# Dependent projects to compile
###################################################################################################


###################################################################################################
# Source files and settings
###################################################################################################
# (need to reset CURRENT_DIR because it gets altered in the sub makefiles)
CURRENT_DIR	:= $(TOOLS_SRC)/src/common/tree

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
EXTRA_HEADERS += $(SHARED_SRC)/sharedmath/pc/include
EXTRA_HEADERS += $(TOP)/game
EXTRA_HEADERS += $(SHARED_SRC)/scea_shared

EXTRA_CCDEFS :=

RELEASE_EXTRA_CCFLAGS :=
DEBUG_EXTRA_CCFLAGS :=


LIBNAME		:= libtree

include $(MAKERULES)/make-library.mk
 

######################################################################
#                          libtree
######################################################################
.PHONY: libtree-debug libtree-release libtree-hybrid libtree
libtree: libtree-release
-include x $(DEPS-debug)
libtree-debug: $(DEBUG_MODULE_TARGETS)
-include x $(DEPS-release)
libtree-release: $(RELEASE_MODULE_TARGETS)
-include x $(DEPS-hybrid)
libtree-hybrid: $(HYBRID_MODULE_TARGETS)

 

endif # COMMON_TREE_INCLUDED