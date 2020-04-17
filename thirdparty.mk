# a minimal build of third party libraries for static linking

THIRD_PARTY_DIR = $(CURDIR)/thirdparty
SRC_DIR = $(THIRD_PARTY_DIR)/src
INCLUDE_DIR = $(THIRD_PARTY_DIR)/include
LIB_DIR = $(THIRD_PARTY_DIR)/lib
BIN_DIR = $(THIRD_PARTY_DIR)/bin
SHARE_DIR = $(THIRD_PARTY_DIR)/share

build = build
glog: build = cmake-build

THIRD_PARTY_LIBS = glog leveldb marisa opencc yaml-cpp gtest

.PHONY: all clean-src $(THIRD_PARTY_LIBS)

all: $(THIRD_PARTY_LIBS)

# note: this won't clean output files under include/, lib/ and bin/.
clean-src:
	rm -r $(SRC_DIR)/glog/cmake-build || true
	rm -r $(SRC_DIR)/googletest/build || true
	rm -r $(SRC_DIR)/leveldb/build || true
	rm -r $(SRC_DIR)/marisa-trie/build || true
	rm -r $(SRC_DIR)/opencc/build || true
	rm -r $(SRC_DIR)/yaml-cpp/build || true

glog:
	cd $(SRC_DIR)/glog; \
	cmake . -B$(build) \
	-DBUILD_TESTING:BOOL=OFF \
	-DWITH_GFLAGS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build) --target install

leveldb:
	cd $(SRC_DIR)/leveldb; \
	cmake . -Bbuild \
	-DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF \
	-DLEVELDB_BUILD_TESTS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build build --target install

marisa:
	cd $(SRC_DIR)/marisa-trie; \
	cmake $(SRC_DIR) -Bbuild \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build build --target install

opencc:
ifndef RIME_IOS_CROSS_COMPILING
	cd $(SRC_DIR)/opencc; \
	cmake . -Bbuild \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build build --target install
else
	# For iOS cross compilation, opencc is a special case.
	# opencc produces libopencc.a & executable opencc-dict, which's used by the build to generate dictionaries.
	# That means we have to compile opencc twice.
	# First targeting the host for host executable opencc-dict to generate the dictionaries.
	# Then cross compile to generate libopencc.a
	cd $(SRC_DIR)/opencc; \
	cmake . -Bbuild \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	&& cmake --build build --target install \
	&& rm $(THIRD_PARTY_DIR)/lib/libopencc.a \
	&& echo "Cross compiling..." \
	&& cmake . -Bbuild/ios \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build build/ios --target libopencc \
	&& cp build/ios/src/libopencc.a $(THIRD_PARTY_DIR)/lib
endif

yaml-cpp:
	cd $(SRC_DIR)/yaml-cpp; \
	cmake . -Bbuild \
	-DYAML_CPP_BUILD_CONTRIB:BOOL=OFF \
	-DYAML_CPP_BUILD_TESTS:BOOL=OFF \
	-DYAML_CPP_BUILD_TOOLS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build build --target install

gtest:
	cd $(SRC_DIR)/googletest; \
	cmake . -B$(build) \
	-DBUILD_GMOCK:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(THIRD_PARTY_DIR)" \
	&& cmake --build $(build) --target install
