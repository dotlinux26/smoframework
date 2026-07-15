# SMO Stability & Frozen API/ABI

This document lists every component that has been **frozen** — its interface
will not change without an RFC and a migration path. Changes must preserve
backward compatibility or provide a documented deprecation window.

## How to Read

| Badge | Meaning |
|---|---|
| ✅ Frozen | API/ABI is stable. Changes require RFC + deprecation. |
| 🔶 Draft | Design accepted, implementation may change. |
| 🚧 Experimental | May change at any time. Not for production. |

---

## ✅ Crypto (RFC 0024)

| Component | Status | Notes |
|---|---|---|
| 3 Crypto Suites | ✅ Frozen | Suite 1 (Classical), Suite 2 (Modern), Suite 3 (PurePQC). No new suites in Stage 1–2. |
| HashSuite enum | ✅ Frozen | Blake3=1, Sha256=2, Sha3_256=3, XxHash3=101, Crc32C=102, CityHash=103. |
| CryptoProvider struct | ✅ Frozen | RNG + Hash + PerfHash + AEAD + KEM + Signer. |
| CryptoRegistry singleton | ✅ Frozen | register_suite(), get_suite(), available_suites(). |
| HashProvider abstract class | ✅ Frozen | hash(), hash_hex(), bytes_to_hex(), hex_to_bytes(), default_provider(), set_default_provider(). |
| HKDF (RFC 5869) | ✅ Frozen | Fixed HMAC-SHA256. Suite-independent. |
| getrandom CSPRNG | ✅ Frozen | Wraps Linux getrandom(). |
| SecureBuffer + zeroize | ✅ Frozen | RAII, copy-deleted, move-enabled, volatile memset. |
| constant_time_compare | ✅ Frozen | Timing-safe memcmp. |

## ✅ Error Model

| Component | Status | Notes |
|---|---|---|
| ErrorCode enum | ✅ Frozen | All error codes from `core/errors/error_codes.md`. |
| Error struct | ✅ Frozen | code, message, source_file, source_line, timestamp_ns. |
| Result<T> template | ✅ Frozen | .value(), .error(), operator bool(). |
| SMO_ERR_* macros | ✅ Frozen | SMO_ERR_CRYPTO, SMO_ERR_INTERNAL, etc. |

## ✅ Storage

| Component | Status | Notes |
|---|---|---|
| SqliteStore | ✅ Frozen | Init, Get, Set, Delete, Exists, schema migration. |
| SessionStore | ✅ Frozen | Schema + CRUD. |
| NodeStore | ✅ Frozen | Schema + CRUD. |
| DagStore | ✅ Frozen | Schema + CRUD. |
| AuditStore | ✅ Frozen | Schema + CRUD. |
| TrustStore | ✅ Frozen | Schema + CRUD. |
| MeshStore | ✅ Frozen | Schema + CRUD. |

## ✅ Wire Protocol

| Component | Status | Notes |
|---|---|---|
| Packet Header v1 | ✅ Frozen | 1+1+1+2+16+8+8 = 37 bytes fixed. |
| Suite ID in header | ✅ Frozen | Byte 2 of header. |
| Session ID format | ✅ Frozen | 16 bytes. |
| Timestamp format | ✅ Frozen | 8 bytes Unix ns. |
| Nonce format | ✅ Frozen | 8 bytes random. |

## ✅ Session

| Component | Status | Notes |
|---|---|---|
| Session lifecycle | ✅ Frozen | OPEN → ACTIVE → CLOSE/EXPIRE/REVOKE. |
| Signed nonce challenge | ✅ Frozen | Session binding (§16.8). |

## ✅ Certificate

| Component | Status | Notes |
|---|---|---|
| MembershipCertificate | ✅ Frozen | MeshID, Role, Capabilities, Epoch, Signature. |
| Certificate chain | ✅ Frozen | Root → Authority → Node. |
| Enrollment flow | ✅ Frozen | .smor → .smoc. |

## ✅ Contract System (RFC 0023)

| Component | Status | Notes |
|---|---|---|
| ContractID format | ✅ Frozen | 64 hex chars (256-bit hash). |
| ContractDefinition schema | ✅ Frozen | JSON fields: contract_version, category, opcode, name, publisher, semver, etc. |
| Canonical JSON serialization | ✅ Frozen | Sorted keys, no whitespace. |
| Contract Registry architecture | ✅ Frozen | Immutable, append-only, Blake3-addressed. |
| OpcodeRegistry | ✅ Frozen | Builtin ops: LS, PUT, GET, EXEC, QUARANTINE, MKDIR, RM, CP, CUSTOM. Plugin range: 0xFB–0xFE. |
| Contract Factory | ✅ Frozen | Intent → ContractID resolution. |
| Contract categories | ✅ Frozen | Native, User-defined, Internal. |
| Native contracts | ✅ Frozen | ls, put, get, exec, quarantine templates. |

## 🔶 Platform Abstraction

| Component | Status | Notes |
|---|---|---|
| platform/ layer | 🔶 Draft | Design accepted. Implementation in progress. |
| io_uring integration | 🚧 Experimental | Not yet frozen. |
| cgroup v2 integration | 🚧 Experimental | Not yet frozen. |
| Windows port | 🚧 Experimental | Not yet started. |

## 🚧 Future / Not Yet Frozen

| Component | Status | Notes |
|---|---|---|
| DAG format | 🚧 Experimental | May change with Compiler implementation. |
| Compiler API | 🚧 Experimental | Not yet frozen. |
| Executor API | 🚧 Experimental | Not yet frozen. |
| Scheduler API | 🚧 Experimental | Not yet frozen. |
| Trust engine | 🚧 Experimental | Scoring algorithm may change. |
| ACL/Permissions | 🔶 Draft | Design accepted, implementation pending. |
| Plugin ABI | 🚧 Experimental | Not yet frozen. |
| Mesh discovery | 🔶 Draft | Design accepted. |
| Witness protocol | 🚧 Experimental | Not yet frozen. |

---

## RFC Index

| RFC | Title | Status |
|---|---|---|
| 0009 | Crypto Provider | Superseded by RFC 0024 |
| 0012 | Identity & Certificate API | ✅ Frozen |
| 0018 | Mesh Manifest | ✅ Frozen |
| 0019 | Packet Layout | ✅ Frozen |
| 0023 | Contract Architecture | ✅ Frozen |
| 0024 | Crypto Suite Freeze | ✅ Frozen |
