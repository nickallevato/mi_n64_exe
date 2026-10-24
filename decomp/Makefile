# Build the decompiled game code into build/decomp/decomp.elf (MIPS), which scripts/decomp.sh hands to N64Recomp.
# Flags follow BanjoRecomp's patches/Makefile.
# make's built-in defaults (cc, ld) would win over ?=
ifeq ($(origin CC),default)
CC := clang
endif
ifeq ($(origin LD),default)
LD := ld.lld
endif
OUT     := ../build/decomp
TARGET  := $(OUT)/decomp.elf

CFLAGS   := -target mips -mips2 -mabi=32 -O2 -G0 -mno-abicalls -mno-odd-spreg -mno-check-zero-division \
			-fomit-frame-pointer -ffast-math -fno-unsafe-math-optimizations -fno-builtin-memset -funsigned-char -fno-builtin-sinf -fno-builtin-cosf \
			-Wall -Wextra -Wno-incompatible-library-redeclaration -Wno-unused-parameter -Wno-unknown-pragmas -Wno-unused-variable -Wno-missing-braces -Wno-unsupported-floating-point-opt -Wno-error=incompatible-pointer-types -Wno-cast-function-type-mismatch -Werror=pointer-bool-conversion
CPPFLAGS := -nostdinc -D_LANGUAGE_C -DMIPS -I include
LDFLAGS  := -nostdlib -T decomp.ld -T imports.ld -Map $(OUT)/decomp.map --unresolved-symbols=ignore-all --emit-relocs

C_SRCS := $(shell find src -name '*.c' | sort)
C_OBJS := $(C_SRCS:%.c=$(OUT)/obj/%.o)
C_DEPS := $(C_OBJS:.o=.d)

$(TARGET): $(C_OBJS) decomp.ld imports.ld
	$(LD) $(C_OBJS) $(LDFLAGS) -o $@

$(OUT)/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) $< -MMD -MF $(@:.o=.d) -c -o $@

clean:
	rm -rf $(OUT)/obj $(TARGET)

-include $(C_DEPS)

.PHONY: clean
