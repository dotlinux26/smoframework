BUILD_DIR  ?= build
CONFIG     ?= Release
JOBS       ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
CMAKE      ?= cmake
PRESET     ?=

.PHONY: all configure build clean test tidy

all: configure build

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) \
		-DCMAKE_BUILD_TYPE=$(CONFIG) \
		-DCMAKE_EXPORT_COMPILE_COMMANDS=ON

build:
	$(CMAKE) --build $(BUILD_DIR) --parallel $(JOBS)

clean:
	rm -rf $(BUILD_DIR)

test:
	$(CMAKE) --build $(BUILD_DIR) --target smo_all_tests --parallel $(JOBS)
	ctest --test-dir $(BUILD_DIR) --output-on-failure

tidy:
	clang-tidy src/*/**.cpp -- --std=c++20

# ── Demo (Docker 3-node mesh) ──────────────────────────────────
DEMO_SCRIPT = deployments/demo/scripts/demo.sh

demo-build:
	@$(DEMO_SCRIPT) build

demo-up:
	@$(DEMO_SCRIPT) up

demo-down:
	@$(DEMO_SCRIPT) down

demo-status:
	@$(DEMO_SCRIPT) status

demo-logs:
	@$(DEMO_SCRIPT) logs

demo-test:
	@$(DEMO_SCRIPT) test

demo-shell:
	@$(DEMO_SCRIPT) shell

demo-discover:
	@$(DEMO_SCRIPT) discover

demo-clean:
	@$(DEMO_SCRIPT) clean

demo: demo-build demo-up demo-test
	@echo "Demo complete."

.PHONY: demo-build demo-up demo-down demo-status demo-logs demo-test demo-shell demo-discover demo-clean demo
