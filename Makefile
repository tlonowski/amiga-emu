CXX := clang++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Werror -g -O2 -Icore/include -MMD -MP
SANITIZE := -fsanitize=address,undefined

CORE_SRCS := \
	core/src/cpu/m68k.cpp \
	core/src/chipset/cia.cpp \
	core/src/chipset/custom.cpp \
	core/src/chipset/floppy.cpp \
	core/src/chipset/hard_drive.cpp \
	core/src/memory/amiga_bus.cpp \
	core/src/memory/rom_loader.cpp \
	core/src/machine.cpp \
	core/src/amiga_c_api.cpp

CORE_OBJS := $(patsubst core/src/%.cpp,build/core/%.o,$(CORE_SRCS))

TEST_M68K := build/test_m68k
TEST_BOOT := build/test_amiga_boot
PROBE_BIN := build/kickstart_probe

.PHONY: all test clean probe app

app:
	./tools/build_app.sh

all: $(TEST_M68K) $(TEST_BOOT) $(PROBE_BIN)

build/core/%.o: core/src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SANITIZE) -c $< -o $@

$(TEST_M68K): tests/test_m68k.cpp $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SANITIZE) tests/test_m68k.cpp $(CORE_OBJS) -o $@

$(TEST_BOOT): tests/test_amiga_boot.cpp $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SANITIZE) tests/test_amiga_boot.cpp $(CORE_OBJS) -o $@

$(PROBE_BIN): tools/kickstart_probe.cpp $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(SANITIZE) tools/kickstart_probe.cpp $(CORE_OBJS) -o $@

probe: $(PROBE_BIN)
	./$(PROBE_BIN)

test: $(TEST_M68K) $(TEST_BOOT)
	./$(TEST_M68K)
	./$(TEST_BOOT)

clean:
	rm -rf build

-include $(CORE_OBJS:.o=.d) build/test_m68k.d build/test_amiga_boot.d build/kickstart_probe.d
