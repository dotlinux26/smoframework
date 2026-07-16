#!/bin/bash
# SMO Demo Runner — orchestrates the full 3-node demo
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
COMPOSE_FILE="${ROOT}/deployments/demo/docker-compose.yml"

# Detect Docker Compose variant
if which docker-compose &>/dev/null; then
    DOCKER_COMPOSE="docker-compose"
elif docker --help 2>/dev/null | grep -qi compose; then
    DOCKER_COMPOSE="docker compose"
else
    echo "ERROR: Neither docker-compose nor 'docker compose' found"
    exit 1
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
NC='\033[0m'

banner() {
    echo ""
    echo -e "${CYAN}╔══════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║     SMO Mesh Demo — 3-Node Cluster             ║${NC}"
    echo -e "${CYAN}╚══════════════════════════════════════════════════╝${NC}"
    echo ""
}

step() {
    echo ""
    echo -e "${GREEN}━━━ $1${NC}"
}

cmd() {
    echo -e "  ${CYAN}\$ $*${NC}"
    eval "$@"
}

# Parse args
ACTION="${1:-up}"

case "$ACTION" in
    build)
        banner
        step "Building Docker images..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" build
        echo ""
        echo -e "${GREEN}Build complete.${NC}"
        ;;

    up)
        banner
        step "Starting 3-node SMO mesh..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" up -d
        echo ""
        echo -e "${GREEN}Containers started.${NC}"
        echo ""
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" ps
        ;;

    down)
        step "Stopping demo..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" down -v
        echo -e "${GREEN}Done.${NC}"
        ;;

    status)
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" ps
        ;;

    logs)
        shift || true
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" logs "${@:-}"
        ;;

    exec)
        shift
        if [ $# -lt 2 ]; then
            echo "Usage: $0 exec <node> <command...>"
            echo "  Nodes: node-a, node-b, node-c"
            exit 1
        fi
        NODE="$1"; shift
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" exec "$NODE" "${@:-bash}"
        ;;

    test)
        shift || true
        NODE="${1:-node-a}"
        step "Running E2E tests on $NODE..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" exec "$NODE" /usr/local/lib/smo-demo/test-e2e.sh
        ;;

    shell)
        NODE="${2:-node-a}"
        step "Opening shell on $NODE..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" exec "$NODE" bash
        ;;

    discover)
        step "Mesh status on node-a..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" exec node-a smo mesh --list 2>/dev/null || \
            echo "(Mesh listing — waiting for daemon)"
        ;;

    exec)
        shift
        step "Executing command via node-a..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" exec node-a smo exec "$@"
        ;;

    clean)
        step "Full cleanup..."
        cmd $DOCKER_COMPOSE -f "$COMPOSE_FILE" down -v --rmi all
        echo -e "${GREEN}Cleaned.${NC}"
        ;;

    *)
        echo "SMO Demo Runner"
        echo ""
        echo "Usage: $0 <command>"
        echo ""
        echo "Commands:"
        echo "  build           Build Docker images"
        echo "  up              Start the 3-node mesh"
        echo "  down            Stop the mesh"
        echo "  status          Show container status"
        echo "  logs [node]     Show logs"
        echo "  exec <node> ..  Run command on a node"
        echo "  test [node]     Run E2E integration tests"
        echo "  shell [node]    Open shell on a node"
        echo "  discover        Show discovery table"
        echo "  clean           Full cleanup (remove images)"
        echo ""
        exit 1
        ;;
esac
