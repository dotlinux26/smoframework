# RFC 0020 — Opcode Registry

## Status
DRAFT — pending review.

## Problem
All protocol messages in SMO use a two-level hierarchical namespace. Without a formal registry, developers will invent ad-hoc opcodes, collide namespace bytes, and make wire-incompatible changes. The registry must be the single source of truth for all message types across all four protocol layers.

## Decisions

### 1. Two-level namespace: 1 byte namespace + 2 byte message ID
```
[NAMESPACE(1)] [MESSAGE_ID(2)]
```
Total: 3 bytes in the packet header. This provides 256 namespaces × 65536 messages each — more than sufficient for the lifetime of SMO.

### 2. Four fixed namespaces (frozen)

| Namespace | Byte | Transport | Purpose |
|---|---|---|---|
| DISCOVERY | 0x01 | UDP | Mesh discovery, liveness, membership |
| CONTROL | 0x02 | TCP | Session, contract, certificate, governance |
| EXECUTION | 0x03 | TCP | Execution lifecycle |
| DATA | 0x04 | TCP | Bulk data transfer |

Namespaces 0x05-0xEF are reserved for future protocol layers. 0xF0-0xFF are reserved for experimental/internal use.

### 3. Registered messages per namespace

**DISCOVERY (0x01):**
| ID | Message | Payload | Direction |
|---|---|---|---|
| 0x0001 | HELLO | NodeID, PubKeyFingerprint, ProtocolVersion | bidirectional |
| 0x0002 | DISCOVER | (empty or seed list hash) | request/response |
| 0x0003 | NODE_INFO | PeerRecord (serialized) | bidirectional |
| 0x0004 | PING | Timestamp | request/response |
| 0x0005 | OFFLINE | NodeID, reason | notification |

**CONTROL (0x02):**
| ID | Message | Payload | Direction |
|---|---|---|---|
| 0x0001 | CONTRACT_PROPOSAL | Contract JSON | Requester → Responder |
| 0x0002 | CONTRACT_ACCEPT | ContractID, signature | Responder → Requester |
| 0x0003 | CONTRACT_REJECT | ContractID, reason code | Responder → Requester |
| 0x0004 | CONTRACT_RESULT | ContractID, ResultHash, Status | Responder → Requester |
| 0x0010 | SESSION_OPEN | Nonce, Cert | bidirectional |
| 0x0011 | SESSION_CLOSE | Reason | bidirectional |
| 0x0012 | SESSION_RENEW | New nonce | bidirectional |
| 0x0020 | CSR | EnrollRequest (.smor) | Node → Authority |
| 0x0021 | CERTIFICATE | Membership cert (.smoc) | Authority → Node |
| 0x0030 | WITNESS_REQUEST | ContractID, RequesterID | Responder → Witness |
| 0x0031 | WITNESS_RESPONSE | Attestation | Witness → Responder |
| 0x0040 | CAP_GRANT | NodeID, CapabilitySet | Authority → Node |
| 0x0041 | CAP_REVOKE | NodeID, CapabilitySet | Authority → Node |
| 0x0050 | REVOKE_CERT | NodeID, CertHash | Authority → mesh |
| 0x0051 | EPOCH_INCREMENT | NewEpoch, signatures | Authority → mesh |
| 0x0060 | TRUST_DIGEST | TrustDigest | bidirectional |
| 0x0070 | GOVERNANCE_PROPOSAL | Proposal | Authority ↔ Authority |
| 0x0071 | GOVERNANCE_SIGNATURE | ProposalID, Signature | Authority ↔ Authority |
| 0x0072 | GOVERNANCE_COMMIT | ProposalID, Result | Authority → mesh |

**EXECUTION (0x03):**
| ID | Message | Payload | Direction |
|---|---|---|---|
| 0x0001 | EXEC_START | ContractID, DAGHash | Responder → Executor |
| 0x0002 | EXEC_PROGRESS | ContractID, Progress | Executor → Requester |
| 0x0003 | EXEC_EVENT | ContractID, Event | Executor → Requester |
| 0x0004 | EXEC_RESULT | ContractID, ResultHash, ExitCode | Executor → Requester |
| 0x0005 | EXEC_CANCEL | ContractID | Requester → Executor |
| 0x0006 | EXEC_TIMEOUT | ContractID | Executor → Requester |
| 0x0007 | EXEC_ERROR | ContractID, ErrorCode | Executor → Requester |

**DATA (0x04):**
| ID | Message | Payload | Direction |
|---|---|---|---|
| 0x0001 | CHANNEL_OPEN | ChannelID, DataHash, Size | bidirectional |
| 0x0002 | CHUNK | ChannelID, SeqNum, Bytes | sender → receiver |
| 0x0003 | ACK | ChannelID, SeqNum | receiver → sender |
| 0x0004 | NACK | ChannelID, SeqNum, Reason | receiver → sender |
| 0x0005 | FIN | ChannelID, TotalBytes, Checksum | sender → receiver |
| 0x0006 | CANCEL | ChannelID, Reason | bidirectional |

### 4. Registration rules (frozen)
1. No two messages may share the same (namespace, message_id) pair.
2. Message IDs within a namespace must be allocated sequentially within functional groups (0x00-0x0F, 0x10-0x1F, etc.) to leave room for future messages in the same group.
3. A message ID once assigned and released in any SMO 3.x version MUST NOT be reassigned. It is marked RESERVED permanently.
4. Adding a new message requires an RFC update to this registry. No ad-hoc IDs in implementation.

## Interfaces

```cpp
enum class ProtocolNamespace : uint8_t {
    Discovery  = 0x01,
    Control    = 0x02,
    Execution  = 0x03,
    Data       = 0x04,
};

struct Opcode {
    ProtocolNamespace ns;
    uint16_t          id;

    bool operator==(const Opcode&) const = default;
};

// Registry: compile-time constant table
struct OpcodeEntry {
    Opcode          opcode;
    std::string_view name;
    std::string_view description;
    bool            is_request;    // vs response/notification
    bool            idempotent;    // replay-safe?
};

// Generated from this RFC — NEVER hand-coded
constexpr std::array<OpcodeEntry, 35> OPCODE_REGISTRY = {{ ... }};
```

## Consequences
- Opcode registry is frozen for SMO 3.x. New messages require an RFC amendment.
- 35 registered messages across 4 namespaces. Room for ~65,000 more per namespace.
- Hierarchical namespace prevents the flat 0x01-0xFF exhaustion problem of old SMF.
- Functional grouping (0x10-0x1F for session, 0x20-0x2F for certificate, etc.) keeps IDs organized.
- Experimental namespace (0xF0-0xFF) allows internal testing without polluting the registry.
