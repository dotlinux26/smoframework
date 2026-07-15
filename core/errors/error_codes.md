# SMO Error Codes

Every module returns `Result<T, Error>`. Every `Error` carries:

| Field | Type | Purpose |
|---|---|---|
| `code.category` | `ErrorCategory` | Origin module (13 categories) |
| `code.code` | `uint16_t` | Numeric code (0-1023 per category) |
| `code.severity` | `Severity` | Debug / Info / Warn / Error / Critical / Alert |
| `code.retry` | `RetryClass` | NoRetry / RetrySafe / RetryBackoff / RetryNever |
| `code.recovery` | `Recovery` | Strategy hint for the caller |
| `message` | `string` | Human-readable description |
| `source_file` | `const char*` | Origin file (`__FILE__`) |
| `source_line` | `int` | Origin line (`__LINE__`) |
| `timestamp_ns` | `uint64_t` | Monotonic clock at error creation |

**Legend:**

- Sev: D(ebug) I(nfo) W(arn) E(rror) C(ritical) A(lert)
- Ret: N(oRetry) S(afe) B(ackoff) V(erNever)
- Rec: N(one) R(etry) C(onnect) E(nroll) F(SM) B(oot) G(overnance) M(anual)

---

## 1. CRYPTO (ErrorCategory::Crypto) — codes 0-99

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 1 | KEYGEN_FAILED | E | N | R | Keypair generation failed (RNG error, insufficient entropy) |
| 2 | SIGN_FAILED | E | N | N | Signing operation failed (internal error in crypto backend) |
| 3 | VERIFY_FAILED | E | N | N | Signature verification returned false (packet is forged or corrupted) |
| 4 | VERIFY_ERROR | E | N | N | Signature verification could not complete (bad parameters) |
| 5 | DECRYPT_FAILED | E | N | N | Symmetric decryption failed (wrong key, corrupted ciphertext) |
| 6 | ENCRYPT_FAILED | E | N | R | Symmetric encryption failed (RNG error, oversized payload) |
| 7 | HASH_FAILED | E | N | N | Hash computation failed (internal error) |
| 8 | UNSUPPORTED_SUITE | E | N | N | Crypto Suite ID is not implemented by this node |
| 9 | SUITE_NEGOTIATION_FAILED | W | B | C | No common crypto suite between two nodes |
| 10 | KEY_EXCHANGE_FAILED | E | S | C | Key exchange (ECDH/KEM) failed |
| 11 | NONCE_GENERATION_FAILED | E | S | R | Random nonce generation failed |
| 12 | SHAMIR_SPLIT_FAILED | E | S | R | Shamir secret sharing split operation failed |
| 13 | SHAMIR_COMBINE_FAILED | C | N | M | Shamir share combination failed (wrong shares, threshold not met) |
| 14 | AEAD_TAG_MISMATCH | A | N | N | AEAD authentication tag verification failed (tampered ciphertext) |
| 15 | RNG_NOT_SEEDED | C | N | B | Random number generator not properly seeded |

**Recovery notes:**
- Most crypto errors are fatal to the current operation but retryable at a higher level (new session, new key).
- `AEAD_TAG_MISMATCH` is a security alert — packet is dropped, source is penalized.
- `RNG_NOT_SEEDED` requires node daemon restart.

---

## 2. IDENTITY (ErrorCategory::Identity) — codes 100-199

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 100 | KEYPAIR_NOT_FOUND | E | N | R | No keypair exists (node not initialized) |
| 101 | KEYPAIR_ALREADY_EXISTS | W | N | N | `smo-node init` called on an already-initialized node |
| 102 | KEYPAIR_CORRUPTED | C | N | B | Keypair file is corrupted or tampered |
| 103 | KEY_ROTATION_IN_PROGRESS | I | S | N | Rotation already active; new rotation rejected |
| 104 | KEY_ROTATION_FAILED | C | N | M | Key rotation could not complete; manual recovery required |
| 105 | KEY_COMPROMISED | A | N | M | Key compromise detected (out-of-band); emergency rotation triggered |
| 106 | NODE_BUSY | W | S | N | Node is in a state that cannot accept the operation |
| 107 | LIFECYCLE_INVALID_TRANSITION | E | N | F | Requested FSM transition is not valid for current state (§7.10) |
| 108 | LIFECYCLE_TIMEOUT | W | B | F | FSM state dwell time exceeded; automatic fallback transition |
| 109 | SUSPENDED | W | N | N | Node is suspended; no new operations until unsuspended |
| 110 | RETIRED | E | N | N | Node is retired; no further operations possible |

**Recovery notes:**
- `KEYPAIR_NOT_FOUND` → re-run `smo-node init`.
- `KEYPAIR_CORRUPTED` → node daemon restart. If persistent, re-enroll with new keypair.
- `KEY_COMPROMISED` → immediate alert, emergency rotation, epoch increment.
- `LIFECYCLE_TIMEOUT` → FSM timeout handler fires fallback transition automatically.

---

## 3. CERTIFICATE (ErrorCategory::Certificate) — codes 200-299

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 200 | CERT_NOT_FOUND | E | N | E | No membership certificate found for this mesh |
| 201 | CERT_EXPIRED | E | N | E | Certificate `expires_at` has passed |
| 202 | CERT_EPOCH_MISMATCH | E | N | E | Certificate epoch < mesh current_epoch |
| 203 | CERT_BAD_CHAIN | C | N | E | Certificate chain does not verify up to Root Public Key |
| 204 | CERT_BAD_SIGNATURE | A | N | N | Certificate signature is invalid (forged or corrupted) |
| 205 | CERT_REVOKED | E | N | E | Certificate has been explicitly revoked (REVOKE_CERT) |
| 206 | CERT_SUSPENDED | W | N | N | Certificate is temporarily suspended |
| 207 | CERT_MISSING_CAPABILITY | E | N | N | Certificate does not contain the required capability |
| 208 | CERT_ROLE_NOT_AUTHORIZED | E | N | N | Certificate role does not permit this operation |
| 209 | CSR_BAD_SIGNATURE | A | N | N | Certificate Signing Request has invalid signature |
| 210 | CSR_EXPIRED | E | N | S | CSR timestamp outside acceptable window |
| 211 | ENROLL_FAILED | E | S | E | Enrollment request was rejected by Authority |
| 212 | ENROLL_TOKEN_INVALID | W | N | E | Join Token is invalid, expired, or already consumed |
| 213 | ENROLL_TOKEN_EXPIRED | W | N | S | Join Token has expired; request a new one |
| 214 | MANIFEST_BAD_SIGNATURE | C | N | M | Mesh Manifest has invalid Authority co-signatures |
| 215 | MANIFEST_THRESHOLD_NOT_MET | E | N | G | Manifest does not have enough Authority signatures |
| 216 | MANIFEST_VERSION_MISMATCH | W | N | E | Mesh Manifest protocol version does not match node version |
| 217 | CHAIN_INCOMPLETE | E | N | M | Certificate chain is missing intermediate certificates |
| 218 | ROOT_KEY_FINGERPRINT_MISMATCH | A | N | M | Root Public Key fingerprint does not match expected value |

**Recovery notes:**
- Most cert errors → re-enroll (`smo-node import`).
- `CERT_BAD_SIGNATURE` → security alert; do NOT re-enroll with same Authority.
- `MANIFEST_BAD_SIGNATURE` → manifest is compromised; obtain fresh copy from trusted source.
- `ROOT_KEY_FINGERPRINT_MISMATCH` → possible MITM; operator must verify out-of-band.

---

## 4. TRANSPORT (ErrorCategory::Transport) — codes 300-399

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 300 | CONNECTION_REFUSED | W | B | C | TCP connection refused by remote endpoint |
| 301 | CONNECTION_TIMEOUT | W | B | C | TCP connection timed out |
| 302 | CONNECTION_RESET | W | S | C | TCP connection reset by peer |
| 303 | CONNECTION_CLOSED | I | S | C | TCP connection closed gracefully |
| 304 | WRITE_FAILED | E | S | C | Write to socket failed (buffer full, peer disconnected) |
| 305 | READ_FAILED | E | S | C | Read from socket failed |
| 306 | LISTEN_FAILED | E | N | R | Could not bind/listen on the specified address/port |
| 307 | ADDRESS_IN_USE | W | N | R | Port is already in use |
| 308 | DNS_RESOLVE_FAILED | W | B | S | Could not resolve hostname |
| 309 | FRAME_TOO_LARGE | E | N | N | Incoming frame exceeds maximum allowed size |
| 310 | FRAME_TRUNCATED | E | N | C | Incoming frame is truncated (connection closed mid-frame) |
| 311 | FRAME_CHECKSUM_MISMATCH | E | N | C | Frame checksum failed (corrupted on wire) |
| 312 | VERSION_NEGOTIATION_FAILED | W | N | C | No common protocol version between two nodes |
| 313 | VERSION_NOT_SUPPORTED | W | N | C | Remote node version is outside supported range |
| 314 | BUFFER_EXHAUSTED | E | N | R | Transport buffer pool exhausted (resource pressure) |
| 315 | SERIALIZE_FAILED | E | N | N | Message serialization failed (schema mismatch, invalid data) |
| 316 | DESERIALIZE_FAILED | E | N | N | Message deserialization failed (malformed wire data) |
| 317 | STUN_REQUEST_FAILED | W | B | S | STUN server did not respond |
| 318 | STUN_MAPPING_INVALID | W | S | S | STUN returned an invalid mapped address |
| 319 | ICE_CANDIDATE_GATHERING_FAILED | W | S | S | No ICE candidates could be gathered |
| 320 | ICE_CONNECTIVITY_FAILED | W | N | R | All ICE candidate pairs failed connectivity check |
| 321 | HOLE_PUNCH_FAILED | W | N | R | UDP hole punch did not establish direct connection |
| 322 | TURN_ALLOCATION_FAILED | W | B | S | TURN relay allocation failed (auth, quota, timeout) |
| 323 | TURN_RELAY_EXHAUSTED | W | N | S | TURN relay bandwidth or capacity exhausted |

**Recovery notes:**
- Connection errors → `RECONNECTING` state with exponential backoff (§29.5).
- Frame errors → packet dropped; source trust score penalized if repeated.
- STUN/ICE/TURN errors → fallback chain: try next connectivity method.
- `VERSION_NEGOTIATION_FAILED` → connection rejected; node needs upgrade.

---

## 5. DISCOVERY (ErrorCategory::Discovery) — codes 400-499

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 400 | PEER_NOT_FOUND | I | S | N | Requested NodeID is not in the membership table |
| 401 | PEER_UNREACHABLE | W | B | N | Peer does not respond to PING |
| 402 | PEER_SUSPECT | I | S | N | SWIM suspicion triggered; awaiting confirmation |
| 403 | PEER_TIMEOUT | W | N | N | Peer heartbeat timeout; peer marked OFFLINE |
| 404 | GOSSIP_FAILED | W | B | N | Gossip message could not be delivered to target peer |
| 405 | MEMBERSHIP_TABLE_FULL | W | N | N | Membership table at capacity; oldest entry evicted |
| 406 | BOOTSTRAP_NO_SEEDS | E | B | M | No seed nodes could be reached; node cannot join mesh |
| 407 | BOOTSTRAP_AUTH_FAILED | E | N | E | Bootstrap node rejected the connection (cert issue) |
| 408 | ROUTE_NOT_FOUND | W | S | N | No route to target NodeID |
| 409 | ROUTE_EXHAUSTED | E | N | N | All routes to target NodeID have been tried and failed |

**Recovery notes:**
- `PEER_UNREACHABLE` → try next address in Peer Record; if all fail, mark OFFLINE.
- `BOOTSTRAP_NO_SEEDS` → operator must provide at least one reachable seed.
- `ROUTE_NOT_FOUND` → wait for gossip to propagate updated membership.

---

## 6. SESSION (ErrorCategory::Session) — codes 500-599

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 500 | SESSION_OPEN_FAILED | E | S | C | Session could not be established |
| 501 | SESSION_CLOSED | I | S | C | Session was closed normally |
| 502 | SESSION_EXPIRED | W | S | C | Session TTL expired; must open new session |
| 503 | SESSION_REJECTED | E | N | C | Remote node rejected session (cert, epoch, or policy) |
| 504 | SESSION_CAP_EXCEEDED | W | N | N | Maximum concurrent sessions reached |
| 505 | SESSION_RENEW_FAILED | W | S | C | Session renewal rejected; must open new session |
| 506 | SESSION_RECONNECT_FAILED | E | B | C | Reconnection attempts exhausted; session permanently closed |
| 507 | CAPABILITY_NOT_GRANTED | E | N | N | Capability is not in the session's active set |
| 508 | CAPABILITY_EXPIRED | W | N | S | Capability has expired within the session |
| 509 | CAPABILITY_REVOKED | E | N | S | Capability was revoked mid-session |
| 510 | NONCE_REPLAY_DETECTED | A | V | N | Same nonce used twice by the same sender; security violation |
| 511 | NONCE_TIMESTAMP_DRIFT | W | N | C | Packet timestamp outside ±300s window; possible replay |

**Recovery notes:**
- `SESSION_EXPIRED` → transparent renewal or new session.
- `CAPABILITY_REVOKED` → mid-session revocation; current contract may be interrupted.
- `NONCE_REPLAY_DETECTED` → security alert; packet dropped, source penalized.

---

## 7. PROTOCOL (ErrorCategory::Protocol) — codes 600-699

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 600 | UNKNOWN_OPCODE | E | N | N | Opcode namespace/id not recognized |
| 601 | UNKNOWN_NAMESPACE | E | N | N | Protocol namespace byte not recognized |
| 602 | MALFORMED_MESSAGE | E | N | N | Message does not match schema for its opcode |
| 603 | MISSING_FIELD | E | N | N | Required field is absent from message payload |
| 604 | INVALID_FIELD | E | N | N | Field value is out of range or invalid |
| 605 | CONTRACT_REJECTED | I | N | N | Contract was rejected by Responder (normal operation) |
| 606 | CONTRACT_ALREADY_EXISTS | I | S | N | Duplicate contract_id; cached result returned |
| 607 | CONTRACT_TIMEOUT | W | N | N | Contract proposal timed out waiting for response |
| 608 | CONTRACT_GARBAGE_COLLECTED | I | N | N | Orphaned contract was cleaned up after gc_ttl |
| 609 | WITNESS_TIMEOUT | W | N | F | Witness did not respond in time; falling back |
| 610 | WITNESS_CONFLICT | W | N | N | Witness attestation conflicts with local observation |
| 611 | EXECUTION_ALREADY_STARTED | W | N | N | Duplicate EXEC_START for the same contract |
| 612 | EXECUTION_FAILED | E | N | N | Execution completed with non-zero exit or runtime error |
| 613 | EXECUTION_CANCELLED | I | N | N | Execution was cancelled by requester or governance |
| 614 | EXECUTION_TIMEOUT | W | N | N | Execution exceeded its declared deadline |
| 615 | DATA_CHANNEL_EXISTS | W | N | N | Data channel for this hash already open |
| 616 | DATA_CHANNEL_NOT_FOUND | E | N | N | CHUNK references unknown channel_id |
| 617 | DATA_CHUNK_OUT_OF_ORDER | W | N | N | CHUNK sequence number gap detected |
| 618 | DATA_TRANSFER_FAILED | E | S | S | Data transfer aborted mid-stream |
| 619 | GOVERNANCE_PROPOSAL_EXISTS | I | S | N | Duplicate governance_id; proposal already pending |
| 620 | GOVERNANCE_PROPOSAL_EXPIRED | I | N | N | Governance proposal did not meet threshold in time |
| 621 | GOVERNANCE_PROPOSAL_REJECTED | I | N | N | Governance proposal was explicitly rejected |
| 622 | GOVERNANCE_THRESHOLD_NOT_MET | W | N | G | Governance proposal needs more signatures |
| 623 | GOVERNANCE_FORK_DETECTED | C | N | M | Mesh split detected; governance histories diverged |

**Recovery notes:**
- `MALFORMED_MESSAGE` → packet dropped; repeated offenses → trust penalty.
- `CONTRACT_REJECTED` is NOT an error — it is a normal protocol response.
- `WITNESS_TIMEOUT` → FSM fires fallback transition (§29.4).
- `GOVERNANCE_FORK_DETECTED` → operator must decide which history to keep (§33.8).

---

## 8. RUNTIME (ErrorCategory::Runtime) — codes 700-799

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 700 | FSM_INVALID_TRANSITION | E | N | F | Requested FSM transition is not valid for current state |
| 701 | FSM_TIMEOUT | W | N | F | FSM state exceeded maximum dwell time |
| 702 | FSM_STATE_CORRUPTED | C | N | B | Persisted FSM state is corrupted on load |
| 703 | SCHEDULER_QUEUE_FULL | W | N | R | Scheduler queue at capacity; contract rejected |
| 704 | SCHEDULER_DEADLINE_EXCEEDED | W | N | N | Task in DAG exceeded its deadline |
| 705 | SCHEDULER_CYCLE_DETECTED | E | N | N | DAG contains a cycle (compiler should have caught this) |
| 706 | EXECUTOR_NOT_FOUND | E | N | N | No executor registered for this opcode |
| 707 | EXECUTOR_CRASHED | C | N | B | Executor process terminated unexpectedly |
| 708 | WORKERPOOL_EXHAUSTED | W | N | R | All workers busy; task queued or rejected |
| 709 | SANDBOX_INIT_FAILED | E | N | R | Sandbox (cgroup/namespace) could not be initialized |
| 710 | SANDBOX_VIOLATION | A | N | M | Execution attempted to escape sandbox constraints |
| 711 | AUDIT_LOG_WRITE_FAILED | C | N | B | Audit log could not be written; node may need restart |
| 712 | RESOURCE_EXCEEDED | E | N | R | Execution exceeded declared resource limits |
| 713 | RESOURCE_UNAVAILABLE | W | B | R | Node does not have sufficient resources for this contract |
| 714 | RECOVERY_FAILED | C | N | M | State recovery could not reconstruct execution history |
| 715 | PLUGIN_LOAD_FAILED | E | N | R | Plugin `.so` could not be loaded (dlopen failed) |
| 716 | PLUGIN_ABI_MISMATCH | E | N | R | Plugin ABI version does not match runtime |
| 717 | PLUGIN_CRASHED | C | N | B | Plugin execution caused segfault or unhandled exception |
| 718 | PLUGIN_NOT_RESPONDING | W | B | R | Plugin health check timed out |

**Recovery notes:**
- `FSM_INVALID_TRANSITION` → invariant violation; must be caught in testing.
- `EXECUTOR_CRASHED` → FSM transitions to FAILED; requester notified.
- `SANDBOX_VIOLATION` → security alert; execution killed, node quarantined.
- `AUDIT_LOG_WRITE_FAILED` → critical; node should refuse new contracts until resolved.

---

## 9. GOVERNANCE (ErrorCategory::Governance) — codes 800-899

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 800 | AUTHORITY_NOT_FOUND | E | N | G | No Authority with the given NodeID exists |
| 801 | AUTHORITY_NOT_AUTHORIZED | E | N | G | Authority does not have the required privileges for this action |
| 802 | AUTHORITY_REVOKED | E | N | G | Authority certificate has been revoked |
| 803 | AUTHORITY_THRESHOLD_NOT_MET | W | N | G | Not enough Authority signatures for this governance action |
| 804 | AUTHORITY_CONFLICT | C | N | M | Two Authorities issued conflicting decisions; POLICY_CONFLICT state |
| 805 | CAPABILITY_CONFLICTED | E | N | G | Capability is in CONFLICTED state; no decision until resolution |
| 806 | POLICY_CHANGE_REJECTED | I | N | N | Policy change proposal was rejected by an Authority |
| 807 | EPOCH_INCREMENT_FAILED | C | N | M | Epoch increment could not be committed (network or threshold) |
| 808 | EMERGENCY_LOCKDOWN | A | N | M | Emergency lockdown triggered; all non-critical operations halted |
| 809 | MESH_SPLIT_DETECTED | C | N | M | Mesh partition detected; governance histories diverged |
| 810 | GOVERNANCE_PROPOSAL_INVALID | E | N | N | Governance proposal format or content is invalid |
| 811 | ROOT_KEY_REQUIRED | C | N | M | This operation requires the Root Key (offline recovery) |

**Recovery notes:**
- `AUTHORITY_CONFLICT` → runtime enters POLICY_CONFLICT; operator must submit resolution proposal.
- `EMERGENCY_LOCKDOWN` → all new contracts rejected; existing executions drained.
- `ROOT_KEY_REQUIRED` → operator must retrieve Recovery Package and reconstruct Root Key offline.
- `MESH_SPLIT_DETECTED` → no automatic merge; see §33.8.

---

## 10. STORAGE (ErrorCategory::Storage) — codes 900-999

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 900 | STORE_NOT_FOUND | C | N | B | Storage backend could not be opened/initialized |
| 901 | STORE_CORRUPTED | C | N | B | Storage data is corrupted (checksum mismatch, invalid format) |
| 902 | KEY_NOT_FOUND | I | S | N | Requested key does not exist in the store |
| 903 | KEY_ALREADY_EXISTS | W | N | N | Insert failed; key already exists |
| 904 | WRITE_FAILED | E | S | R | Write to storage failed (I/O error, disk full) |
| 905 | READ_FAILED | E | S | R | Read from storage failed (I/O error) |
| 906 | DELETE_FAILED | W | S | R | Delete operation failed |
| 907 | TRANSACTION_CONFLICT | W | S | R | Concurrent transaction conflict; retry |
| 908 | TRANSACTION_ABORTED | W | S | R | Transaction was rolled back |
| 909 | SERIALIZATION_FAILED | E | N | N | Value could not be serialized for storage |
| 910 | DESERIALIZATION_FAILED | E | N | N | Stored value could not be deserialized |
| 911 | QUOTA_EXCEEDED | W | N | R | Storage quota for this store has been exceeded |
| 912 | BACKUP_FAILED | E | S | R | Backup operation failed (I/O error, insufficient space) |
| 913 | RESTORE_FAILED | C | N | M | Restore from backup failed; data may be lost |

**Recovery notes:**
- `STORE_CORRUPTED` → attempt restore from backup; if unavailable, node must re-enroll.
- `TRANSACTION_CONFLICT` → caller retries with backoff.
- `RESTORE_FAILED` → data loss; operator must decide: re-enroll or accept missing history.

---

## 11. INTERNAL (ErrorCategory::Internal) — codes 1000-1023

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 1000 | INVARIANT_VIOLATION | C | N | B | A design invariant was violated (this is a bug) |
| 1001 | ASSERTION_FAILED | C | N | B | Internal assertion failed (this is a bug) |
| 1002 | OUT_OF_MEMORY | C | N | B | Memory allocation failed |
| 1003 | OUT_OF_DISK | C | N | B | Disk space exhausted |
| 1004 | THREAD_POOL_EXHAUSTED | C | N | B | All threads in pool are blocked; deadlock risk |
| 1005 | DEADLOCK_DETECTED | C | N | B | Lock acquisition timed out; possible deadlock |
| 1006 | UNREACHABLE_CODE | C | N | B | Code path that should never be reached was reached |
| 1007 | NOT_IMPLEMENTED | E | N | N | Feature is not yet implemented |
| 1008 | CONFIGURATION_ERROR | E | N | M | Node configuration is invalid or missing required values |
| 1009 | INTERNAL_TIMEOUT | C | N | B | Internal operation timed out (possible livelock) |

**Recovery notes:**
- All internal errors indicate a bug or resource exhaustion.
- `INVARIANT_VIOLATION` → node should self-quarantine to prevent cascade.
- `OUT_OF_MEMORY` / `OUT_OF_DISK` → operator must free resources.
- `CONFIGURATION_ERROR` → operator must fix config and restart.

---

## 12. COMPILER (ErrorCategory::Compiler) — codes 1100-1199

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 1100 | PARSE_FAILED | E | N | N | Contract source could not be parsed (syntax error) |
| 1101 | VALIDATION_FAILED | E | N | N | Contract failed semantic validation |
| 1102 | CAPABILITY_RESOLUTION_FAILED | E | N | N | Contract requires capabilities not present in session |
| 1103 | NODE_PLANNING_FAILED | E | N | N | No node in mesh satisfies contract constraints |
| 1104 | DAG_CYCLE_DETECTED | E | N | N | Dependency graph contains a cycle |
| 1105 | DAG_TOO_LARGE | E | N | N | DAG exceeds max node count or depth |
| 1106 | UNKNOWN_OPCODE_IN_CONTRACT | E | N | N | Contract references an opcode not recognized by the compiler |
| 1107 | RESOURCE_DECLARATION_INVALID | E | N | N | Resource declaration in contract is malformed or out of range |

**Recovery notes:**
- All compiler errors are fatal to the current contract; requester must fix and resubmit.
- No automatic retry; requester receives error details in CONTRACT_REJECT.

---

## 13. TRUST (ErrorCategory::Trust) — codes 1200-1299

| Code | Name | Sev | Ret | Rec | Description |
|---|---|---|---|---|---|
| 1200 | TRUST_SCORE_UNAVAILABLE | I | S | N | No trust data for this NodeID; using default score |
| 1201 | TRUST_BELOW_THRESHOLD | I | N | N | Requester trust score is below local threshold |
| 1202 | TRUST_PENALTY_APPLIED | I | N | N | Trust penalty was applied for policy violation |
| 1203 | TRUST_DIGEST_INVALID | W | N | N | Received trust digest has invalid signature |
| 1204 | TRUST_DIGEST_STALE | I | N | N | Received trust digest is older than local data; ignored |
| 1205 | WITNESS_SELECTION_FAILED | W | S | F | No suitable witness found; proceeding with local decision |
| 1206 | WITNESS_ATTESTATION_INVALID | E | N | N | Witness attestation has invalid signature |
| 1207 | WITNESS_ATTESTATION_CONFLICT | W | N | N | Witness attestation conflicts with local observation |
| 1208 | ATTESTATION_EXPIRED | W | N | N | Witness attestation timestamp is outside acceptable window |

**Recovery notes:**
- `TRUST_BELOW_THRESHOLD` → contract rejected; requester can improve trust via good behavior.
- `WITNESS_SELECTION_FAILED` → FSM proceeds with local decision only (§29.4).
- `WITNESS_ATTESTATION_INVALID` → witness trust score penalized.

---

## Error Code Ranges by Category

| Category | Range | Count |
|---|---|---|
| Crypto | 1 — 99 | 15 |
| Identity | 100 — 199 | 11 |
| Certificate | 200 — 299 | 19 |
| Transport | 300 — 399 | 24 |
| Discovery | 400 — 499 | 10 |
| Session | 500 — 599 | 12 |
| Protocol | 600 — 699 | 24 |
| Runtime | 700 — 799 | 19 |
| Governance | 800 — 899 | 12 |
| Storage | 900 — 999 | 14 |
| Internal | 1000 — 1099 | 10 |
| Compiler | 1100 — 1199 | 8 |
| Trust | 1200 — 1299 | 9 |
| **Total** | | **187** |

Each category has room for expansion (up to code 1023 per category).
