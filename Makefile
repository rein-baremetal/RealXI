# RealXI offline: desktop tooling for the C++ engine. The Android app itself is built with Gradle / Android Studio.
CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra
SRC := $(wildcard engine/src/*.cpp)
INC := -Iengine/include -Iengine/src

.PHONY: engine-test sim check-layout formations all clean
all: engine-test check-layout

build/rx_tests: $(SRC) engine/tests/engine_tests.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

build/rx_sim: $(SRC) engine/tools/sim.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INC) $^ -o $@

engine-test: build/rx_tests
	./build/rx_tests

# Headless AI self-play, e.g. `make sim ARGS="16 3 0 120 7"` = 16 matches, home tier 3 vs away tier 0, 120 s halves, seed 7.
sim: build/rx_sim
	./build/rx_sim $(ARGS)

check-layout:
	python3 tools/check_layout.py

formations:
	python3 engine/tools/gen_formations.py

clean:
	rm -rf build
