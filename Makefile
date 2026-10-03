CXX := g++

CXXFLAGS := \
	-std=c++17 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-O2 \
	-g

BUILD := build

COMMON := src/protocol.cpp

TARGETS := \
	$(BUILD)/serve \
	$(BUILD)/bincurl \
	$(BUILD)/hexdump

all: $(TARGETS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/serve: $(COMMON) src/server.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

$(BUILD)/bincurl: $(COMMON) src/client.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

$(BUILD)/hexdump: $(COMMON) src/hexdump.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc $^ -o $@

test: all
	./tests/run_tests.sh

clean:
	rm -rf $(BUILD)

.PHONY: all test clean