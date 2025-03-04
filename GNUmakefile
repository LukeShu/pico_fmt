# Copyright (C) 2025  Luke T. Shumaker <lukeshu@lukeshu.com>
# SPDX-License-Identifier: BSD-3-Clause

all: build
.PHONY: all

.NOTINTERMEDIATE:
.DELETE_ON_ERROR:

################################################################################

generate/files = LICENSE.test.txt pico_fmt/test/catch.hpp
catch2_version = 2.13.5
pico_fmt/test/catch.hpp: $(MAKEFILE_LIST)
	wget --no-use-server-timestamps -O $@ https://github.com/catchorg/Catch2/releases/download/v$(catch2_version)/catch.hpp
	sed -i '1i// SPDX-License-Identifier: BSL-1.0' $@
LICENSE.test.txt:
	wget --no-use-server-timestamps -O $@ https://raw.githubusercontent.com/catchorg/Catch2/refs/tags/v$(catch2_version)/LICENSE.txt

generate: $(generate/files)
.PHONY: generate

generate-clean:
	rm -f -- $(generate/files)
.PHONY: generate-clean

build/Makefile:
	mkdir -p $(@D) && cd $(@D) && cmake -DCMAKE_BUILD_TYPE=Debug ..
build: build/Makefile generate
	$(MAKE) -C $(<D)
.PHONY: build

check: build
	$(MAKE) -C build test
.PHONY: check
