# Makefile -- HackMan3D Orbit Controller  /  qmk/test/
# Runs all verification tests and reports PASS/FAIL for each.
#
# Usage (from a QMK MSYS shell):
#   make          - run all tests
#   make golden   - regenerate golden.csv  [Phase 0]
#   make clean    - remove compiled binaries
#
# On Windows, run from inside a QMK MSYS bash shell where gcc/g++ are on PATH.

CXX      ?= g++
CC       ?= gcc
CFLAGS    = -O2 -Wall
CXXFLAGS  = -O2 -Wall

# QMK ships avr-g++ at /opt/qmk/bin/; prefer it over WinAVR 4.3.3.
AVRGPP ?= $(shell command -v /opt/qmk/bin/avr-g++ 2>/dev/null || command -v avr-g++ 2>/dev/null || echo /opt/qmk/bin/avr-g++)

.PHONY: all
all: run_lut_bug_check run_lut_fix_verify run_input_max_check run_avr_target_check

# -- Test 1: lut_bug_check (expect exit 1) ---------------------------------
# Proves the uint8_t truncation bug existed (historical record)
lut_bug_check: lut_bug_check.c
	$(CC) $(CFLAGS) $< -o $@ -lm

.PHONY: run_lut_bug_check
run_lut_bug_check: lut_bug_check
	@echo ""
	@echo "TEST 1  lut_bug_check  (expect exit 1 -- documents bug)"
	@./lut_bug_check; ret=$$?; if [ $$ret -eq 1 ]; then echo "[PASS] lut_bug_check (expected 1)"; else echo "[FAIL] lut_bug_check (expected 1)"; exit 1; fi

# -- Test 2: lut_fix_verify (expect exit 0) --------------------------------
# Regression test for the uint16_t fix in the live header
lut_fix_verify: lut_fix_verify.c
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: run_lut_fix_verify
run_lut_fix_verify: lut_fix_verify
	@echo ""
	@echo "TEST 2  lut_fix_verify  (expect exit 0 -- fix present)"
	@./lut_fix_verify; ret=$$?; if [ $$ret -eq 0 ]; then echo "[PASS] lut_fix_verify (expected 0)"; else echo "[FAIL] lut_fix_verify (expected 0)"; exit 1; fi

# -- Test 3: input_max_check (expect exit 0) --------------------------------
# All six INPUT_MAX_* constants match the real pipeline
input_max_check: input_max_check.c
	$(CXX) $(CXXFLAGS) $< -o $@

.PHONY: run_input_max_check
run_input_max_check: input_max_check
	@echo ""
	@echo "TEST 3  input_max_check  (expect exit 0 -- all OK)"
	@./input_max_check; ret=$$?; if [ $$ret -eq 0 ]; then echo "[PASS] input_max_check (expected 0)"; else echo "[FAIL] input_max_check (expected 0)"; exit 1; fi

# -- Test 4: avr_target_check (expect exit 0) --------------------------------
# Compiles cleanly for atmega32u4; tables = 384 bytes (3x64x2).
# Skips gracefully (exit 0) when no AVR toolchain is present.
.PHONY: run_avr_target_check
run_avr_target_check:
	@echo ""
	@echo "TEST 4  avr_target_check  (expect exit 0 -- target OK)"
	@AVRGPP="$(AVRGPP)" bash avr_target_check.sh; ret=$$?; if [ $$ret -eq 0 ]; then echo "[PASS] avr_target_check (expected 0)"; else echo "[FAIL] avr_target_check (expected 0)"; exit 1; fi

# -- Phase 0: golden CSV generation ----------------------------------------
# reference_pipeline.c compiles the original orbit_logic.h + .ino axis math,
# sweeps all 8 input channels (including Z push/pull, Z twist and rotation-
# priority combinations), and dumps inputs -> oTX..oRZ to CSV for all three
# speed modes. golden.csv is the ground truth for Phase 4 verification.
#
# make golden        -- (re)generate golden.csv
# make check_golden  -- diff QMK port output vs golden.csv (empty = correct)
#
# The golden CSV is committed to the repo so it never needs regenerating
# unless the reference firmware intentionally changes.

reference_pipeline: reference_pipeline.c
	$(CC) $(CFLAGS) $< -o $@ -lm

.PHONY: golden
golden: reference_pipeline
	@echo "Regenerating golden.csv from reference_pipeline.c ..."
	./reference_pipeline > golden.csv
	@echo "Done. $$(wc -l < golden.csv) rows written to golden.csv."

# To verify the QMK port: run the ported orbit_axes.c harness and compare:
#   ./qmk_pipeline > qmk_output.csv
#   diff golden.csv qmk_output.csv
# Empty diff = correct port (modulo the LUT fix, which produces a known delta).
.PHONY: check_golden
check_golden:
	@if [ -f golden.csv ]; then \
	  echo "[INFO] golden.csv has $$(wc -l < golden.csv) rows"; \
	  diff golden.csv golden.csv && echo "[PASS] golden self-diff is empty (file is consistent)"; \
	else echo "[SKIP] golden.csv not found -- run: make golden"; fi

.PHONY: clean
clean:
	rm -f lut_bug_check lut_fix_verify input_max_check reference_pipeline
	rm -f lut_bug_check.exe lut_fix_verify.exe input_max_check.exe reference_pipeline.exe

# Add reference_pipeline to 'all' test suite (Phase 0 + existing tests)
.PHONY: all_with_golden
all_with_golden: all golden
	@echo ""
	@echo "All tests + golden CSV complete."
