# VibeRetro68 task runner — short mnemonics for the scripts in scripts/.
# Actual building is delegated (CMake/Retro68 toolchain in build/,
# emulator launches in scripts/run-*.sh). Run `make help` for targets.

.PHONY: help setup fetch-deps build-retro68 doctor build basiliskii minivmac release clean

help:
	@echo "VibeRetro68 targets:"
	@echo "  make setup          One-shot env setup (brew + fetch-deps + build-retro68 + doctor)"
	@echo "  make fetch-deps     Download Retro68 source, emulators, ROMs into deps/"
	@echo "  make build-retro68  Build the Retro68 cross-compiler (~30-60 min, one-time)"
	@echo "  make doctor         Diagnose missing pieces of deps/"
	@echo "  make build          cmake --build build/  (compile the project)"
	@echo "  make basiliskii     Build and run in Basilisk II"
	@echo "  make minivmac       Build and (re)launch Mini vMac"
	@echo "  make release        Package build/ into release/MacDown-<date>.zip"
	@echo "  make clean          Remove build/"

setup:
	@./scripts/setup.sh

fetch-deps:
	@./scripts/fetch-deps.sh

build-retro68:
	@./scripts/build-retro68.sh

doctor:
	@./scripts/doctor.sh

build:
	@if [ ! -f build/CMakeCache.txt ]; then \
	    echo "==> Configuring build/ with the Retro68 toolchain"; \
	    mkdir -p build && \
	    (cd build && cmake .. -DCMAKE_TOOLCHAIN_FILE=../deps/retro68/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake); \
	fi
	@cmake --build build/

basiliskii:
	@./scripts/run-basiliskii.sh

minivmac:
	@./scripts/run-minivmac.sh

release:
	@./scripts/release.sh

clean:
	@rm -rf build/
	@echo "Removed build/"
