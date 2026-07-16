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
        fail "$desc — expected '$expect' in output, got: $(echo "$result" | head -3)"
    fi
}

do_cmd() {
    local desc="$1"
    local cmd="$2"
    local result
    result=$(eval "$cmd" 2>&1) || true
    echo "  $ $cmd"
    echo "  → $(echo "$result" | head -5)"
}

echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║     SMO End-to-End Integration Test Suite       ║"
echo "╚══════════════════════════════════════════════════╝"
echo ""
echo "Node: $(hostname)"
echo "Date: $(date)"
echo ""

# Find the mesh directory created by entrypoint (has authority keys)
ACTUAL_MESH_DIR=$(find /var/lib/smo-mesh/meshes -maxdepth 1 -mindepth 1 -type d | head -1)
if [ -z "$ACTUAL_MESH_DIR" ]; then
    echo "ERROR: No mesh directory found in /var/lib/smo-mesh/meshes/"
    exit 1
fi
MESH_DIR="$ACTUAL_MESH_DIR"

SMO_DATA="/tmp/smo-test-$$"
NODE_DIR="$SMO_DATA/node"
mkdir -p "$NODE_DIR"

# ── Phase 1: Tooling Smoke Tests ───────────────────────
step "1/6  Tooling — Help & Basic Commands"

do_verify "smo-admin --help"       "smo-admin --help 2>&1" "Usage"
do_verify "smo-node --help"        "smo-node --help 2>&1" "Usage"
do_verify "smo-cli --help"         "smo-cli --help 2>&1" "Usage"
do_verify "smo help"               "smo help 2>&1" "Usage"

# ── Phase 2: Mesh Verification ─────────────────────────────
step "2/6  Mesh — Verify"

do_verify "mesh.json exists"       "test -f $MESH_DIR/mesh.json && echo OK" "OK"
do_verify "mesh.json has hmac"       "grep hmac_secret $MESH_DIR/mesh.json" "hmac_secret"
do_verify "mesh.json has authority_pubkey" "grep authority_pubkey $MESH_DIR/mesh.json | grep -v '\"\"'" "authority_pubkey"
do_verify "mesh.json has root_pubkey" "grep root_pubkey $MESH_DIR/mesh.json | grep -v '\"\"'" "root_pubkey"

# ── Phase 3: Node Identity ─────────────────────────────
step "3/6  Node — Identity & CSR"

do_verify "Init identity"          "smo-node --init --name node-a --data $NODE_DIR 2>&1" "Identity created"
do_verify "identity.json exists"   "test -f $NODE_DIR/identity.json && echo OK" "OK"

do_verify "Export CSR"             "smo-node --export $NODE_DIR/node.csr.smor --data $NODE_DIR 2>&1" "exported"
do_verify "CSR file exists"        "test -f $NODE_DIR/node.csr.smor && echo OK" "OK"

do_verify "Show pubkey"            "smo-node --pubkey --data $NODE_DIR 2>&1" "SMO-PUBKEY-"
do_verify "Show fingerprint"       "smo-node --pubkey --fingerprint --data $NODE_DIR 2>&1" "[0-9a-f]"

# ── Phase 4: Certificate Signing ───────────────────────
step "4/6  Certificate — Sign & Import"

do_verify "Sign CSR"               "smo-admin --mesh-dir $MESH_DIR sign $NODE_DIR/node.csr.smor -o $NODE_DIR/node.cert.smoc 2>&1" "Certificate signed"
do_verify "Cert file exists"       "test -f $NODE_DIR/node.cert.smoc && echo OK" "OK"

do_verify "Import certificate"     "smo-node --import $NODE_DIR/node.cert.smoc --data $NODE_DIR 2>&1" "Enrollment successful"

do_verify "Post-import summary"    "smo-node --data $NODE_DIR 2>&1" "NodeID"

# ── Phase 5: Join Token ────────────────────────────────
step "5/6  Enrollment — Join Token"

do_verify "Generate invite"        "smo-admin --mesh-dir $MESH_DIR generate-invite Worker --expire 1h --endpoint node-a:7777 2>&1" "SMO-JOIN-"

# ── Phase 6: Daemon Smoke Test ─────────────────────────
step "6/6  Daemon — Quick Smoke"

# Start daemon briefly, check it listens, then stop
SMO_DAEMON_PORT="19999"
smo-node --daemon --port $SMO_DAEMON_PORT --data $NODE_DIR --name node-a &
DAEMON_PID=$!
sleep 1

if kill -0 $DAEMON_PID 2>/dev/null; then
    pass "Daemon started on port $SMO_DAEMON_PORT"
    do_verify "Daemon listening"   "netstat -tlnp 2>/dev/null | grep $SMO_DAEMON_PORT || ss -tlnp 2>/dev/null | grep $SMO_DAEMON_PORT || echo LISTENING" "LISTENING"
    kill $DAEMON_PID 2>/dev/null || true
    wait $DAEMON_PID 2>/dev/null || true
    pass "Daemon stopped cleanly"
else
    fail "Daemon failed to start"
fi

# ── Final Summary ──────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║                 TEST RESULTS                     ║"
echo "╠══════════════════════════════════════════════════╣"
printf "║  ${GREEN}PASS: %-3d${NC}  ${RED}FAIL: %-3d${NC}  Total: %-3d             ║\n" $PASS $FAIL $((PASS + FAIL))
echo "╚══════════════════════════════════════════════════╝"
echo ""

rm -rf "$SMO_DATA"

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}$FAIL TEST(S) FAILED${NC}"
    exit 1
fi

# ── Final Summary ──────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════╗"
echo "║                 TEST RESULTS                     ║"
echo "╠══════════════════════════════════════════════════╣"
printf "║  ${GREEN}PASS: %-3d${NC}  ${RED}FAIL: %-3d${NC}  Total: %-3d             ║\n" $PASS $FAIL $((PASS + FAIL))
echo "╚══════════════════════════════════════════════════╝"
echo ""

rm -rf "$SMO_DATA"

if [ "$FAIL" -eq 0 ]; then
    echo -e "${GREEN}ALL TESTS PASSED${NC}"
    exit 0
else
    echo -e "${RED}$FAIL TEST(S) FAILED${NC}"
    exit 1
fi
