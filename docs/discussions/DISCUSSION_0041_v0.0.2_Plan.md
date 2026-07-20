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

---

## 2. Sprint Plan — 9 Epics

### P1 — Anti-Entropy Service (Gossip Convergence)

*RFC: DISCUSSION_0040 §10.5*

Without anti-entropy, long-term gossip convergence is not guaranteed. Nodes that partition and rejoin may never learn about stale membership, revoked certificates, or policy changes.

**Tasks:**
- [ ] P1.1 Implement `AntiEntropyService` — periodic Merkle tree comparison every 30 min with 3 random peers
- [ ] P1.2 Merkle hash over (MembershipTable + CRL entries) — use Blake3 for tree hashing
- [ ] P1.3 Wire `CAP_ANTI_ENTROPY` (bit 4, already reserved in `join_protocol.hpp:51`) into capability negotiation
- [ ] P1.4 Handle repair: request full delta from peer on hash mismatch
- [ ] P1.5 Add `PCT-018: Anti-entropy Merkle verification` test
- [ ] P1.6 Integration test: partition 2 nodes for 60s, rejoin, verify convergence

### P2 — Real Gossip Readiness (FSM GOSSIP_SYNC)

*Bug: DISCUSSION_0039 §7, auto_enroll.cpp:718-726*

The Join FSM transitions `WAIT_SYNC → GOSSIP_STARTED → GOSSIP_COMPLETE → READY` **immediately** without waiting for actual gossip I/O. The node reports READY before any network gossip has occurred.

**Tasks:**
- [ ] P2.1 Replace stub in `auto_enroll.cpp:718-726` with actual GossipEngine probe
- [ ] P2.2 Define gossip readiness condition: received ≥ 1 gossip message from ≥ 1 peer within timeout
- [ ] P2.3 Add `GOSSIP_PROBE_TIMEOUT` (configurable, default 15s) with retry to BOOTSTRAP_SYNC
- [ ] P2.4 Add `PCT-019: Gossip readiness probe` test
- [ ] P2.5 Integration test: verify FSM stays in WAIT_GOSSIP until first gossip received

### P3 — CI/CD Pipeline

*Gap: no CI/CD infra exists*

No automated build, test, lint, or release pipeline.

**Tasks:**
- [ ] P3.1 Create `.github/workflows/ci.yml` — build + test on Ubuntu 24.04 (gcc-13, clang-18)
- [ ] P3.2 Add `WITH_PQC=ON` and `WITH_PQC=OFF` matrix builds
- [ ] P3.3 Run full ctest suite + E2E smoke test on every PR
- [ ] P3.4 Add `clang-tidy` static analysis
- [ ] P3.5 Add `codespell` for doc typos
- [ ] P3.6 Add Doxygen doc generation check

### P4 — Install Targets & Packaging

*Gap: no `install()` in CMake, no CPack*

Currently only build-tree artifacts exist. No `make install`, no system-wide deployment.

**Tasks:**
- [ ] P4.1 Add `install(TARGETS ...)` for smo-core, smo-protocol, smo-transport static libs
- [ ] P4.2 Add `install(TARGETS ...)` for smo-node, smo-cli, smo-admin binaries
- [ ] P4.3 Add `install(DIRECTORY ...)` for public headers (include/smo/)
- [ ] P4.4 Add CPack config for `.deb` packaging
- [ ] P4.5 Add `find_package(smo)` config file generation
- [ ] P4.6 Verify `make install` + `ctest` on clean system

### P5 — Policy Store Integration

*Gap: storage/CMakeLists.txt:14 — policy_store commented out*

The `policy_store` subdirectory has an SqliteStore API mismatch that prevents it from being built. Policy deltas are currently stubs returning `{}`.

**Tasks:**
- [ ] P5.1 Audit `storage/policy_store` API vs current `SqliteStore` interface
- [ ] P5.2 Fix API mismatch (likely column schema or query interface)
- [ ] P5.3 Re-enable `add_subdirectory(policy_store)` in `storage/CMakeLists.txt`
- [ ] P5.4 Wire policy store into `SyncService` policy delta handler
- [ ] P5.5 Remove policy delta stub in `smo-node` daemon (lines 1057-1065)
- [ ] P5.6 Add `PCT-020: Policy store CRUD` test

### P6 — Nonce Dedup & JOIN_REQUEST Anti-Replay

*Gap: no nonce dedup in production — DISCUSSION_0040 §12*

JOIN_REQUEST replay is possible within timestamp TTL.

**Tasks:**
- [ ] P6.1 Add nonce cache to `join_protocol.cpp:process_join_request()`
- [ ] P6.2 Use Blake3 hash of (nonce + client_id) as dedup key
- [ ] P6.3 TTL-based eviction (default 60s, configurable)
- [ ] P6.4 Return error code 219 on replay detection
- [ ] P6.5 Add `PCT-021: JOIN_REQUEST nonce dedup` test

### P7 — NAT Traversal for Discovery

*Gap: discovery only works on same subnet/LAN — DISCUSSION_0040 §12*

The current UDP discovery implementation has no STUN/ICE/hole-punching. Cross-subnet mesh formation is impossible without relay.

**Tasks:**
- [ ] P7.1 Implement STUN-like endpoint discovery (`core/network/udp/stun.hpp`)
- [ ] P7.2 Add `connectivity/` layer with session-mode hole-punching
- [ ] P7.3 Add relay fallback in GossipEngine for nodes behind symmetric NAT
- [ ] P7.4 Add `PCT-022: STUN endpoint discovery` test

### P8 — Production Hardening

*Basket of quality items from DISCUSSION_0040 §12*

**Tasks:**
- [ ] P8.1 Audit history queries: implement `StorageService::query_audit()` with time-range + node filter
- [ ] P8.2 HTTP enroll server cleanup: remove or isolate legacy `enroll_server` path in `smo-admin`
- [ ] P8.3 Single Authority → HA enrollment: support 2+ authority nodes with leader election
- [ ] P8.4 Compiler pipeline: wire SMIR stages for custom contract definitions
- [ ] P8.5 Graceful shutdown: verify all timers/fds are cleaned in `smo-node` signal handler

### P9 — Release v0.0.2

**Tasks:**
- [ ] P9.1 Bump version in root `CMakeLists.txt` to 3.1.0
- [ ] P9.2 Update feature status table in README.md
- [ ] P9.3 Run full 19+ test suite + E2E smoke
- [ ] P9.4 `git tag v0.0.2 && git push origin v0.0.2`
- [ ] P9.5 Draft GitHub release notes with changelog

---

## 3. Effort Estimate

| Item | Effort | Dependencies | Priority |
|------|--------|-------------|----------|
| P1 Anti-Entropy | 5 days | none | Medium |
| P2 Gossip Readiness | 2 days | P1 (partial) | High |
| P3 CI/CD | 2 days | none | High |
| P4 Packaging | 2 days | none | High |
| P5 Policy Store | 2 days | none | Medium |
| P6 Nonce Dedup | 1 day | none | Medium |
| P7 NAT Traversal | 5 days | P1 | Low |
| P8 Hardening | 3 days | P5 | Medium |
| P9 Release | 1 day | P1–P8 | — |

**Total:** ~23 engineering days.

---

## 4. Open Questions

| # | Question | Proposed Answer |
|---|----------|----------------|
| Q1 | Should Anti-Entropy be its own service or part of GossipEngine? | Separate `AntiEntropyService` with reference to `GossipEngine` and `MembershipTable`. Cleaner separation. |
| Q2 | What Merkle bucket size? | 256 entries per bucket. For 10K nodes → ~40 buckets. Buckets hashed independently, root = Blake3(all bucket hashes). |
| Q3 | CPack generator for packaging? | Start with `.deb` (Ubuntu 24.04), add `.tgz` for portable binary. RPM deferred. |
| Q4 | Policy store DB schema — reuse SqliteStore or custom? | Reuse `SqliteStore` interface. The mismatch is column naming — align with `Policy` struct fields. |
| Q5 | NAT traversal strategy — STUN-only or ICE? | STUN for initial hole-punch, ICE for session continuity. Deploy relay nodes as a separate capability. |
| Q6 | CI runner — self-hosted or GitHub-hosted? | GitHub-hosted (ubuntu-24.04) for OSS. Self-hosted for private builds. |

---

## 5. Success Criteria

```
☐ All P1–P8 items verified in CI
☐ Test suite ≥ 25 tests (19 existing + 6 new PCTs + integration)
☐ E2E smoke test passes with 0 failures
☐ `make install` produces relocatable system install
☐ `.deb` package installs and runs on clean Ubuntu 24.04
☐ Gossip FSM waits for actual network I/O before READY
☐ Anti-entropy service converges 2 partitioned nodes within 2 cycles
☐ GitHub Actions CI green on every PR
```

---

## 6. References

- [DISCUSSION_0040 §10.5](DISCUSSION_0040_Release_0.0.1_Plan.md#105-anti-entropy-service-post-001) — Anti-entropy design
- [DISCUSSION_0040 §12](DISCUSSION_0040_Release_0.0.1_Plan.md#12-known-limitations-in-001) — Known limitations
- [DISCUSSION_0039 §7](DISCUSSION_0039_Mesh_Lifecycle_Complete.md#7-remaining-gaps-phase-8) — Gossip readiness gap
- [RFC 0024](../RFC/0024-crypto-suite-freeze.md) — Crypto suite specification
- [RFC 0034](../RFC/0034-bootstrap-snapshot.md) — Bootstrap protocol
- [SPEC.md §IX](../SPEC.md) — State machine definitions
