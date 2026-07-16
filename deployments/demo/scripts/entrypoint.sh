#!/bin/bash
set -euo pipefail

NODE_ROLE="${SMO_NODE_ROLE:-Member}"
NODE_NAME="${SMO_NODE_NAME:-unknown}"
NODE_PORT="${SMO_NODE_PORT:-7777}"
MESH_NAME="${SMO_MESH_NAME:-SOC-Production}"
SEED_NODE="${SMO_SEED_NODE:-}"
IS_AUTHORITY="${SMO_IS_AUTHORITY:-false}"

SMO_DATA="/var/lib/smo"
SMO_ETC="/etc/smo"

mkdir -p "$SMO_DATA" "$SMO_ETC"

log() { echo "[$(date +%H:%M:%S)] $*"; }

phase_init() {
    log "=== Phase 1: Node Init ==="
    log "Node: $NODE_NAME, Role: $NODE_ROLE, Mesh: $MESH_NAME"
    log "SMO runtime identity managed by smo-node daemon"
}

phase_mesh() {
    log "=== Phase 2: Mesh Setup ==="
    if [ "$IS_AUTHORITY" = "true" ]; then
        log "This node is the mesh Authority"
        smo mesh --create "$MESH_NAME" || log "Mesh may already exist"
        smo mesh --use "$MESH_NAME" || true
    else
        smo mesh --use "$MESH_NAME" 2>/dev/null || log "Mesh not local — will join via seed"
        if [ -n "$SEED_NODE" ]; then
            log "Connecting to seed: $SEED_NODE"
            sleep 2
            smo connect "$SEED_NODE" || log "Connect will work after daemon starts"
        fi
    fi
}

phase_ready() {
    log "=== Node READY ==="
    log "  Name:   $NODE_NAME"
    log "  Role:   $NODE_ROLE"
    log "  Mesh:   $MESH_NAME"
    log "  Port:   $NODE_PORT"
    log "  Data:   $SMO_DATA"
    log ""
    log "Waiting for commands..."
}

case "${1:-}" in
    node-a|node-b|node-c)
        phase_init
        phase_mesh
        phase_ready

        if command -v smo-node &>/dev/null; then
            exec smo-node --daemon --port "$NODE_PORT" --data "$SMO_DATA"
        else
            log "smo-node not found — running in CLI mode"
            tail -f /dev/null
        fi
        ;;
    test)
        shift
        exec /usr/local/lib/smo-demo/test-e2e.sh "$@"
        ;;
    shell)
        exec /bin/bash
        ;;
    *)
        echo "Usage: docker run ... [node-a|node-b|node-c|test|shell]"
        exit 1
        ;;
esac
