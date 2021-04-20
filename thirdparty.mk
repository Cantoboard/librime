# a minimal build of third party libraries for static linking

third_party_dir = $(CURDIR)/thirdparty
src_dir = $(third_party_dir)/src

glog: build ?= cmake-build
build ?= build

third_party_libs = capnproto glog gtest leveldb marisa opencc yaml-cpp

.PHONY: all clean-src $(third_party_libs)

all: $(third_party_libs)

# note: this won't clean output files under include/, lib/ and bin/.
clean-src:
	rm -r $(src_dir)/capnproto/build || true
	rm -r $(src_dir)/glog/cmake-build || true
	rm -r $(src_dir)/googletest/build || true
	rm -r $(src_dir)/leveldb/build || true
	rm -r $(src_dir)/marisa-trie/build || true
	rm -r $(src_dir)/opencc/build || true
	rm -r $(src_dir)/yaml-cpp/build || true

capnproto:
ifndef RIME_IOS_CROSS_COMPILING
	cd $(src_dir)/capnproto; \
	cmake . -B$(build) \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DBUILD_TESTING:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	&& cmake --build $(build) --target install
else
	cd $(src_dir)/capnproto; \
	cmake . -B$(build) \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DBUILD_TESTING:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	&& cmake --build $(build) --target install \
	&& rm $(third_party_dir)/lib/libopencc.a || true \
	&& cmake . -B$(build)/ios \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DBUILD_TESTING:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)/ios" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build)/ios --target install \
	&& cp $(third_party_dir)/ios/lib/*.a $(third_party_dir)/lib
endif

glog:
	cd $(src_dir)/glog; \
	cmake . -B$(build) \
	-DBUILD_TESTING:BOOL=OFF \
	-DWITH_GFLAGS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build) --target install

gtest:
	cd $(src_dir)/googletest; \
	cmake . -B$(build) \
	-DBUILD_GMOCK:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build) --target install

leveldb:
	cd $(src_dir)/leveldb; \
	cmake . -B$(build) \
	-DLEVELDB_BUILD_BENCHMARKS:BOOL=OFF \
	-DLEVELDB_BUILD_TESTS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build) --target install
	&& echo "Cross compiling..." \

marisa:
	cd $(src_dir)/marisa-trie; \
	cmake $(src_dir) -B$(build) \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build) --target install

opencc:
ifndef RIME_IOS_CROSS_COMPILING
	cd $(src_dir)/opencc; \
	cmake . -B$(build) \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build) --target install
else
	# For iOS cross compilation, opencc is a special case.
	# opencc produces libopencc.a & executable opencc-dict, which's used by the build to generate dictionaries.
	# That means we have to compile opencc twice.
	# First targeting the host for host executable opencc-dict to generate the dictionaries.
	# Then cross compile to generate libopencc.a
	cd $(src_dir)/opencc; \
	cmake . -B$(build) \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	&& cmake --build build --target install \
	&& rm $(third_party_dir)/lib/libopencc.a || true \
	&& echo "Cross compiling..." \
	&& cmake . -B$(build)/ios \
	-DBUILD_SHARED_LIBS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)/ios" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build $(build)/ios --target install \
	&& cp $(third_party_dir)/ios/lib/libopencc.a $(third_party_dir)/lib
endif

yaml-cpp:
	cd $(src_dir)/yaml-cpp; \
	cmake . -B$(build) \
	-DYAML_CPP_BUILD_CONTRIB:BOOL=OFF \
	-DYAML_CPP_BUILD_TESTS:BOOL=OFF \
	-DYAML_CPP_BUILD_TOOLS:BOOL=OFF \
	-DCMAKE_BUILD_TYPE:STRING="Release" \
	-DCMAKE_INSTALL_PREFIX:PATH="$(third_party_dir)" \
	$(RIME_CMAKE_FLAGS) \
	&& cmake --build build --target install
