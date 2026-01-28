#!/usr/bin/env bash
set -uo pipefail   # 注意：去掉 -e，避免失败直接退出整个脚本

BASE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
MLIR_PTO="${BASE_DIR}/../../../../build/bin/ptoas"

usage() {
  cat <<EOF
Usage:
  $0 -t <name>   # e.g. -t a  -> uses folder A, script a.py
  $0 all         # traverse every subfolder under ${BASE_DIR}
EOF
  exit 1
}

ucfirst() { echo "${1^}"; }
lcfirst() { echo "${1,}"; }

# 记录结果：name -> "OK|FAIL|SKIP"
declare -A RESULT
declare -A DETAIL

process_one() {
  local a="$1"          # e.g. a
  local A dir py mlir cpp tmp
  A="$(ucfirst "$a")"

  dir="${BASE_DIR}/${A}"
  py="${dir}/${a}.py"
  mlir="${dir}/${a}-pto-ir.mlir"
  cpp="${dir}/${a}-pto.cpp"

  # 工具不存在：这是全局致命错误
  if [[ ! -x "$MLIR_PTO" ]]; then
    RESULT["$A"]="FAIL"
    DETAIL["$A"]="Missing executable: $MLIR_PTO"
    return 1
  fi

  # 目录不存在：这里也算 SKIP（你也可以改成 FAIL）
  if [[ ! -d "$dir" ]]; then
    RESULT["$A"]="SKIP"
    DETAIL["$A"]="Missing dir: $dir"
    return 2
  fi

  # 找不到 py：按你的需求 SKIP，继续下一个
  if [[ ! -f "$py" ]]; then
    RESULT["$A"]="SKIP"
    DETAIL["$A"]="Missing python: $py"
    return 2
  fi

  # 2) python -> mlir
  if ! python3 "$py" > "$mlir"; then
    RESULT["$A"]="FAIL"
    DETAIL["$A"]="python3 failed: $py"
    return 1
  fi

  # 3) mlir-pto -> cpp
  if ! "$MLIR_PTO" "$mlir" > "$cpp"; then
    RESULT["$A"]="FAIL"
    DETAIL["$A"]="ptoas failed: $mlir"
    return 1
  fi

  # 4) post-process cpp
  tmp="$(mktemp)"

  awk '
    $0 == "#include \"common/pto_instr.hpp\"" { keep=1 }
    keep { print }
  ' "$cpp" > "$tmp"

  # delete last 3 lines（文件行数不足也不致命：失败则记 FAIL）
  if ! sed -i '$d' "$tmp" || ! sed -i '$d' "$tmp" || ! sed -i '$d' "$tmp"; then
    rm -f "$tmp"
    RESULT["$A"]="FAIL"
    DETAIL["$A"]="post-process sed failed: $cpp"
    return 1
  fi

  mv "$tmp" "$cpp"

  RESULT["$A"]="OK"
  DETAIL["$A"]="generated: $cpp"
  return 0
}

print_summary() {
  local ok=0 fail=0 skip=0
  echo "========== SUMMARY =========="
  # 为了输出稳定，按字母序打印
  for k in "${!RESULT[@]}"; do
    :
  done | true

  # bash 无法直接对 assoc key 排序，这里用 printf+sort
  while IFS= read -r A; do
    printf "%-6s %-4s %s\n" "$A" "${RESULT[$A]}" "${DETAIL[$A]}"
    case "${RESULT[$A]}" in
      OK)   ((ok++)) ;;
      FAIL) ((fail++)) ;;
      SKIP) ((skip++)) ;;
    esac
  done < <(printf "%s\n" "${!RESULT[@]}" | sort)

  echo "-----------------------------"
  echo "OK=$ok  FAIL=$fail  SKIP=$skip"
  echo "============================="
}

if [[ $# -eq 1 && "$1" == "all" ]]; then
  shopt -s nullglob
  for d in "${BASE_DIR}"/*/; do
    A="$(basename "$d")"
    a="$(lcfirst "$A")"
    process_one "$a" || true   # 不让单个失败中断 all
  done
  print_summary
elif [[ $# -eq 2 && "$1" == "-t" ]]; then
  a="$2"
  process_one "$a" || true
  print_summary
else
  usage
fi
