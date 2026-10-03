# Builds obxf_render against a local VST3 SDK whose hosting libraries were built with:
#   cmake -S $(VST3_SDK) -B $(VST3_BUILD) -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ADD_VST3_PLUGINS_SAMPLES=OFF \
#     -DSMTG_ADD_VST3_HOSTING_SAMPLES=OFF -DSMTG_RUN_VST_VALIDATOR=OFF
#   ninja -C $(VST3_BUILD) sdk_hosting sdk_common base pluginterfaces
VST3_SDK ?= $(HOME)/vst3sdk
VST3_BUILD ?= build-vst3sdk
LIBS = $(VST3_BUILD)/lib/Release

# module_linux.cpp and plugprovider.cpp are not part of the sdk_hosting library
EXTRA = $(VST3_SDK)/public.sdk/source/vst/hosting/module_linux.cpp \
        $(VST3_SDK)/public.sdk/source/vst/hosting/plugprovider.cpp

obxf_render: obxf_render.cpp
	$(CXX) -std=c++17 -O2 -DRELEASE=1 -I$(VST3_SDK) $< $(EXTRA) -o $@ \
		$(LIBS)/libsdk_hosting.a $(LIBS)/libsdk_common.a $(LIBS)/libbase.a $(LIBS)/libpluginterfaces.a \
		-ldl -lpthread
