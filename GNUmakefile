# Copyright (C) 2025  Luke T. Shumaker <lukeshu@lukeshu.com>
# SPDX-License-Identifier: BSD-3-Clause

all: build
.PHONY: all

.NOTINTERMEDIATE:
.DELETE_ON_ERROR:

################################################################################

build/Makefile:
	mkdir -p $(@D) && cd $(@D) && cmake -DCMAKE_BUILD_TYPE=Debug ..
build: build/Makefile
	$(MAKE) -C $(<D)
.PHONY: build

check: build
	$(MAKE) -C build test
.PHONY: check
