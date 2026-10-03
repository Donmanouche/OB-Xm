# OB-Xm — VCV Rack plugin build.
#   RACK_DIR=~/rack-sdk make            build
#   RACK_DIR=~/rack-sdk make dist       distributable .vcvplugin in dist/
#   RACK_DIR=~/rack-sdk make install    install into the local Rack user folder
RACK_DIR ?= ../..

# OB-Xf's DSP headers, vendored unmodified (see thirdparty/obxf/PROVENANCE.md),
# and the shim that replaces their JUCE dependencies.
FLAGS += -Ithirdparty/obxf/engine -Ithirdparty/obxf/shim
FLAGS += -fno-math-errno
CFLAGS +=
CXXFLAGS +=
LDFLAGS +=

SOURCES += $(wildcard src/*.cpp)

DISTRIBUTABLES += res
DISTRIBUTABLES += $(wildcard LICENSE*)
DISTRIBUTABLES += thirdparty/obxf/LICENSE thirdparty/obxf/PROVENANCE.md

include $(RACK_DIR)/plugin.mk

# OB-Xf's Noise.h uses std::countr_zero (C++20). Appended after plugin.mk so it
# overrides the SDK's -std=c++11.
CXXFLAGS += -std=c++20
# Rack 2 headers use [=] lambdas capturing this, deprecated (not removed) in C++20
CXXFLAGS += -Wno-deprecated

# Regenerate the panels, components and src/PanelLayout.hpp (needs Inkscape)
panels:
	python3 tools/gen_panels.py

# Offline DSP tests: `make -C tests` (does not need RACK_DIR)

.PHONY: panels
