# Thin convenience wrapper. The build system is CMake (constitution §1) and
# every rule here forwards verbatim to the canonical commands in AGENTS.md.
# Generated trees stay inside build/ so constitution §14 holds.

BUILD ?= build

.PHONY: help configure build test asan leaks clean

help:
	@printf '%s\n' \
	  'Quantum Annealing Thesis - build helpers (constitution §1)' \
	  '  make configure  cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug' \
	  '  make build      cmake --build $(BUILD)' \
	  '  make test       ctest --test-dir $(BUILD) --output-on-failure' \
	  '  make asan       ASan+UBSan configure, build and test under $(BUILD)/san' \
	  '  make leaks      leaks --atExit on each CTest executable' \
	  '  make clean      remove generated build trees'

configure:
	cmake -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Debug

build: configure
	cmake --build $(BUILD)

test: build
	ctest --test-dir $(BUILD) --output-on-failure

# Constitution §9 gate. The sanitizer tree lives inside build/ because
# constitution §14 keeps the ignored-artifact list closed.
asan:
	cmake -S . -B $(BUILD)/san -DCMAKE_BUILD_TYPE=Debug \
	      -DQA_SANITIZE=address,undefined
	cmake --build $(BUILD)/san
	ctest --test-dir $(BUILD)/san --output-on-failure

leaks:
	leaks --atExit -- $(BUILD)/test-001-states
	leaks --atExit -- $(BUILD)/test-001-states-header

clean:
	rm -rf $(BUILD)
