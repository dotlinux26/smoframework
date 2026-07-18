# RFC — Request for Comments

Design changes follow the RFC process.

1. Create a numbered RFC document in this directory.
2. Reach consensus with stakeholders.
3. Update SPEC.md to reflect the accepted design.
4. Merge.

SPEC.md is the source of truth for implementation.
RFCs are the source of truth for design history.

---

## Index

| RFC | Title | Status | Notes |
|-----|-------|--------|-------|
| 0001 | Contract Format v2 | Superseded by 0025, 0026 | |
| 0002 | Capability System | Accepted | |
| 0003 | Witness Protocol | Accepted | |
| 0004 | Trust Model | Accepted | |
| 0005 | DAG Execution Model | Accepted | |
| 0006 | Mesh Identity & Certificate | Accepted | |
| 0007 | Enrollment Protocol | Accepted | |
| 0008 | Error Model | Accepted | |
| 0009 | Crypto Provider | Superseded by 0024 | |
| 0010 | Storage Backend | Accepted | |
| 0011 | Thread & Async Model | Accepted | |
| 0012 | Identity & Certificate API | Accepted | |
| 0013 | Transport Abstraction | Accepted | |
| 0014 | Session Lifecycle | Accepted | |
| 0015 | Discovery Engine | Accepted | MVP: UDP HELLO/PING |
| 0016 | Governance Protocol | Accepted | |
| 0017 | Trust Engine | Accepted | |
| 0018 | Mesh Manifest | Accepted | |
| 0019 | Packet Layout | Accepted | |
| 0020 | Opcode Registry | Accepted | |
| 0021 | FSM Rules | Accepted | |
| 0022 | Storage Schema | Accepted | |
| 0023 | Contract Architecture v2 | Accepted | 3-tier, polymorphic kernel |
| 0024 | Crypto Suite Freeze | Accepted | Suites 1-3 frozen |
| 0025 | Contract Runtime Architecture | Accepted | Compiler pipeline, Executor, Runtime::execute() |
| 0026 | Contract ABI Specification | Accepted | ABI Hash, Semantic Hash, version bounds |
| 0027 | Network Layer: Bootstrap/Heartbeat/Gossip/PeerStore/Sync | **Accepted** | Sprint 4 implementation |
| 0028 | Contract Runtime (revised) | Accepted | |
| 0029 | Policy Engine | Accepted | |
| 0030 | Native Contracts | Accepted | |
| 0031 | Mesh Manager | Accepted | |
| 0032 | Context CLI | Accepted | |
| 0032 | Zero-Touch Enrollment | Accepted | |
| 0033 | Mesh Genesis & Governance | **FROZEN** | PKI, governance policy, voting |
| 0034 | Bootstrap Protocol | **FROZEN** | CBOR-over-TCP bootstrap plane |
| 0035 | Runtime Architecture | **FROZEN** | RuntimeKernel, Execution Model, DAG |
| 0036 | Contract ABI Freeze | **APPROVED** | ContextValue arguments, partial-const RuntimeContext |
| 0037 | Runtime Service Injection | **APPROVED** | DI + ClockService + RandomService, no reinterpret_cast |
| 0038 | Execution State Machine | **APPROVED** | 12-state + Compensating, no TimedOut terminal |
| 0039 | NextAction Model | **APPROVED** | std::variant, ActionDispatchContract/Message, no ActionComplete |
| 0040 | Contract Lifecycle + Metadata + Capabilities | **APPROVED** | Registry/Manager split, no Executing lifecycle |