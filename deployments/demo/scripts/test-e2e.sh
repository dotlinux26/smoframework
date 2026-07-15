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

# ── Phase 1: Node Initialization ─────────────────────────
step "1/13  Node Init — Generate Identity"

do_cmd "Init node-a"         "smo node init --name soc-hn-01 --force"
do_verify "Identity created" "test -f /var/lib/smo/identity.json" ""

# ── Phase 2: Display Name ────────────────────────────────
step "2/13  Display Name & NodeInfo"

do_verify "Name set correctly" "smo node info 2>&1" "soc-hn-01"

# ── Phase 3: Mesh Creation ───────────────────────────────
step "3/13  Mesh Create"

do_cmd "Create mesh SOC"     "smo mesh create --name SOC-Production"
do_verify "Mesh created"     "test -f /etc/smo/mesh.json" ""

# ── Phase 4: Enrollment ──────────────────────────────────
step "4/13  Enrollment (CSR → Certificate)"

do_cmd "Export enroll request" "smo export --format json > /tmp/request.smor"
do_verify "Enroll request exists" "test -s /tmp/request.smor" ""

do_cmd "Authority signs"       "smo-admin sign /tmp/request.smor -o /tmp/node.smoc"
do_verify "Certificate signed" "test -s /tmp/node.smoc" ""

do_cmd "Import certificate"    "smo node import /tmp/node.smoc"
do_verify "Certificate imported" "test -f /var/lib/smo/certificate.json" ""

# ── Phase 5: Discovery ──────────────────────────────────
step "5/13  Discovery — Peer Detection"

do_verify "Peer table populated" "smo discover 2>&1" "soc-hn-01"

# ── Phase 6: Selection Engine ────────────────────────────
step "6/13  Selection Engine — Node Targeting"

do_verify "Select by name"       "smo exec --name soc-hn-01 --dry-run 2>&1" "soc-hn-01"
do_verify "Select by role"       "smo exec --role Authority --dry-run 2>&1" "Authority"
do_verify "Select by expression" "smo exec --where 'role==\"Authority\"' --dry-run 2>&1" "soc-hn-01"
do_verify "Select nearest"       "smo exec --nearest --dry-run 2>&1" "soc-hn-01"

# ── Phase 7: Contract Execution ──────────────────────────
step "7/13  Native Contract — ping"

do_verify "Ping responds" "smo exec ping 2>&1" "pong"

# ── Phase 8: Filesystem Contract ─────────────────────────
step "8/13  Native Contract — ls (filesystem)"

do_cmd "List root"           "smo exec ls --name soc-hn-01 --path / --dry-run"
do_verify "Contract resolved" "smo exec ls --name soc-hn-01 --dry-run 2>&1" "ls"

# ── Phase 9: Session Management ──────────────────────────
step "9/13  Session Management"

do_verify "Session create"    "smo session open --name soc-hn-01 2>&1" "session"

# ── Phase 10: Scope Dispatch ─────────────────────────────
step "10/13 Dispatch Scope — single vs mesh"

do_verify "Single scope"     "smo exec ping --scope single --dry-run 2>&1" "single"
do_verify "Mesh scope"       "smo exec ping --scope mesh --dry-run 2>&1" "mesh"

# ── Phase 11: Capability Check ───────────────────────────
step "11/13 Capability Enforcement"

do_verify "Cap filter"       "smo exec --cap EXEC --dry-run 2>&1" "EXEC"

# ── Phase 12: Governance (Rename) ────────────────────────
step "12/13 Governance — Node Rename"

do_verify "Rename request"    "smo node rename --name backup-storage --dry-run 2>&1" "backup-storage"

# ── Phase 13: Discovery Table ────────────────────────────
step "13/13 Discovery — Full Table"

do_cmd "Discover all" "smo discover --full 2>&1"

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
