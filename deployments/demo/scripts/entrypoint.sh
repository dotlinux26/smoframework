#!/bin/bash
set -euo pipefail

NODE_ROLE="${SMO_NODE_ROLE:-Member}"
NODE_NAME="${SMO_NODE_NAME:-unknown}"
NODE_PORT="${SMO_NODE_PORT:-7777}"
MESH_NAME="${SMO_MESH_NAME:-SOC-Production}"
SEED_NODE="${SMO_SEED_NODE:-}"
IS_AUTHORITY="${SMO_IS_AUTHORITY:-false}"

SMO_DATA="/var/lib/smo"
MESH_DIR="/var/lib/smo-mesh"

log() { echo "[$(date +%H:%M:%S)] $*"; }

phase_init() {
    log "=== Phase 1: Node Init ==="
    log "Node: $NODE_NAME, Role: $NODE_ROLE, Mesh: $MESH_NAME"
    log ""

    if [ ! -f "$SMO_DATA/identity.json" ]; then
        smo-node --init --name "$NODE_NAME" --data "$SMO_DATA"
        log "Identity created for $NODE_NAME"
    else
        log "Identity already exists at $SMO_DATA"
    fi

    if [ "$IS_AUTHORITY" = "true" ]; then
        if [ ! -f "$MESH_DIR/mesh.json" ]; then
            mkdir -p "$MESH_DIR"
            smo-admin --mesh-dir "$MESH_DIR" create-mesh "$MESH_NAME"
            log "Mesh '$MESH_NAME' created at $MESH_DIR"
        else
            log "Mesh already exists at $MESH_DIR"
        fi

        # Publish mesh (configure bootstrap endpoints)
        log "Publishing mesh..."
        smo-admin --mesh-dir "$MESH_DIR" mesh publish --port "$NODE_PORT" || true

        log "Mesh published. Other nodes can now join."
    fi
}

phase_enroll() {
    if [ "$IS_AUTHORITY" = "true" ]; then
        return 0
    fi

    log "=== Phase 2: Enrollment ==="
    log "Waiting for mesh to be published by authority..."

    # Wait for mesh.json to appear (authority publishes it)
    local timeout=60
    while [ ! -f "$MESH_DIR/mesh.json" ] && [ $timeout -gt 0 ]; do
        sleep 1
        timeout=$((timeout - 1))
    done

    if [ ! -f "$MESH_DIR/mesh.json" ]; then
        log "Warning: Mesh not published yet, will rely on seed for bootstrap"
        return 0
    fi

    # Export CSR
    log "Exporting CSR for signing..."
    smo-node --export "$SMO_DATA/node.csr.smor" --data "$SMO_DATA"

    # Sign CSR via authority (copy to authority mesh dir and sign)
    # In demo, authority mesh dir is shared via volume or we copy
    local authority_mesh_dir="/tmp/authority-mesh"
    if [ -f "$authority_mesh_dir/mesh.json" ]; then
        log "Signing CSR with authority..."
        smo-admin --mesh-dir "$authority_mesh_dir" sign "$SMO_DATA/node.csr.smor" -o "$SMO_DATA/node.cert.smoc"

        log "Importing certificate..."
        smo-node --import "$SMO_DATA/node.cert.smoc" --data "$SMO_DATA"
    else
        log "Authority mesh dir not found at $authority_mesh_dir, will use seed bootstrap"
    fi
}

phase_ready() {
    log "=== Phase 3: Starting Daemon ==="
    local daemon_args="--daemon --port $NODE_PORT --data $SMO_DATA --name $NODE_NAME"
    if [ -n "$SEED_NODE" ]; then
        daemon_args="$daemon_args --seed $SEED_NODE"
    fi
    log "Exec: smo-node $daemon_args"
    exec smo-node $daemon_args
}

case "${1:-}" in
    node-a|node-b|node-c)
        phase_init
        phase_enroll
        phase_ready
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