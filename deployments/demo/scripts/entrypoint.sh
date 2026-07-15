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
IDENTITY_FILE="${SMO_DATA}/identity.json"
CERT_FILE="${SMO_DATA}/certificate.json"

mkdir -p "$SMO_DATA" "$SMO_ETC"

log() { echo "[$(date +%H:%M:%S)] $*"; }

phase_init() {
    log "=== Phase 1: Node Init ==="
    if [ -f "$IDENTITY_FILE" ]; then
        log "Identity exists — skipping init"
    else
        log "Initializing node: $NODE_NAME (role: $NODE_ROLE)"
        smo node init --name "$NODE_NAME"
        echo "{\"name\":\"$NODE_NAME\",\"role\":\"$NODE_ROLE\",\"port\":$NODE_PORT,\"mesh\":\"$MESH_NAME\"}" > "$SMO_DATA/node.json"
    fi
}

phase_discover() {
    log "=== Phase 2: Discovery ==="
    if [ "$IS_AUTHORITY" = "true" ]; then
        log "This node is the mesh Authority — waiting for peers..."
        smo discover --wait 30
    else
        if [ -n "$SEED_NODE" ]; then
            log "Connecting to seed: $SEED_NODE"
            smo node connect --address "$SEED_NODE"
            sleep 2
            smo discover
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
        phase_discover
        phase_ready

        # Keep running
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
