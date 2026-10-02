#!/usr/bin/env bash
# GNU toolchain -> ELF section checks -> Ripes. Renderer absent.
set -u
cd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1
RV=${RV:-"$HOME/桌面/CA/HW1/riscv64-unknown-elf-gcc-8.3.0-2020.04.1-x86_64-linux-ubuntu14/bin"}
RIPES=${RIPES:-"$HOME/桌面/Ripes-v2.2.6-106-g5b8a616-linux-x86_64.AppImage"}
for tool in gcc size readelf objdump; do
    [[ -x "$RV/riscv64-unknown-elf-$tool" ]] || { echo "Missing tool: $RV/riscv64-unknown-elf-$tool"; exit 1; }
done
[[ -x "$RIPES" ]] || { echo "Ripes missing or not executable: $RIPES"; exit 1; }
OUT=$(mktemp -d "$PWD/rodata-report.XXXXXX") || exit 1
printf 'Output directory: %s\nRipes: %s\n' "$OUT" "$RIPES"
"$RV/riscv64-unknown-elf-gcc" --version > "$OUT/gcc-version.txt"
flags=(-march=rv32i -mabi=ilp32 -nostdlib -nostartfiles -static -mno-relax
       -Wl,--no-relax -Wl,-e,main -Wl,-Ttext=0x0 -Wl,-Tdata=0x10000000)

inspect() {
    local name=$1
    "$RV/riscv64-unknown-elf-size" -A -d "$OUT/$name.elf" > "$OUT/$name-size.txt" || return 1
    "$RV/riscv64-unknown-elf-readelf" -SW "$OUT/$name.elf" > "$OUT/$name-sections.txt" || return 1
    "$RV/riscv64-unknown-elf-readelf" -h "$OUT/$name.elf" > "$OUT/$name-header.txt" || return 1
    "$RV/riscv64-unknown-elf-objdump" -d "$OUT/$name.elf" > "$OUT/$name-disassembly.txt" || return 1
    awk '$1==".text" {print ".text bytes =", $2}
         $1==".data" || $1==".bss" || $1==".rodata" {sum += $2}
         END {print "static data bytes =",sum; if(sum>131072) exit 1}' "$OUT/$name-size.txt" || return 1
    # Inspect readelf's separate section-flag field, not the section name.
    awk '{for(i=1;i<=NF;i++) {
            if($i==".rodata") {ro=1; f=$(i+6); if(f !~ /A/ || f ~ /W/) bad=1}
            if($i==".data") {rw=1; f=$(i+6); if(f !~ /W/) bad=1}
          }}
         END {if(!ro || !rw || bad) {print "Section flags check FAILED"; exit 1}
              print "Section flags OK: read-only .rodata, writable .data"}' "$OUT/$name-sections.txt"
}

for name in full_v0 full_v1_1 full_v1_1_D; do
    printf '\nCHECK %s\n' "$name"
    "$RV/riscv64-unknown-elf-gcc" "${flags[@]}" "$name.s" -o "$OUT/$name.elf" || exit 1
    inspect "$name" || exit 1
    "$RIPES" --mode cli --proc RV32_ISS --src "$OUT/$name.elf" -t elf --iret \
        --output "$OUT/$name-iret.txt" > "$OUT/$name-console.txt" 2>&1
    cat "$OUT/$name-console.txt"
    grep -q 'ALL PASSED' "$OUT/$name-console.txt" || { echo "Correctness harness failed"; exit 1; }
done

for version in v0 v1_1_D; do
    for state in 21345671111111 54721631111111 14325671111111; do
        name="bench_${version}_${state}"
        printf '\nMEASURE %s\n' "$name"
        "$RV/riscv64-unknown-elf-gcc" "${flags[@]}" "-DINPUT=\"$state\"" \
            "bench_${version}.S" -o "$OUT/$name.elf" || exit 1
        inspect "$name" || exit 1
        # The guest intentionally exits with solution length (11), not zero.
        "$RIPES" --mode cli --proc RV32_ISS --src "$OUT/$name.elf" -t elf --iret \
            --output "$OUT/$name-iret.txt" > "$OUT/$name-console.txt" 2>&1
        cat "$OUT/$name-console.txt"
        [[ -s "$OUT/$name-iret.txt" ]] || { echo "No retired-instruction report"; exit 1; }
        cat "$OUT/$name-iret.txt"
    done
done
printf '\nCompleted. Reports: %s\n' "$OUT"
