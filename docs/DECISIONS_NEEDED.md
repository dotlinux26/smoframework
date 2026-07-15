# Finalized Decisions — Stage 0 Freeze

Status: **ALL FROZEN** — 2026-07-15

## A. Architectural Gaps — 7/7 ACCEPTED

| # | Gap | Decision | Note |
|---|---|---|---|
| A1 | Storage backend | ✅ **SQLite3** (WAL, ACID, C API trực tiếp, không ORM) | |
| A2 | Async I/O model | ✅ **epoll-first**, io_uring là optional backend | Kernel compatibility |
| A3 | Thread safety | ✅ **Reactor + Worker Pool + SPSC Queue** | |
| A4 | Config format | ✅ **TOML** | |
| A5 | Logging API | ✅ **Logger Interface** + spdlog backend | Runtime không biết spdlog |
| A6 | Crypto abstraction | ✅ **Suite Registry** + SuiteID | Không virtual dispatch, registry tra |
| A7 | Error hierarchy | ✅ **Result<T> + 13 categories** | |

## B. Engineering RFCs — 10/10 ACCEPTED

| # | RFC | Status |
|---|---|---|
| B1 | 0008 — Error Model | ✅ ACCEPTED |
| B2 | 0009 — Crypto Provider | ✅ ACCEPTED |
| B3 | 0010 — Storage Backend | ✅ ACCEPTED |
| B4 | 0011 — Thread & Async Model | ✅ ACCEPTED |
| B5 | 0012 — Identity & Certificate API | ✅ ACCEPTED |
| B6 | 0013 — Transport Abstraction | ✅ ACCEPTED |
| B7 | 0014 — Session Lifecycle | ✅ ACCEPTED |
| B8 | 0015 — Discovery Engine | ✅ ACCEPTED |
| B9 | 0016 — Governance Protocol | ✅ ACCEPTED |
| B10 | 0017 — Trust Engine | ✅ ACCEPTED |

## C. Additional RFCs Required — 5 mới

| # | RFC | Subject |
|---|---|---|
| C1 | 0018 | Mesh Manifest (riêng, không gộp Identity) |
| C2 | 0019 | Packet Layout (freeze wire format) |
| C3 | 0020 | Opcode Registry (namespace chuẩn) |
| C4 | 0021 | FSM Rules (tách riêng, linh hồn runtime) |
| C5 | 0022 | Storage Schema (SQLite schema, version, migration) |
| C6 | 0023 | **Contract Architecture (v2)** — Intent/Contract separation, 3 categories, Contract Registry, Opcode Registry, Compiler boundary, DAG cache |

## D. Stage 1 Implementation Order

```
 1. Error Model
 2. Crypto Provider
 3. Storage (SQLite)
 4. Identity
 5. Certificate
 6. FSM
 7. Transport
 8. Session
 9. Discovery
10. Governance
11. Trust
```
