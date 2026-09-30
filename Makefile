CC ?= cc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -Wpedantic
FRAMA_C ?= frama-c
CLANG_FORMAT := $(shell command -v clang-format-20 2>/dev/null || \
	command -v clang-format 2>/dev/null)
C_SOURCES := $(wildcard *.c *.h)
SAMPLE_STATE := 21345671111111
SAMPLE_SOLUTION := B' R' D2 R' B R B' R D2 B R'
VECTORS := tests/solutions.txt
# One per rejection path: short, long, cubie digit low, cubie digit high,
# orientation digit low, orientation digit high, non-digit, duplicate, parity.
INVALID_STATES := 1234567111111 123456711111111 02345671111111 82345671111111 \
	12345671111110 12345671111114 1234567111111a 11345671111111 12345671111112

.PHONY: all check check-before check-after check-baseline \
        exhaustive exhaustive-before exhaustive-after \
        prove prove-baseline clean indent

all: solver solver_opt mini

solver: solver.c
	$(CC) $(CFLAGS) $< -o $@

solver_opt: solver_opt.c
	$(CC) $(CFLAGS) $< -o $@

mini: mini.c
	$(CC) $(CFLAGS) $< -o $@

# Build separate verifiers so the selected solver is explicit.
# The included solver source must be listed as a dependency.
verify_before: verify.c oracle.c solver.c solver_original.c
	$(CC) $(CFLAGS) -DVERIFY_OPTIMIZED=0 verify.c oracle.c -o $@

verify_after: verify.c oracle.c solver_opt.c solver_original.c
	$(CC) $(CFLAGS) -DVERIFY_OPTIMIZED=1 verify.c oracle.c -o $@

# IDA* tests accept different shortest move sequences.
check: check-before check-after

check-before: solver verify_before $(VECTORS) tests/check_cli.py
	./solver --self-test
	python3 tests/check_cli.py ./solver
	./verify_before 10000

check-after: solver_opt verify_after $(VECTORS) tests/check_cli.py
	./solver_opt --self-test
	python3 tests/check_cli.py ./solver_opt
	./verify_after 10000

# Full H1/H3 validation for both versions; this can take several minutes.
exhaustive: exhaustive-before exhaustive-after

exhaustive-before: verify_before
	./verify_before

exhaustive-after: verify_after
	./verify_after

solver_original: solver_original.c
	$(CC) $(CFLAGS) $< -o $@

# Original byte-exact regression tests apply only to the original solvers.
check-baseline: solver_original mini $(VECTORS)
	./solver_original --self-test
	@expected=$$(mktemp); actual=$$(mktemp); \
		trap 'rm -f "$$expected" "$$actual"' 0 1 2 15; \
		count=0; \
		while IFS='|' read -r state solution; do \
			case "$$state" in ""|\#*) continue ;; esac; \
			printf '%s\n' "$$solution" >"$$expected"; \
			for binary in ./solver_original ./mini; do \
				$$binary "$$state" >"$$actual"; \
				status=$$?; \
				test $$status -eq 0 || { \
					echo "$$binary $$state: exit status $$status"; exit 1; }; \
				cmp -s "$$actual" "$$expected" || { \
					echo "$$binary $$state: output mismatch"; \
					echo "  expected: $$solution"; \
					printf '  got:      '; cat "$$actual"; \
					echo "  ($$(wc -c <"$$expected") bytes expected, \
$$(wc -c <"$$actual") produced)"; exit 1; }; \
			done; \
			count=$$((count + 1)); \
		done <$(VECTORS); \
		echo "$$count solution vectors matched by solver and mini"
	@for binary in ./solver_original ./mini; do \
		for bad in $(INVALID_STATES); do \
			$$binary "$$bad" >/dev/null 2>&1; \
			status=$$?; \
			test $$status -eq 2 || { \
				echo "$$binary $$bad: expected status 2, got $$status"; exit 1; }; \
		done; \
		$$binary >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "$$binary with no argument: expected status 2, got $$status"; \
			exit 1; }; \
		$$binary $(SAMPLE_STATE) $(SAMPLE_STATE) >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "$$binary with two arguments: expected status 2, got $$status"; \
			exit 1; }; \
		$$binary $(SAMPLE_STATE) >&- 2>/dev/null; \
		status=$$?; \
		test $$status -eq 1 || { \
			echo "$$binary with stdout closed: expected status 1, got $$status"; \
			exit 1; }; \
	done
	@./solver_original --self-test >&- 2>/dev/null; \
		status=$$?; \
		test $$status -eq 1 || { \
			echo "solver --self-test with stdout closed: expected 1, got $$status"; \
			exit 1; }
	@echo "invalid input rejected with status 2, unwritable stdout with status 1"

# Compatibility alias: only the BASELINE contracts are checked.
# This does not prove solve_ida or the new table-building functions.
prove: prove-baseline

prove-baseline: solver_original.c
	@log=$$(mktemp); trap 'rm -f "$$log"' 0 1 2 15; \
		$(FRAMA_C) -wp -wp-fct quarter_turn,rank_state,valid,parse_state \
		-wp-rte -rte-verbose 0 -wp-prover alt-ergo -wp-timeout 20 \
		-wp-cache none solver_original.c >"$$log" 2>&1; rc=$$?; \
		grep -Fvx -e '[wp] Warning: Skipped RTE guards: unaligned pointers (\aligned not supported)' \
		-e '[wp] Warning: Skipped RTE guards: invalid function pointer calls (\valid_function not supported)' "$$log"; \
		test $$rc -eq 0 && awk '$$1 == "[wp]" && $$2 == "Proved" && $$3 == "goals:" && $$4 > 0 && $$4 == $$6 { ok = 1 } END { exit !ok }' "$$log" && \
		! grep -Eq '(^|[[:space:]])(Timeout|Unknown|Failed):' "$$log"

indent:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format 20 not found)
endif
	@$(CLANG_FORMAT) --version | grep -q 'version 20' || \
		{ echo "error: clang-format version 20 required"; exit 1; }
	$(CLANG_FORMAT) -i $(C_SOURCES)

clean:
	$(RM) solver solver_opt mini solver_original verify verify_before verify_after