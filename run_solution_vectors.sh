#!/usr/bin/env bash
# Run from the minirubik repository root. Optional positional source/fixture paths.
set -euo pipefail
SRC=${1:-stage4/full_v1_1_D.s}
VECTORS=${2:-tests/solutions.txt}
RV=${RV:-"$HOME/桌面/CA/HW1/riscv64-unknown-elf-gcc-8.3.0-2020.04.1-x86_64-linux-ubuntu14/bin"}
RIPES=${RIPES:-"$HOME/桌面/Ripes-v2.2.6-106-g5b8a616-linux-x86_64.AppImage"}
PROC=${PROC:-RV32_ISS}
[[ -f "$SRC" ]] || { echo "Missing source: $SRC" >&2; exit 1; }
[[ -f "$VECTORS" ]] || { echo "Missing vectors: $VECTORS" >&2; exit 1; }
[[ -x "$RIPES" ]] || { echo "Ripes missing or not executable: $RIPES" >&2; exit 1; }
for tool in gcc size; do
    [[ -x "$RV/riscv64-unknown-elf-$tool" ]] || { echo "Missing tool: $tool" >&2; exit 1; }
done
mkdir -p measurements/stage4/solution-vectors
OUT=$(mktemp -d "$PWD/measurements/stage4/solution-vectors/${PROC}.XXXXXX")
printf 'Processor: %s\nReports: %s\n' "$PROC" "$OUT"
cp -- "$VECTORS" "$OUT/solutions-used.txt"
cp -- "$SRC" "$OUT/source-used.s"
"$RV/riscv64-unknown-elf-gcc" --version > "$OUT/gcc-version.txt"
printf '%s\n' "$RIPES" > "$OUT/ripes-path.txt"

python3 - "$SRC" "$VECTORS" "$OUT" <<'PY'
import pathlib, re, sys
src, fixture, out = map(pathlib.Path, sys.argv[1:])
text = src.read_text(encoding='utf-8-sig')
if any('LED_MATRIX_' in s.split('#',1)[0] for s in text.splitlines()):
    raise SystemExit('Use the renderer-off full_v1_1_D.s, not the LED build.')
pattern = r'(?ms)^tests:[^\n]*\n.*?^tests_end:'
if len(list(re.finditer(pattern, text))) != 1:
    raise SystemExit('Expected exactly one tests: ... tests_end: block.')
if not re.search(r'(?m)^check_path:', text) or 'ALL PASSED' not in text:
    raise SystemExit('Use the full correctness harness, not a bench source.')
rows = []
valid_moves = {'R','R2',"R'",'B','B2',"B'",'D','D2',"D'"}
for line_no, raw in enumerate(fixture.read_text(encoding='utf-8-sig').splitlines(), 1):
    line = raw.split('#',1)[0].strip()
    if not line:
        continue
    state, sep, solution = line.partition('|')
    state = state.strip()
    moves = solution.split()
    if (not sep or len(state)!=14 or sorted(state[:7])!=list('1234567')
        or any(c not in '123' for c in state[7:])
        or sum(int(c)-1 for c in state[7:])%3
        or any(m not in valid_moves for m in moves) or len(moves)>11):
        raise SystemExit(f'Invalid fixture line {line_no}: {raw}')
    name = f'{len(rows)+1:02d}_{state}'
    block = f'tests:\n    .string "{state}"\n    .byte {len(moves)}\ntests_end:'
    generated = '.globl main\n' + re.sub(pattern, lambda _: block, text)
    (out/f'{name}.s').write_text(generated, encoding='utf-8')
    rows.append(f'{name}\t{state}\t{len(moves)}\n')
if not rows:
    raise SystemExit('No test cases found.')
(out/'cases.tsv').write_text(''.join(rows), encoding='utf-8')
print(f'Generated {len(rows)} independent test cases.')
PY

printf 'state\texpected_length\tresult\n' > "$OUT/summary.tsv"
while IFS=$'\t' read -r name state expected; do
    printf '\nTesting %s (expected %s moves)\n' "$state" "$expected"
    base="$OUT/$name"
    "$RV/riscv64-unknown-elf-gcc" \
        -march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -static -mno-relax \
        -Wl,--no-relax -Wl,-e,main -Wl,-Ttext=0x0 -Wl,-Tdata=0x10000000 \
        "$base.s" -o "$base.elf"
    "$RV/riscv64-unknown-elf-size" -A -d "$base.elf" > "$base-size.txt"
    if ! "$RIPES" --mode cli --proc "$PROC" --src "$base.elf" -t elf \
        --iret --output "$base-iret.txt" > "$base-console.txt" 2>&1; then
        cat "$base-console.txt"
        printf '%s\t%s\tRUN_ERROR\n' "$state" "$expected" >> "$OUT/summary.tsv"
        exit 1
    fi
    cat "$base-console.txt"
    if ! grep -q 'ALL PASSED' "$base-console.txt" || grep -q 'FAILED' "$base-console.txt"; then
        printf '%s\t%s\tFAIL\n' "$state" "$expected" >> "$OUT/summary.tsv"
        exit 1
    fi
    [[ -s "$base-iret.txt" ]] || { echo 'Missing retired-instruction report'; exit 1; }
    cat "$base-iret.txt"
    printf '%s\t%s\tPASS\n' "$state" "$expected" >> "$OUT/summary.tsv"
done < "$OUT/cases.tsv"
printf '\nCompleted: %s\n' "$OUT"
cat "$OUT/summary.tsv"
