# DISCUSSION 0041 — Release 0.0.2: From Developer Preview to Production Readiness

**Status:** Planning  
**Target:** v0.0.2 (production-ready)  
**Parent:** v0.0.1-rc — 19/19 tests, E2E smoke pass  
**RFCs:** 0001–0044  

---

## 1. Motivation

v0.0.1-rc delivered a protocol-complete developer preview:

- Protocol freeze with 6 architecture review fixes
- 17 protocol compliance tests (PCT-001–017)
- 19/19 ctest suite + 8/8 E2E smoke checks
- Full crypto suite support (Classical, Modern, PurePQC)
- Native contracts: Join, Bootstrap, Governance, Recovery, File, Process

**v0.0.2 bridges the gap from developer preview to production-ready mesh runtime.** Every item below addresses a concrete limitation documented in §12 of DISCUSSION_0040.

This roadmap is **production-driven, not feature-driven**. All epics focus on convergence, consistency, observability, replay protection, packaging, CI, and HA — the properties a real distributed system needs before new features.

---

## 2. Sprint Plan — 12 Epics

### P1 — Anti-Entropy Service (Gossip Convergence)

*RFC: DISCUSSION_0040 §10.5*

Without anti-entropy, long-term gossip convergence is not guaranteed:

```
partition → heal → gossip packet loss → state forever inconsistent
```

**Design:** 4 independent Merkle trees, each carrying a **version vector** for causal ordering:

| Tree | Scope | Bucket Size | Version Vector |
|------|-------|-------------|----------------|
| Membership | Peer records (node_id, role, state, epoch) | 256 entries | Per-node dot |
| CRL | Revoked certificate fingerprints | 256 entries | Per-authority dot |
| Policy | Active policy rules + version | 64 entries | Per-policy dot |
| Contract Registry | Known contract IDs + versions | 64 entries | Per-contract dot |

Each tree tracks:

```
tree_state = {
    epoch:          uint64           // monotonic counter
    version_vector: map[node_id → epoch]  // causal history
    merkle_root:    bytes[32]        // Blake3 of all buckets
}
```

On sync, peers exchange `(epoch, version_vector, merkle_root)`. If roots differ, the version vector identifies which peer is behind in which dimension. Only the divergent tree's delta is requested — not the entire world state.

**Bandwidth budget:** If a delta exceeds 500 entries, fall back to full snapshot sync instead.

**Tasks:**
- [ ] P1.1 Implement `AntiEntropyService` — periodic tree exchange every 30 min with 3 random peers
- [ ] P1.2 4 independent Merkle trees with version vectors: Membership, CRL, Policy, Contract Registry
- [ ] P1.3 Wire `CAP_ANTI_ENTROPY` (bit 4, reserved in `join_protocol.hpp:51`) into capability negotiation
- [ ] P1.4 Handle repair: request only divergent tree's delta; if delta > 500 entries, use snapshot sync
- [ ] P1.5 Add `PCT-018: Anti-entropy Merkle + version vector` test
- [ ] P1.6 Integration test: partition 2 nodes for 60s, rejoin, verify all 4 trees converge within 2 cycles

---

### P2 — Real Gossip Readiness (FSM GOSSIP_SYNC)

*Bug: DISCUSSION_0039 §7, auto_enroll.cpp:718-726*

The Join FSM transitions `WAIT_SYNC → GOSSIP_STARTED → GOSSIP_COMPLETE → READY` **immediately** without waiting for actual gossip I/O. The node reports READY before any network gossip has occurred.

**Design:** Node is READY only when all 6 conditions are met:

```
certificate_valid          // crypto chain verified
AND bootstrap_complete     // BOOTSTRAP_SYNC response received
AND sync_state == CONSISTENT  // all deltas applied (membership, CRL, policy, contracts)
AND heartbeat_active       // received ≥ 1 heartbeat from ≥ 1 peer
AND gossip_rx              // received ≥ 1 gossip message
AND gossip_tx              // sent ≥ 1 gossip message
```

**DEGRADED state:** If a node passes bootstrap but cannot meet the network-readiness conditions (no peers online), it enters `DEGRADED` instead of `FAILED`. A `DEGRADED` node retries gossip probes periodically and transitions to `READY` once all conditions are met.

```
WAIT_GOSSIP
    ↓ [timeout, no peers]
  DEGRADED
    ↓ [conditions met]
  READY
```

**Tasks:**
- [ ] P2.1 Replace stub in `auto_enroll.cpp:718-726` with actual GossipEngine + HeartbeatService probe
- [ ] P2.2 Implement 6-condition readiness: cert, bootstrap, sync, heartbeat, rx, tx
- [ ] P2.3 Add `DEGRADED` FSM state with periodic retry (configurable, default 30s)
- [ ] P2.4 Add `GOSSIP_PROBE_TIMEOUT` (configurable, default 15s) with retry to BOOTSTRAP_SYNC
- [ ] P2.5 Add `PCT-019: Gossip readiness + DEGRADED state` test
- [ ] P2.6 Integration test: verify FSM stays in WAIT_GOSSIP/DEGRADED until all 6 conditions met

---

### P3 — CI/CD + Metrics Pipeline

*Gap: no CI/CD infra, no metrics endpoint*

#### CI/CD

No automated build, test, sanitizer, or release pipeline. For a C++ project handling untrusted network input, sanitizers are non-negotiable.

**Sanitizer matrix:**

| Config | ASAN | UBSAN | TSAN |
|--------|------|-------|------|
| Debug | ✅ | ✅ | — |
| Release | — | — | — |
| ASAN | ✅ | ✅ | — |
| TSAN | — | — | ✅ |

#### Metrics (Prometheus `/metrics`)

CI should verify the metrics endpoint from day one. Expose at minimum:

| Metric | Type | Source |
|--------|------|--------|
| `smo_join_latency_seconds` | Histogram | Join FSM |
| `smo_bootstrap_seconds` | Histogram | Bootstrap FSM |
| `smo_gossip_queue_depth` | Gauge | GossipEngine |
| `smo_gossip_fanout_count` | Histogram | GossipEngine |
| `smo_heartbeat_rtt_seconds` | Histogram | HeartbeatService |
| `smo_connected_peers` | Gauge | MembershipTable |
| `smo_anti_entropy_repairs_total` | Counter | AntiEntropyService |
| `smo_membership_epoch` | Gauge | Membership tree |
| `smo_contract_epoch` | Gauge | Contract tree |
| `smo_policy_epoch` | Gauge | Policy tree |
| `smo_crl_epoch` | Gauge | CRL tree |

**Tasks:**
- [ ] P3.1 Create `.github/workflows/ci.yml` — 4 configs × 2 PQC options = 8 jobs
- [ ] P3.2 Add `WITH_PQC=ON` and `WITH_PQC=OFF` matrix builds
- [ ] P3.3 Run full ctest suite + E2E smoke test on every PR
- [ ] P3.4 Add `clang-tidy` static analysis
- [ ] P3.5 Add `codespell` for doc typos
- [ ] P3.6 Add Doxygen doc generation check
- [ ] P3.7 Implement `MetricsService` with Prometheus text format (`/metrics` endpoint)
- [ ] P3.8 Instrument Join FSM, Bootstrap FSM, GossipEngine, HeartbeatService, MembershipTable
- [ ] P3.9 CI verifies `/metrics` returns ≥ 8 metrics with expected types

---

### P4 — Install Targets & Packaging

*Gap: no `install()` in CMake, no CPack*

Currently only build-tree artifacts exist. No `make install`, no `find_package(SMO)`, no `.deb`.

**Tasks:**
- [ ] P4.1 Add `install(TARGETS ...)` for smo-core, smo-protocol, smo-transport static libs
- [ ] P4.2 Add `install(TARGETS ...)` for smo-node, smo-cli, smo-admin binaries
- [ ] P4.3 Add `install(DIRECTORY ...)` for public headers (`include/smo/`)
- [ ] P4.4 Add `install(EXPORT SMO)` so external projects can `find_package(SMO)`
- [ ] P4.5 Add CPack config for `.deb` packaging
- [ ] P4.6 Verify `make install` + `find_package(SMO)` on clean Ubuntu 24.04

---

### P5 — Policy Store Integration

*Gap: storage/CMakeLists.txt:14 — policy_store commented out*

The `policy_store` subdirectory has an SqliteStore API mismatch. Policy deltas are currently stubs returning `{}`.

**Tasks:**
- [ ] P5.1 Audit `storage/policy_store` API vs current `SqliteStore` interface
- [ ] P5.2 Fix API mismatch (column naming → align with `Policy` struct fields)
- [ ] P5.3 Re-enable `add_subdirectory(policy_store)` in `storage/CMakeLists.txt`
- [ ] P5.4 Wire policy store into `SyncService` policy delta handler
- [ ] P5.5 Remove policy delta stub in `smo-node` daemon (lines 1057-1065)
- [ ] P5.6 Add `PCT-020: Policy store CRUD` test

---

### P6 — Nonce Dedup & JOIN_REQUEST Anti-Replay

*Gap: no nonce dedup in production — DISCUSSION_0040 §12*

JOIN_REQUEST replay is possible within timestamp TTL.

**Design:** Nonce cache key = `Blake3(mesh_id || node_id || nonce)`. Mesh ID is included so that an Authority managing multiple meshes does not suffer cross-mesh collisions.

Nonce TTL = token expiry. An invite token valid for 5 minutes means the nonce cache TTL is also 5 minutes — no need to maintain two independent timers.

**Tasks:**
- [ ] P6.1 Add nonce cache to `join_protocol.cpp:process_join_request()` — keyed by Blake3(mesh_id || node_id || nonce)
- [ ] P6.2 Nonce TTL = `token.expiry_unix_sec` instead of hardcoded value
- [ ] P6.3 Return error code 219 on replay detection
- [ ] P6.4 Add `PCT-021: JOIN_REQUEST nonce dedup` test

---

### ~~P7 — NAT Traversal for Discovery~~ *(Postponed to v0.0.3)*

*Gap: discovery only works on same subnet/LAN — DISCUSSION_0040 §12*

STUN, ICE, hole-punching, relay — this is an entire connectivity subsystem. Forcing it into v0.0.2 would delay the release.

**Decision:** v0.0.2 targets production in LAN/VPN environments. Internet mesh is deferred to **v0.0.3** or **v0.1.0**.

*Cross-reference: [DISCUSSION_0040 §12](DISCUSSION_0040_Release_0.0.1_Plan.md#12-known-limitations-in-001)*

---

### P8 — Production Hardening

*Basket of quality items from DISCUSSION_0040 §12*

**Tasks:**
- [ ] P8.1 Audit history queries: implement `StorageService::query_audit()` with time-range + node filter
- [ ] P8.2 HTTP enroll server cleanup: remove or isolate legacy `enroll_server` path in `smo-admin`
- [ ] P8.3 Authority HA: support 2+ authority nodes — the chosen replication MUST provide linearizable writes, quorum reads, leader election, and log replication
- [ ] P8.4 Compiler pipeline: wire SMIR stages for custom contract definitions
- [ ] P8.5 Graceful shutdown: verify all timers/fds cleaned in `smo-node` signal handler

---

### P9 — Release v0.0.2

**Tasks:**
- [ ] P9.1 Bump version in root `CMakeLists.txt` to `0.0.2` (semver; internal protocol version tracked separately in `protocol/schema/schema.h`)
- [ ] P9.2 Update feature status table in README.md
- [ ] P9.3 Run full test suite + E2E smoke
- [ ] P9.4 `git tag v0.0.2 && git push origin v0.0.2`
- [ ] P9.5 Draft GitHub release notes with changelog

---

### P10 — Structured Logging

*Status quo: printf + spdlog mixed, no uniform schema*

Log aggregation (ELK/Loki) is painful without a consistent schema.

**Design:**

```json
{
  "timestamp": "2026-07-20T10:30:00.123Z",
  "node_id": "a1b2c3d4...",
  "mesh_id": "mesh-smo-dev",
  "session_id": "550e8400-...",
  "component": "GossipEngine",
  "level": "info",
  "message": "fanout complete",
  "peer_count": 5,
  "rtt_ms": 42
}
```

**Tasks:**
- [ ] P10.1 Define `LogEntry` struct with all standard fields
- [ ] P10.2 Replace `printf` + raw `spdlog` calls with structured logger
- [ ] P10.3 Add JSON + plaintext formatters (configurable)
- [ ] P10.4 Node identity auto-injected into every log line
- [ ] P10.5 Add `PCT-022: Structured log format` test

---

### P11 — Config Versioning & Migration

*Gap: config evolves without schema version*

No `config_version` or `schema_version` means breaking changes cannot be detected or migrated.

**Tasks:**
- [ ] P11.1 Add `config_version` and `schema_version` fields to `MeshConfig`
- [ ] P11.2 Implement migration path: `schema_version < current` → auto-upgrade on load
- [ ] P11.3 Add `PCT-023: Config version migration` test

---

## 3. Effort Estimate

| Epic | Effort | Dependencies | Priority |
|------|--------|-------------|----------|
| P1 Anti-Entropy | 6 days | none | Medium |
| P2 Gossip Readiness | 2 days | P1 (partial) | High |
| P3 CI/CD + Metrics | 4 days | none | High |
| P4 Packaging | 2 days | none | High |
| P5 Policy Store | 3 days | none | Medium |
| P6 Nonce Dedup | 1 day | none | Medium |
| ~~P7 NAT Traversal~~ | ~~postponed~~ | — | — |
| P8 Hardening | 5 days | P5 | Medium |
| P9 Release | 1 day | P1–P8 | — |
| P10 Structured Logging | 3 days | none | Medium |
| P11 Config Versioning | 2 days | none | Low |

**Total (v0.0.2):** ~29 engineering days.

---

## 4. Open Questions

| # | Question | Proposed Answer |
|---|----------|----------------|
| Q1 | Anti-Entropy — own service or part of GossipEngine? | Separate `AntiEntropyService` holding reference to `GossipEngine` + `MembershipTable`. Cleaner separation of concerns. |
| Q2 | Version vector format? | Dot per node: `map[node_id → epoch]`. Compact: only include nodes that have diverged from peer during exchange. |
| Q3 | CPack generator? | Start with `.deb` (Ubuntu 24.04), add `.tgz` portable binary. RPM deferred. |
| Q4 | Policy store — reuse SqliteStore or custom? | Reuse `SqliteStore`. The mismatch is column naming — align with `Policy` struct fields. |
| Q5 | Authority HA — which library? | RFC specifies **requirements** (linearizable writes, quorum, leader election, log replication). Implementation choice deferred to implementor. Candidates: etcd/raft, braft, Dragonboat. |
| Q6 | CI runner? | GitHub-hosted (ubuntu-24.04) for OSS. Self-hosted for private builds. |
| Q7 | Metrics format? | Prometheus text format. `/metrics` endpoint via embedded HTTP (no external dependency). |
| Q8 | Log format — JSON always or configurable? | Configurable. Default = JSON for production, plaintext for development. |

---

## 5. Success Criteria

```
☐ All P1–P11 items verified in CI (P7 postponed)
☐ Test suite ≥ 25 tests (19 existing + 6 new PCTs + integration)
☐ ASAN/UBSAN green on every PR
☐ E2E smoke test passes with 0 failures
☐ `make install` produces relocatable system install
☐ `find_package(SMO)` works from external project
☐ `.deb` package installs and runs on clean Ubuntu 24.04
☐ Gossip FSM waits for all 6 readiness conditions before READY
☐ DEGRADED state exists and retries gossip probes
☐ Anti-entropy converges 2 partitioned nodes within 2 cycles (4 trees, version vectors)
☐ Delta repair falls back to snapshot sync when > 500 entries
☐ Prometheus `/metrics` endpoint exposes ≥ 10 metrics with correct types
☐ Nonce dedup key includes mesh_id (cross-mesh safe)
☐ Log output conforms to structured JSON schema
☐ Config version migration works: schema_version < current → auto-upgrade
☐ GitHub Actions CI green on every PR
```

---

## 6. Failure Model

v0.0.2 does not target Byzantine fault tolerance. The assumed failure model:

| Failure | Handling | Epic |
|---------|----------|------|
| Network partition | Anti-entropy detects on heal, Merkle repair | P1 |
| Node crash + restart | FSM restarts from NEW, re-join with existing cert | P2 |
| Message loss (gossip) | Redundant fanout + periodic anti-entropy | P1 |
| Clock skew | Timestamp window (±30s) enforced at JOIN | P6 |
| Replay attack | Nonce dedup, TTL = token expiry | P6 |
| Authority crash | Single Authority = SPOF in v0.0.2; HA via Raft in P8.3 | P8 |
| Slow peer (straggler) | Anti-entropy detects version vector gap, snapshot sync | P1 |
| Split-brain (Authority) | Eliminated by Raft-based HA in P8.3 | P8 |

---

## 7. Versioning Strategy

```
Release version (semver): 0.0.2  (CMakeLists.txt)
Protocol version:            1.0  (protocol/schema/schema.h)
Schema version:                1  (per-config, tracked in MeshConfig)
```

Release version and protocol version are decoupled. Protocol v1.0 spans multiple releases; it only bumps on wire-breaking changes.

---

## 8. References

- [DISCUSSION_0040 §10.5](DISCUSSION_0040_Release_0.0.1_Plan.md#105-anti-entropy-service-post-001) — Anti-entropy design
- [DISCUSSION_0040 §12](DISCUSSION_0040_Release_0.0.1_Plan.md#12-known-limitations-in-001) — Known limitations
- [DISCUSSION_0039 §7](DISCUSSION_0039_Mesh_Lifecycle_Complete.md#7-remaining-gaps-phase-8) — Gossip readiness gap
- [RFC 0024](../RFC/0024-crypto-suite-freeze.md) — Crypto suite specification
- [RFC 0034](../RFC/0034-bootstrap-snapshot.md) — Bootstrap protocol
- [SPEC.md §IX](../SPEC.md) — State machine definitions
