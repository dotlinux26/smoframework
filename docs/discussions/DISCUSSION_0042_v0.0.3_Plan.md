# DISCUSSION 0042 — Release 0.0.3: Internet Mesh, NAT Traversal & Runtime Maturity

**Status:** Planning  
**Target:** v0.0.3 (internet-ready mesh runtime)  
**Parent:** v0.0.2 — production-ready (LAN/VPN)  
**Postponed from v0.0.2:** P7 (NAT Traversal), P12 (SMIR Compiler)

---

## 1. Motivation

v0.0.2 delivers a production-ready mesh for LAN/VPN environments — anti-entropy, gossip readiness, CI/CD, packaging, structured logging, config versioning.

**v0.0.3 takes the mesh to the open internet.** The core challenge is NAT traversal: in a 3-node topology where only one node has a public IP and the other two sit behind NAT, nodes must still discover each other, gossip, heartbeat, and maintain consistency without a central relay.

This release focuses on three pillars:

1. **NAT Traversal** — make every node reachable regardless of network topology
2. **WAN Testing & Reliability** — systematic internet-grade failure testing
3. **Runtime Maturity** — SMIR compiler, advanced execution, performance

---

## 2. Architecture: NAT Traversal Design

### 2.1 Current Limitation

All transport today is direct TCP/UDP with hardcoded addresses. A node behind NAT can **outbound-connect** to a public node, but:

- Public node **cannot initiate** connections back to NAT nodes
- Two NAT nodes **cannot talk to each other** directly
- Discovery (HelloMsg/WelcomeMsg) fails if the target is unreachable
- Heartbeat (UDP) fails if the target is behind NAT without port forwarding

This is the classic TCP/UDP NAT problem — identical to what WebRTC, libp2p, and Tox solve.

### 2.2 Required Primitives

```
┌─────────────────────────────────────────────────────┐
│                  NAT Traversal Layer                │
├─────────────┬──────────────┬───────────┬────────────┤
│  STUN       │   TURN/Relay │  ICE      │  Hole Punch │
│  (RFC 5389) │  (RFC 5766)  │  (RFC     │  (TCP+UDP)  │
│             │              │  8445)    │             │
└─────────────┴──────────────┴───────────┴────────────┘
```

| Primitive | Purpose | When Used |
|-----------|---------|-----------|
| **STUN** | Discover mapped address:port | On startup, and on network change |
| **UDP Hole Punch** | Establish direct NAT-to-NAT UDP | For heartbeat & gossip between NAT nodes |
| **TCP Hole Punch** | Establish direct NAT-to-NAT TCP | For session data transfer |
| **TURN/Relay** | Fallback when hole punch fails | Symmetric NAT, corporate firewalls |
| **ICE** | Candidate gathering + connectivity checks | Orchestrate the above in order |

### 2.3 Architecture Decision: Embedded vs Library

| Approach | Pro | Con |
|----------|-----|-----|
| **Embedded** (custom STUN/TURN/ICE) | Zero dependency, full control | ~3–4 months engineering |
| **libjuice** (UDP hole punch only) | Lightweight, 1 file, MIT | No TURN, no ICE |
| **libp2p** (full stack) | Production-grade NAT traversal | Heavy dependency, Go/C++ interop |
| **Pion** (TURN/ICE in Go) | Mature, WebRTC-compatible | Language mismatch |

**Recommendation:** Start with **embedded STUN client + UDP hole punch** (simplest path for heartbeat/discovery). Add TURN relay only if hole punch fails in testing. ICE can be added incrementally in v0.0.4.

The mesh already has a UDP discovery port — the hole punch mechanism plugs directly into the existing `DiscoveryEngine` and `HeartbeatService`.

### 2.4 STUN Integration Points

```
smo-node startup
    │
    ▼
┌──────────────────────────┐
│ 1. Contact STUN server   │ ← Built-in or configurable (e.g. stun.l.google.com:19302)
│ 2. Get mapped addr:port  │
│ 3. Store as secondary    │
│    listen address         │
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│ 4. Advertise BOTH addrs  │ ← public:7777 AND mapped:54321
│    in membership table   │    (for nodes behind NAT)
└──────────┬───────────────┘
           ▼
┌──────────────────────────┐
│ 5. ICE candidate exchange│ ← During Join / Bootstrap sync
│    (relay addrs via      │
│     existing CBOR proto) │
└──────────────────────────┘
```

### 2.5 Hole Punch Sequence (UDP)

```
Node A (NAT)          Node B (NAT)          STUN Server
    │                      │                     │
    │── STUN Bind ────────→│←──── STUN Bind ─────│
    │←── Mapped A:1111 ────│── Mapped B:2222 →──│
    │                      │                     │
    │── Punch A:1111 ─────→│ (Node B sees packet │
    │  to B:2222           │  from A:1111,       │
    │                      │  opens NAT hole)    │
    │←── Punch B:2222 ─────│ (Node A sees packet │
    │  to A:1111           │  from B:2222,       │
    │                      │  opens NAT hole)    │
    │                      │                     │
    │══════ Direct UDP ═════│ (bidirectional)     │
    │  heartbeat + gossip   │                     │
```

### 2.6 Relay Fallback

When hole punch fails (both sides are symmetric NAT), traffic MUST fall back to a relay node. The relay is any node with a public IP that volunteers as a TURN-like forwarder.

```
┌──────────┐    ┌──────────┐    ┌──────────┐
│  Node A  │    │ Node R   │    │  Node B  │
│ (NAT)    │    │ (Public) │    │ (NAT)    │
└────┬─────┘    └────┬─────┘    └────┬─────┘
     │─── TCP ──────→│←─── TCP ───────│
     │               │               │
     │── data ──────→│── data ──────→│
     │←──── data ────│←──── data ────│
     │               │               │
```

Relay MUST be:
- Bandwidth-limited per peer (configurable, default 1 Mbps)
- Opt-in (node sets `relay: true` in its membership record)
- Detected automatically (no manual relay config)

---

## 3. Sprint Plan — 10 Epics

### N1 — STUN Client & Mapped Address Discovery

*New: no existing STUN support*

Implement a minimal STUN client (RFC 5389) that runs at node startup.

**Tasks:**
- [ ] N1.1 Implement STUN binding request/response (RFC 5389 §6)
- [ ] N1.2 Integrate into daemon startup — run before joining mesh
- [ ] N1.3 Store mapped address alongside physical address in membership table
- [ ] N1.4 Configurable STUN server (default: `stun.l.google.com:19302`)
- [ ] N1.5 Retry on failure (3 attempts, 2s timeout)
- [ ] N1.6 Add `PCT-024: STUN binding` test
- [ ] N1.7 Add STUN metric: `smo_stun_latency_seconds`

---

### N2 — UDP Hole Punch for Heartbeat & Gossip

*New: no hole punch exists*

After STUN, each node has its mapped address. The hole punch sequence runs during join/gossip setup.

**Tasks:**
- [ ] N2.1 Implement UDP hole punch protocol (predictable port pairs)
- [ ] N2.2 Wire into HeartbeatService — try direct UDP first, fall back to relay
- [ ] N2.3 Wire into GossipEngine — fanout to both physical + mapped addresses
- [ ] N2.4 Add `smo_hole_punch_success_total` / `smo_hole_punch_failure_total` metrics
- [ ] N2.5 Integration test: 2 NAT nodes → UDP hole punch → heartbeat ←→ OK
- [ ] N2.6 Add `PCT-025: UDP hole punch` test

---

### N3 — Relay Service (TURN-Lite)

*New: no relay exists*

When hole punch fails, traffic MUST fall back to a relay node.

**Tasks:**
- [ ] N3.1 Add `RelayService` — allocates per-peer relay sessions
- [ ] N3.2 Relay protocol: forward encrypted frames (no decrypt), preserve AEAD
- [ ] N3.3 Auto-detect relay candidates: nodes with `relay: true` capability
- [ ] N3.4 Bandwidth budget: 1 Mbps per peer, configurable
- [ ] N3.5 Add `smo_relay_bytes_total` / `smo_relay_active_peers` metrics
- [ ] N3.6 Integration test: 2 symmetric NAT nodes → relay → gossip ←→ OK

---

### N4 — ICE-Lite: Candidate Gathering & Connectivity Checks

*New: no ICE exists*

Not full ICE (RFC 8445) — a simplified 3-step: STUN addr → direct addr → relay.

**Tasks:**
- [ ] N4.1 Define `Candidate` struct: (type, addr, port, priority, foundation)
- [ ] N4.2 Candidate gathering: host addr → STUN mapped → relay (if available)
- [ ] N4.3 Exchange candidates via existing CBOR protocol during Join
- [ ] N4.4 Connectivity checks: STUN-style binding requests between candidates
- [ ] N4.5 Nominate best candidate pair (lowest RTT wins)
- [ ] N4.6 Add `PCT-026: ICE candidate exchange` test

---

### N5 — 3-Node WAN Test Suite (1 Public, 2 NAT)

*New: systematic WAN testing*

A test topology that mirrors the user's scenario:

```
Node A: public IP (cloud VM)
Node B: NAT #1 (local machine behind CGNAT)
Node C: NAT #2 (local machine behind different CGNAT)
```

**Tasks:**
- [ ] N5.1 Define WAN test topology (docker-compose + iptables NAT simulation)
- [ ] N5.2 Simulate NAT: `iptables -t nat -A POSTROUTING -o eth0 -j MASQUERADE`
- [ ] N5.3 Simulate CGNAT: no port forwarding, no UPnP
- [ ] N5.4 Test case: B & C join via A (outbound TCP) → OK
- [ ] N5.5 Test case: B ↔ C heartbeat via hole punch → PASS / FAIL log
- [ ] N5.6 Test case: B ↔ C heartbeat via relay fallback → OK
- [ ] N5.7 Test case: A crash → B, C detect via heartbeat timeout → DEGRADED
- [ ] N5.8 Test case: A restart → B, C re-establish → state sync via anti-entropy
- [ ] N5.9 Add `smo_nat_test_status` metric (0 = unknown, 1 = direct, 2 = relay, 3 = blocked)
- [ ] N5.10 WAN test suite in CI (weekly, not per-PR — too slow)

---

### N6 — SMIR Compiler Pipeline

*Postponed from v0.0.2 (P12)*

Wire the existing SMIR stages (lexer → parser → SSA → codegen) into a functioning compiler.

**Tasks:**
- [ ] N6.1 Complete SMIR lexer — tokenize contract DSL
- [ ] N6.2 Complete SMIR parser — AST construction
- [ ] N6.3 Complete SMIR SSA pass — IR lowering
- [ ] N6.4 Complete SMIR codegen — emit native contract bytecode
- [ ] N6.5 Wire `smo-cli compile <file.smir>` command
- [ ] N6.6 Wire `smo-node deploy <compiled.smo>` command
- [ ] N6.7 Add `PCT-027: SMIR compile → deploy → exec` E2E test

---

### N7 — WAN Failure Model

*New: systematic internet failure testing*

The failure model from v0.0.2 assumed LAN. v0.0.3 must handle internet-grade failures.

| Failure | v0.0.2 (LAN) | v0.0.3 (WAN) |
|---------|--------------|---------------|
| Packet loss | < 0.1% | Up to 5% |
| Latency | < 5ms | Up to 500ms |
| Jitter | < 1ms | Up to 100ms |
| Bandwidth | 1 Gbps | As low as 1 Mbps |
| NAT binding timeout | N/A | 30–300s (UDP) |
| IP change | Never | On reconnect |

**Tasks:**
- [ ] N7.1 Add `tc` (traffic control) test harness: loss, latency, jitter
- [ ] N7.2 Test: 5% loss → gossip still converges within 3 cycles
- [ ] N7.3 Test: 300ms latency → join timeout handling
- [ ] N7.4 Test: 100ms jitter → heartbeat misdetection false positive rate
- [ ] N7.5 Test: UDP binding timeout → re-STUN and hole punch
- [ ] N7.6 Test: IP change → node re-discovery within 60s
- [ ] N7.7 Document WAN tuning parameters in `MeshConfig`

---

### N8 — Performance Optimization

*New: systematic benchmarking*

**Tasks:**
- [ ] N8.1 Benchmark: max TCP sessions per node (target: 5000)
- [ ] N8.2 Benchmark: max UDP heartbeat targets (target: 2000)
- [ ] N8.3 Benchmark: gossip fanout throughput (target: 1000 msg/s)
- [ ] N8.4 Benchmark: anti-entropy tree sync for 10k nodes (target: < 30s)
- [ ] N8.5 Add `smo_benchmark` target to CMake (not run in CI)
- [ ] N8.6 Profile and fix top 3 CPU/memory bottlenecks

---

### N9 — Release v0.0.3

**Tasks:**
- [ ] N9.1 Bump version to `0.0.3`
- [ ] N9.2 Run WAN test suite (weekly CI)
- [ ] N9.3 Run full ctest + E2E smoke
- [ ] N9.4 `git tag v0.0.3 && git push origin v0.0.3`
- [ ] N9.5 Draft release notes with NAT traversal architecture

---

### N10 — v0.0.4 Preview: Advanced Runtime

*Planning only — no implementation*

Collect requirements for v0.0.4:

- Full ICE (RFC 8445) with TURN
- Mesh federation (cross-mesh routing)
- Smart contract SDK (SMIR → WASM)
- Performance benchmarks as CI gates

---

## 4. Effort Estimate

| Epic | Effort | Dependencies | Priority |
|------|--------|-------------|----------|
| N1 STUN Client | 3 days | none | High |
| N2 UDP Hole Punch | 5 days | N1 | High |
| N3 Relay Service | 5 days | none | High |
| N4 ICE-Lite | 4 days | N1, N3 | Medium |
| N5 WAN Test Suite | 5 days | N1–N4 | High |
| N6 SMIR Compiler | 8 days | none | Medium |
| N7 WAN Failure Model | 4 days | N5 | Medium |
| N8 Performance | 5 days | none | Low |
| N9 Release | 1 day | N1–N8 | — |
| N10 v0.0.4 Preview | 1 day | none | Low |

**Total (v0.0.3):** ~41 engineering days.

---

## 5. 3-Node WAN Test Scenario (Detailed)

This is the concrete test plan for the user's scenario: 1 public node, 2 NAT nodes.

### Topology

```
┌────────────────┐     Internet      ┌────────────────┐
│  Node B (NAT)  │                   │  Node A (Pub)  │
│  10.0.0.2:7777 │◄────────────────►│  1.2.3.4:7777  │
│  UDP mapped:   │                   │  (Authority)   │
│  5.6.7.8:11111 │                   │  relay: true   │
└───────┬────────┘                   └───────┬────────┘
        │                                    │
        │         (both behind NAT,          │
        │          no direct path)            │
        │                                    │
        ▼                                    ▼
┌────────────────┐                   ┌────────────────┐
│  Node C (NAT)  │                   │  STUN Server   │
│  10.0.0.3:7777 │                   │  stun.l.google │
│  UDP mapped:   │                   │  .com:19302    │
│  9.10.11.12    │                   └────────────────┘
│  :22222        │
└────────────────┘
```

### Flow

```
Phase 1 — Bootstrap
─────────────────────
Node A: smo-node --init --name node-a
        smo-admin genesis create --profile Enterprise --nodes 3
        smo-admin serve &                    # TCP 5454
        smo-node --daemon --mesh test-mesh   # UDP 7777 + TCP 7777

Phase 2 — STUN + Join (Node B)
────────────────────────────────
Node B: smo-node --init --name node-b
        smo-node --daemon --mesh test-mesh --seed 1.2.3.4:7777
        # Daemon starts:
        #   1. STUN → mapped 5.6.7.8:11111
        #   2. TCP outbound → 1.2.3.4:7777 → PQ handshake → JoinRequest
        #   3. Receive cert, register in membership with BOTH addrs
        #      - physical: 10.0.0.2:7777
        #      - mapped:   5.6.7.8:11111
        #   4. Bootstrap sync → get peer list (A + empty)
        #   5. Ready (DEGRADED — only A reachable)

Phase 3 — STUN + Join (Node C)
────────────────────────────────
Node C: smo-node --init --name node-c
        smo-node --daemon --mesh test-mesh --seed 1.2.3.4:7777
        # Same as B but mapped: 9.10.11.12:22222
        # After join, membership has: A(public), B(mapped), C(mapped)

Phase 4 — Hole Punch (B ↔ C)
─────────────────────────────
        # Both B and C know each other's mapped addresses
        # via membership sync from A.
        #
        # B sends UDP to 9.10.11.12:22222 (C's mapped)
        # C sends UDP to 5.6.7.8:11111 (B's mapped)
        # Both NATs open pinholes → direct UDP established
        #
        # If hole punch fails → relay via A

Phase 5 — Verification
───────────────────────
        # Check all paths:
        #   A ←→ B : direct (public ↔ NAT outbound)
        #   A ←→ C : direct (public ↔ NAT outbound)
        #   B ←→ C : hole punch OR relay
        #
        # Metrics:
        #   smo_connected_peers = 2 (each node)
        #   smo_hole_punch_success_total ≥ 1 (B and C)
        #   smo_heartbeat_rtt_seconds: B↔C via hole punch
        #
        # Kill A → B, C detect heartbeat timeout → enter DEGRADED
        # Restart A → B, C re-establish → anti-entropy converges
```

---

## 6. Success Criteria

```
☐ All N1–N9 items verified in CI (N10 planning only)
☐ STUN: binding response received, mapped address stored
☐ UDP hole punch: 2 NAT nodes establish direct heartbeat
☐ Relay fallback: symmetric NAT nodes communicate via relay
☐ ICE: candidate exchange during Join, best path nominated
☐ WAN test suite passes: 6/6 test cases
☐ 5% packet loss: anti-entropy converges within 3 cycles
☐ 300ms latency: join succeeds with extended timeout
☐ IP change: node re-discovers within 60s
☐ SMIR: compile → deploy → exec E2E
☐ Benchmarks: 5000 TCP sessions, 2000 UDP targets, 1000 gossip msg/s
☐ GitHub Actions CI green (weekly WAN suite)
```

---

## 7. Versioning Strategy

```
Release version (semver): 0.0.3  (CMakeLists.txt)
Protocol version:            1.0  (unchanged — wire format is backward-compatible)
Schema version:                2  (membership record gains `mapped_addr` + `relay` fields)
```

NAT traversal adds fields to existing CBOR structures but does not break wire compatibility — old nodes ignore unknown fields per CBOR spec.

---

## 8. Appendices

### A. STUN Message Format (RFC 5389)

```
0                   1                   2                   3
0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|0 0|     STUN Message Type     |         Message Length        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         Magic Cookie                          |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                                                               |
|                     Transaction ID (96 bits)                  |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

### B. Relay Protocol (Minimal)

```
┌────────────────┬──────────────┬──────────────────────┐
│ 4-byte length  │ 1-byte type  │ encrypted payload    │
│ (network byte  │ 0x01 = data  │ (XChaCha20-Poly1305) │
│  order)        │ 0x02 = close │                      │
├────────────────┼──────────────┼──────────────────────┤
│ 0x00000100     │ 0x01         │ <256 bytes encrypted> │
└────────────────┴──────────────┴──────────────────────┘
```

### C. References

- [RFC 5389](https://datatracker.ietf.org/doc/html/rfc5389) — STUN
- [RFC 5766](https://datatracker.ietf.org/doc/html/rfc5766) — TURN
- [RFC 8445](https://datatracker.ietf.org/doc/html/rfc8445) — ICE
- [libjuice](https://github.com/paullouisageneau/libjuice) — Lightweight UDP hole punch (MIT)
- [libp2p NAT traversal](https://docs.libp2p.io/concepts/nat/) — Reference architecture
- [DISCUSSION_0041](DISCUSSION_0041_v0.0.2_Plan.md) — v0.0.2 production readiness parent
