# Offline DSP tests (host compiler). Needs the upstream OB-Xf checkout in
# ../thirdparty-src/OB-Xf for the reference voice (see README).
CXX ?= g++
OBXF_SRC ?= ../thirdparty-src/OB-Xf/src
CXXFLAGS = -std=c++20 -O2 -Wall -Wno-unused-variable -Wno-unused-function

DSP_INC = -I../src -I../thirdparty/obxf/engine -I../thirdparty/obxf/shim
# Copied upstream files first, then the test stubs, then the vendored engine.
REF_INC = -Ibuild/ref -Istubs -I../thirdparty/obxf/engine -I../thirdparty/obxf/shim

REF_COPIES = build/ref/Voice.h build/ref/Lfo.h build/ref/Tuning.h build/ref/configuration.h

all: run

build/ref/%.h: $(OBXF_SRC)/engine/%.h
	@mkdir -p build/ref
	cp $< $@
build/ref/configuration.h: $(OBXF_SRC)/configuration.h
	@mkdir -p build/ref
	cp $< $@

build/reference_voice.o: reference_voice.cpp reference_voice.hpp $(REF_COPIES) $(wildcard stubs/*.h)
	$(CXX) $(CXXFLAGS) -DOBXF_VCV_REFERENCE_TEST $(REF_INC) -c $< -o $@

build/test_main.o: test_main.cpp reference_voice.hpp $(wildcard ../src/dsp/*.hpp)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(DSP_INC) -c $< -o $@

build/obxf_tests: build/test_main.o build/reference_voice.o
	$(CXX) $^ -o $@

run: build/obxf_tests
	@mkdir -p out
	./build/obxf_tests

clean:
	rm -rf build out

.PHONY: all run clean
