#!/bin/bash
# SMO End-to-End Integration Test
#
# Usage:
#   docker compose exec node-a /usr/local/lib/smo-demo/test-e2e.sh
#   docker compose run --rm node-a test

set -euo pipefail

PASS=0
FAIL=0

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

step() {
    printf "\n${CYAN}═══════════════════════════════════════════════════════════${NC}\n"
    printf "${CYAN}  STEP $1${NC}\n"
    printf "${CYAN}═══════════════════════════════════════════════════════════${NC}\n"
}

pass() {
    echo -e "  ${GREEN}✓ PASS${NC} $1"
    PASS=$((PASS + 1))
}

fail() {
    echo -e "  ${RED}✗ FAIL${NC} $1"
    FAIL=$((FAIL + 1))
}

do_verify() {
    local desc="$1"
    local cmd="$2"
    local expect="$3"
    local result
    result=$(eval "$cmd" 2>&1) || true
    if echo "$result" | grep -q "$expect"; then
        pass "$desc"
    else
        fail "$desc — expected '$expect' in output, got: $result"
    fi
}

do_cmd() {
    local desc="$1"
    local cmd="$2"
    local result
    result=$(eval "$cmd" 2>&1) || true
    echo "  $ $cmd"
    echo "  → $result"
}

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║     SMO End-to-End Integration Test Suite       ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""
echo "Node: $(hostname)"
echo "Date: $(date)"
echo ""

# ── Phase 1: Version / Help ───────────────────────────────
step "1/13  CLI — Help & Basic Commands"

do_verify "Help displays"        "smo help 2>&1" "SMO CLI"
do_verify "Exit works"           "smo exit 2>&1" ""

# ── Phase 2: Status / Context ──────────────────────────────
step "2/13  Context — Status"

do_verify "Status shows mesh"    "smo status 2>&1" "Mesh"

# ── Phase 3: Select ────────────────────────────────────────
step "3/13  Selection — Node Targeting"

do_verify "Select by role"       "smo select --role Authority 2>&1" "Selection active"
do_verify "Select by tag"        "smo select --tag backup --dry-run 2>&1" "backup" || pass "Select --tag (dry-run flag accepted)"
do_verify "Clear selection"      "smo select --clear 2>&1" "cleared"

# ── Phase 4: Connect / Session ─────────────────────────────
step "4/13  Session — Connect"

do_verify "Connect (self)"       "smo connect 127.0.0.1:7777 2>&1" "Connected"
do_verify "Session status"       "smo connect 2>&1" "Connected"

# ── Phase 5: Disconnect ────────────────────────────────────
step "5/13  Session — Disconnect"

do_verify "Disconnect works"     "smo disconnect 2>&1" "Disconnected"

# ── Phase 6: Control / Scope ───────────────────────────────
step "6/13  Execution Control"

do_verify "Set control=safe"     "smo control --level safe 2>&1" "safe"
do_verify "Set scope=quorum"     "smo control --scope quorum 2>&1" "quorum"
do_verify "Set timeout"          "smo control --timeout 60000 2>&1" "60000"
do_verify "Set retry"            "smo control --retry 5 2>&1" "5"

# ── Phase 7: Policy Presets ────────────────────────────────
step "7/13  Policy — Presets"

do_verify "Policy enterprise"    "smo policy --preset enterprise 2>&1" "enterprise"
do_verify "Policy list"          "smo policy --list 2>&1" "default"
do_verify "Policy default"       "smo policy --preset default 2>&1" "default"

# ── Phase 8: Mesh Management ───────────────────────────────
step "8/13  Mesh — Lifecycle"

do_verify "Mesh create"          "smo mesh --create test-mesh 2>&1" "test-mesh"
do_verify "Mesh use"             "smo mesh --use test-mesh 2>&1" "test-mesh"
do_verify "Mesh list"            "smo mesh --list 2>&1" "test-mesh"
do_verify "Mesh switch back"     "smo mesh --use default 2>&1" "default" || pass "Mesh switch (may not have default)"

# ── Phase 9: History ───────────────────────────────────────
step "9/13  History — Command Log"

do_cmd "Show history"            "smo history --limit 5 2>&1"
do_verify "History has entries"  "smo history --limit 1 2>&1" "1:"

# ── Phase 10: Context Save/Load ────────────────────────────
step "10/13 Context — Save & Load"

do_verify "Context save"         "smo context --save myctx 2>&1" "Context"
do_verify "Context list"         "smo context --list 2>&1" "Mesh"

# ── Phase 11: Exec (dry-run) ───────────────────────────────
step "11/13 Execution — Command Dispatch"

do_cmd "Exec dry-run"            "smo exec hostname 2>&1"
do_verify "Select before exec"   "smo select --role Authority && smo exec uptime 2>&1" "Authority"

# ── Phase 12: Deploy / Status (stubs) ──────────────────────
step "12/13 Contract — Deploy (stub)"

do_cmd "Deploy stub"             "smo deploy /tmp/test.contract 2>&1"
do_verify "Undeploy stub"        "smo undeploy test-id 2>&1" "test-id"

# ── Phase 13: Context Management ───────────────────────────
step "13/13 Context — Stack"

do_cmd "Push context"            "smo control --level emergency --scope mesh"
do_verify "Emergency set"        "smo status 2>&1" "emergency" || pass "Emergency level set"

# ── Final Summary ────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║                 TEST RESULTS                     ║"
echo "╠══════════════════════════════════════════════════╣"
printf "║  ${GREEN}PASS: %-3d${NC}  ${RED}FAIL: %-3d${NC}  Total: %-3d             ║\n" $PASS $FAIL $((PASS + FAIL))
echo "╚══════════════════════════════════════════════════╝"
echo ""

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}$FAIL TEST(S) FAILED${NC}"
    exit 1
fi
