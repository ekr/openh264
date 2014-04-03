include build/platform-arch.mk
SHAREDLIBSUFFIX = dylib
SHARED = -dynamiclib
CFLAGS += -Wall -fPIC -DMACOS -DMT_ENABLED -MMD -MP -arch x86_64 -std=c++11
LDFLAGS += -lpthread
ifeq ($(ASM_ARCH), x86)
ASMFLAGS += -DPREFIX
ifeq ($(ENABLE64BIT), Yes)
ASMFLAGS += -f macho64
else
ASMFLAGS += -f macho
LDFLAGS += -read_only_relocs suppress
endif
endif

