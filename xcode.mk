RIME_ROOT = $(CURDIR)

dist_dir = $(RIME_ROOT)/dist

ifdef BOOST_ROOT
CMAKE_BOOST_OPTIONS = -DBoost_NO_BOOST_CMAKE=TRUE \
	-DBOOST_ROOT="$(BOOST_ROOT)"
endif

XCODE_IOS_CROSS_COMPILE_CMAKE_FLAGS = -DCMAKE_TOOLCHAIN_FILE=$(CMAKE_IOS_TOOLCHAIN_ROOT)/ios.toolchain.cmake \
	-DPLATFORM=OS64COMBINED \
	-DENABLE_BITCODE=YES \
	-T buildsystem=1

IOS_CROSS_COMPILE_CMAKE_FLAGS = -DCMAKE_SYSTEM_NAME=iOS \
	-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
	-DCMAKE_IOS_INSTALL_COMBINED=YES \
	-DCMAKE_MACOSX_BUNDLE=NO

# 	-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO \

RIME_COMPILER_OPTIONS = CC=clang CXX=clang++ \
CXXFLAGS="-stdlib=libc++" LDFLAGS="-stdlib=libc++"

ifdef RIME_IOS_CROSS_COMPILING
	RIME_COMPILER_OPTIONS = CC=clang CXX=clang++ \
	CFLAGS="-fembed-bitcode" \
	CXXFLAGS="-stdlib=libc++ -fembed-bitcode" \
	LDFLAGS="-stdlib=libc++ -fembed-bitcode"

	RIME_CMAKE_XCODE_FLAGS=$(XCODE_IOS_CROSS_COMPILE_CMAKE_FLAGS)

	unexport CMAKE_OSX_ARCHITECTURES
	unexport MACOSX_DEPLOYMENT_TARGET
	unexport SDKROOT
else

# https://cmake.org/cmake/help/latest/envvar/MACOSX_DEPLOYMENT_TARGET.html
export MACOSX_DEPLOYMENT_TARGET ?= 10.9

ifdef BUILD_UNIVERSAL
# https://cmake.org/cmake/help/latest/envvar/CMAKE_OSX_ARCHITECTURES.html
export CMAKE_OSX_ARCHITECTURES = arm64;x86_64

# https://cmake.org/cmake/help/latest/variable/CMAKE_OSX_SYSROOT.html
export SDKROOT ?= $(shell xcrun --sdk macosx --show-sdk-path)
endif

endif

# boost::locale library from homebrew links to homebrewed icu4c libraries
icu_prefix = $(shell brew --prefix)/opt/icu4c

debug debug-with-icu test-debug debug-dist: build ?= debug
build ?= build

.PHONY: all release debug clean dist distclean test test-debug thirdparty \
release-with-icu debug-with-icu dist-with-icu

all: release

release:
	cmake . -B$(build) -GXcode \
	-DBUILD_STATIC=ON \
	-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
	-DCMAKE_INSTALL_PREFIX="$(dist_dir)" \
	$(CMAKE_BOOST_OPTIONS) \
	$(RIME_CMAKE_XCODE_FLAGS)
	cmake --build $(build) --config Release

release-with-icu:
	cmake . -B$(build) -GXcode \
	-DBUILD_STATIC=ON \
	-DBUILD_WITH_ICU=ON \
	-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
	-DCMAKE_INSTALL_PREFIX="$(dist_dir)" \
	-DCMAKE_PREFIX_PATH="$(icu_prefix)" \
	$(CMAKE_BOOST_OPTIONS) \
	$(RIME_CMAKE_XCODE_FLAGS)
	cmake --build $(build) --config Release

debug:
	cmake . -B$(build) -GXcode \
	-DBUILD_STATIC=ON \
	-DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
	$(CMAKE_BOOST_OPTIONS) \
	$(RIME_CMAKE_XCODE_FLAGS)
	cmake --build $(build) --config Debug

debug-with-icu:
	cmake . -B$(build) -GXcode \
	-DBUILD_STATIC=ON \
	-DBUILD_SEPARATE_LIBS=ON \
	-DBUILD_WITH_ICU=ON \
	-DCMAKE_PREFIX_PATH="$(icu_prefix)" \
	$(CMAKE_BOOST_OPTIONS) \
	$(RIME_CMAKE_XCODE_FLAGS)
	cmake --build $(build) --config Debug

clean:
	rm -rf build > /dev/null 2>&1 || true
	rm -rf debug > /dev/null 2>&1 || true
	rm build.log > /dev/null 2>&1 || true
	rm -f thirdparty/lib/* > /dev/null 2>&1 || true
	make -f thirdparty.mk clean-src

dist: release
	cmake --build $(build) --config Release --target install

debug-dist: debug
	cmake --build $(build) --config Release --target install

dist-with-icu: release-with-icu
	cmake --build $(build) --config Release --target install

distclean: clean
	rm -rf "$(dist_dir)" > /dev/null 2>&1 || true

test: release
	(cd $(build)/test; LD_LIBRARY_PATH=../lib/Release Release/rime_test)

test-debug: debug
	(cd $(build)/test; Debug/rime_test)

thirdparty:
	make -f thirdparty.mk

thirdparty/boost:
	./install-boost.sh

thirdparty/%:
	$(RIME_COMPILER_OPTIONS) make -f thirdparty.mk $(@:thirdparty/%=%)

ios:
	RIME_IOS_CROSS_COMPILING=true RIME_CMAKE_FLAGS='$(IOS_CROSS_COMPILE_CMAKE_FLAGS)' make -f xcode.mk

ios/%:
	RIME_IOS_CROSS_COMPILING=true RIME_CMAKE_FLAGS='$(IOS_CROSS_COMPILE_CMAKE_FLAGS)' make -f xcode.mk $(@:ios/%=%)
