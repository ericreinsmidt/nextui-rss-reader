# Top-level Makefile for NextFeed

.PHONY: help tg5040 tg5040-docker package clean

help:
	@echo "Available targets:"
	@echo "  make tg5040         - prepare tg5040 pak assets locally"
	@echo "  make tg5040-docker  - build tg5040 binary inside toolchain container"
	@echo "  make package        - create release zip"
	@echo "  make clean          - remove generated artifacts"

tg5040:
	$(MAKE) -C ports/tg5040 sync-assets

tg5040-docker:
	sh scripts/build_tg5040_docker.sh

package:
	sh scripts/package_pak.sh

clean:
	rm -rf dist release build out
	$(MAKE) -C ports/tg5040 clean
