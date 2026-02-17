# Linx ISA Footprint Harness

`compare_linx_footprint.py` compares baseline and candidate Linx binaries for:

1. static code footprint (`.text` bytes via `llvm-size -A`)
2. optional dynamic footprint (retired instruction count parsed from emulator output)

It is intended for the staged rollout of Linx code-size/link-relax optimizations.

## Inputs

- Two directory roots containing matching binaries:
  - baseline: e.g. `out/baseline`
  - candidate: e.g. `out/candidate`
- A binary set, either:
  - `--binary-list` file with relative paths, or
  - `--glob` patterns (repeatable) to auto-discover matching files.

## Basic Usage

```bash
python3 llvm/utils/linxisa/compare_linx_footprint.py \
  --baseline-dir out/baseline \
  --candidate-dir out/candidate \
  --binary-list corpus.txt \
  --llvm-size /Users/zhoubot/llvm-project/build-linxisa-clang/bin/llvm-size \
  --json-out out/linx-footprint.json \
  --csv-out out/linx-footprint.csv
```

## With Emulator Retired Counts

Use `--emulator-cmd-template` with `{bin}` placeholder and a regex that captures
the retired count integer.

```bash
python3 llvm/utils/linxisa/compare_linx_footprint.py \
  --baseline-dir out/baseline \
  --candidate-dir out/candidate \
  --binary-list corpus.txt \
  --emulator-cmd-template 'linx-emu --retired {bin}' \
  --retired-regex '(?i)retired(?:[_ ]instructions?)?\\s*[:=]\\s*([0-9]+)'
```

The command template is executed with the user shell, so environment setup and
emulator wrappers can be used directly.

## Acceptance Gates

For CI-style pass/fail, use:

- `--target-text-reduction-pct` (for geomean `.text` reduction target)
- `--max-retired-regression-pct` (for geomean retired-instruction regression cap)

Example matching the current experimental goals:

```bash
python3 llvm/utils/linxisa/compare_linx_footprint.py \
  --baseline-dir out/baseline \
  --candidate-dir out/candidate \
  --binary-list corpus.txt \
  --emulator-cmd-template 'linx-emu --retired {bin}' \
  --target-text-reduction-pct 3.0 \
  --max-retired-regression-pct 0.5
```

## Recommended Candidate Flag Bundle

Compiler:

```text
-mllvm -linx-enable-neg-imm-canon
-mllvm -linx-enable-mask-setc-fold
-mllvm -linx-enable-cshift16
-mllvm -linx-enable-t1-motion
-mllvm -linx-codesize-balance-mode=balanced
```

Linker:

```text
--relax
--linx-relax-seq-fusion
```
