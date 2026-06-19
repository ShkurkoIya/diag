#!/usr/bin/env bash
# regen_asn1c_lte.sh — regenerate asn1c LTE RRC sources cleanly.
#
# Workflow:
#   1. Auto-detect asn1c fork (mouse07410 vs vlm/upstream) and pick flags
#   2. Wipe output directory
#   3. Run asn1c with -fcompound-names -pdu=all + space-saver flags
#   4. Search for asn1c skeletons; if missing, copy from --skeletons-from
#   5. Patch generated .c — remove duplicate weak alias declarations (Rel-15+ bug)
#   6. Patch generated .h — remove extern declarations for codec constraints
#      we don't support (JER, XER, OER, APER, CBOR). This avoids:
#        "unknown type name 'asn_jer_constraints_t'"
#      errors when skeletons don't define these types.
#
# Recommended .asn input:
#   EUTRA-RRC-Definitions.asn EUTRA-InterNodeDefinitions.asn EUTRA-UE-Variables.asn
# OR a single bundled file:
#   lte-rrc-19.0.0.asn1

set -euo pipefail

# ── Argument parsing ────────────────────────────────────────────────────
SKELETONS_FROM=""
ASN_FILES=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --skeletons-from)        SKELETONS_FROM="$2"; shift 2 ;;
        --skeletons-from=*)      SKELETONS_FROM="${1#*=}"; shift ;;
        -h|--help)
            cat <<EOF
Usage: $0 [--skeletons-from <dir>] <asn-file> [<asn-file> ...]

Options:
  --skeletons-from <dir>  Directory with asn1c skeleton .c/.h support files.
                          Auto-searched in standard locations if not given.

Recommended for LTE cell discovery (resolves IMPORTS):
  $0 EUTRA-RRC-Definitions.asn \\
     EUTRA-InterNodeDefinitions.asn \\
     EUTRA-UE-Variables.asn
EOF
            exit 0 ;;
        *)
            if [[ ! -f "$1" ]]; then
                echo "ERROR: ASN.1 file not found: $1"; exit 1
            fi
            ASN_FILES+=("$(realpath "$1")"); shift ;;
    esac
done

if [[ ${#ASN_FILES[@]} -eq 0 ]]; then
    echo "ERROR: no ASN.1 files specified. Run with -h for help."; exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OUTPUT_DIR="$PROJECT_ROOT/src/asn1c_lte"

echo "[regen] Project root: $PROJECT_ROOT"
echo "[regen] Output dir:   $OUTPUT_DIR"

# ── Detect asn1c fork & flags ───────────────────────────────────────────
echo "[regen] Detecting asn1c capabilities..."
ASN1C_HELP=$(asn1c -h 2>&1 || true)

PER_FLAG=""
if echo "$ASN1C_HELP" | grep -q "gen-UPER"; then
    PER_FLAG="-gen-UPER"
    echo "[regen]   detected: mouse07410 fork (uses -gen-UPER)"
elif echo "$ASN1C_HELP" | grep -q "gen-PER"; then
    PER_FLAG="-gen-PER"
    echo "[regen]   detected: vlm/upstream (uses -gen-PER)"
else
    PER_FLAG="-gen-UPER"
fi

# Disable codecs we don't need. Each flag is probed against asn1c -h to
# avoid feeding unknown flags. These DRAMATICALLY reduce binary size
# because they tell asn1c not to emit code paths for the disabled codec.
EXTRA_FLAGS=()
for opt in "-no-gen-BER" "-no-gen-XER" "-no-gen-OER" "-no-gen-APER" \
           "-no-gen-JER" "-no-gen-CBOR" "-no-gen-print" "-no-gen-random-fill"; do
    if echo "$ASN1C_HELP" | grep -q -- "${opt#-no-gen-}\|${opt#-}"; then
        EXTRA_FLAGS+=("$opt")
    fi
done
if [[ ${#EXTRA_FLAGS[@]} -gt 0 ]]; then
    echo "[regen]   space-saver flags: ${EXTRA_FLAGS[*]}"
fi

# ── Wipe output dir ─────────────────────────────────────────────────────
mkdir -p "$OUTPUT_DIR"
echo "[regen] Wiping $OUTPUT_DIR..."
rm -f "$OUTPUT_DIR"/*.c "$OUTPUT_DIR"/*.h "$OUTPUT_DIR"/*.a "$OUTPUT_DIR"/*.o
rm -f "$OUTPUT_DIR"/Makefile.am.libasncodec
rm -f "$OUTPUT_DIR"/converter-example.*

# ── Validate inputs ─────────────────────────────────────────────────────
echo "[regen] Validating ASN.1 sources..."
( cd "$OUTPUT_DIR" && asn1c -EF "${ASN_FILES[@]}" >/dev/null ) || {
    echo "[regen] WARN: asn1c -EF reported issues, continuing"
}

# ── Generate ────────────────────────────────────────────────────────────
echo "[regen] Generating C sources from ${#ASN_FILES[@]} input file(s)..."
echo "[regen]   asn1c -fcompound-names -pdu=all $PER_FLAG ${EXTRA_FLAGS[*]} ..."
( cd "$OUTPUT_DIR" && asn1c -fcompound-names -pdu=all "$PER_FLAG" "${EXTRA_FLAGS[@]}" "${ASN_FILES[@]}" )
rm -f "$OUTPUT_DIR"/converter-example.*

# ── Ensure skeletons are present ────────────────────────────────────────
REQUIRED_SKELETONS=(
    "asn_application.h" "asn_internal.h" "constr_TYPE.h"
    "per_decoder.c" "per_decoder.h" "BIT_STRING.c" "BIT_STRING.h"
)
skeletons_missing=0
for f in "${REQUIRED_SKELETONS[@]}"; do
    [[ -f "$OUTPUT_DIR/$f" ]] || skeletons_missing=$((skeletons_missing + 1))
done

if [[ $skeletons_missing -gt 0 ]]; then
    echo "[regen] $skeletons_missing required skeleton files missing — searching..."
    SKELETON_CANDIDATES=()
    [[ -n "$SKELETONS_FROM" ]] && SKELETON_CANDIDATES+=("$SKELETONS_FROM")
    SKELETON_CANDIDATES+=(
        "${ASN1C_SKELETONS_DIR:-}"
        "/usr/local/share/asn1c"
        "/usr/share/asn1c"
        "/opt/asn1c/share/asn1c"
        "$HOME/.local/share/asn1c"
        "/c/Program Files/asn1c/share/asn1c"
        "/c/Program Files (x86)/asn1c/share/asn1c"
        "$(dirname "$(which asn1c 2>/dev/null || echo /nonexistent)")/skeletons"
        "$(dirname "$(which asn1c 2>/dev/null || echo /nonexistent)")/../share/asn1c"
    )

    SKELETONS_DIR=""
    for cand in "${SKELETON_CANDIDATES[@]}"; do
        if [[ -n "$cand" && -f "$cand/asn_application.h" ]]; then
            SKELETONS_DIR="$cand"; break
        fi
    done

    if [[ -z "$SKELETONS_DIR" ]]; then
        echo "[regen] ERROR: skeletons not found. Use --skeletons-from <dir>"
        exit 1
    fi

    echo "[regen] Copying skeletons from $SKELETONS_DIR..."
    COPIED=0
    for src in "$SKELETONS_DIR"/*.c "$SKELETONS_DIR"/*.h; do
        [[ -f "$src" ]] || continue
        bn="$(basename "$src")"
        if [[ ! -f "$OUTPUT_DIR/$bn" ]]; then
            cp "$src" "$OUTPUT_DIR/$bn"
            COPIED=$((COPIED + 1))
        fi
    done
    echo "[regen] Copied $COPIED skeleton files"
fi

# ── PATCH 1: clean up duplicate weak aliases in .c (Rel-15+ bug) ────────
echo "[regen] [.c patch] Stripping duplicate unsuffixed weak aliases..."
PATCHED=0
P1='^extern asn_TYPE_descriptor_t asn_DEF_[a-zA-Z_][a-zA-Z0-9_]* __attribute__\(\(weak, alias\("asn_DEF_[a-zA-Z_][a-zA-Z0-9_]*_[0-9]+"\)\)\);$'
for f in "$OUTPUT_DIR"/*.c; do
    if grep -qE "$P1" "$f" 2>/dev/null; then
        grep -vE "$P1" "$f" > "${f}.patched"
        mv "${f}.patched" "$f"
        PATCHED=$((PATCHED + 1))
    fi
done
echo "[regen]   stripped duplicate aliases from $PATCHED file(s)"

# ── PATCH 2: clean up codec-specific extern decls in .h ─────────────────
# When asn1c generates .h files, even with -no-gen-JER it sometimes emits:
#   extern asn_jer_constraints_t asn_JER_type_FOO_constr_1;
# These decls require type asn_jer_constraints_t which doesn't exist
# without JER support. We strip them — safe because no code references
# these symbols when JER/XER/OER/APER/CBOR are disabled.
echo "[regen] [.h patch] Stripping unused codec constraint declarations..."
H_PATCHED=0
H_PATTERNS=(
    '^extern asn_jer_constraints_t asn_JER_type_[a-zA-Z_][a-zA-Z0-9_]*_constr_[0-9]+;$'
    '^extern asn_xer_constraints_t asn_XER_type_[a-zA-Z_][a-zA-Z0-9_]*_constr_[0-9]+;$'
    '^extern asn_oer_constraints_t asn_OER_type_[a-zA-Z_][a-zA-Z0-9_]*_constr_[0-9]+;$'
    '^extern asn_aper_constraints_t asn_APER_type_[a-zA-Z_][a-zA-Z0-9_]*_constr_[0-9]+;$'
    '^extern asn_cbor_constraints_t asn_CBOR_type_[a-zA-Z_][a-zA-Z0-9_]*_constr_[0-9]+;$'
)

# Combine into one OR-pattern for one grep pass per file
COMBINED="$(printf "%s|" "${H_PATTERNS[@]}")"
COMBINED="${COMBINED%|}"

for f in "$OUTPUT_DIR"/*.h; do
    if grep -qE "$COMBINED" "$f" 2>/dev/null; then
        grep -vE "$COMBINED" "$f" > "${f}.patched"
        mv "${f}.patched" "$f"
        H_PATCHED=$((H_PATCHED + 1))
    fi
done
echo "[regen]   stripped codec decls from $H_PATCHED .h file(s)"

# ── Sanity check ────────────────────────────────────────────────────────
NUM_C=$(find "$OUTPUT_DIR" -maxdepth 1 -name "*.c" | wc -l)
NUM_H=$(find "$OUTPUT_DIR" -maxdepth 1 -name "*.h" | wc -l)
echo "[regen] Total: $NUM_C .c files, $NUM_H .h files"

[[ -f "$OUTPUT_DIR/asn_application.h" ]] || { echo "[regen] FATAL: asn_application.h missing"; exit 1; }
[[ -f "$OUTPUT_DIR/pdu_collection.c" ]] || { echo "[regen] FATAL: pdu_collection.c missing"; exit 1; }

PDU_COUNT=$(grep -c '^\s*&asn_DEF_' "$OUTPUT_DIR/pdu_collection.c" 2>/dev/null || echo "?")
echo "[regen] pdu_collection.c contains $PDU_COUNT PDU types"

# Verify .h patch worked — none of these patterns should remain
LEFTOVER=$(grep -lE "$COMBINED" "$OUTPUT_DIR"/*.h 2>/dev/null | wc -l)
if [[ $LEFTOVER -gt 0 ]]; then
    echo "[regen] WARNING: $LEFTOVER .h files still contain patterns — first 3:"
    grep -lE "$COMBINED" "$OUTPUT_DIR"/*.h 2>/dev/null | head -3
fi

echo
echo "[regen] Done. Build with:"
echo "    cmake -B build -DUSE_ASN1C_LTE=ON"
echo "    cmake --build build -j"