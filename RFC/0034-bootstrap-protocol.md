# RFC 0034 — Bootstrap Protocol

| Field | Value |
|---|---|
| Status | Draft |
| Author | SMO Core Team |
| Date | 2026-07-17 |
| Supersedes | — |
| Extends | RFC 0019 (Packet Layout), RFC 0020 (Opcode Registry) |

## Summary

Define a Bootstrap Protocol namespace (`0x05`) for joining nodes to retrieve a
complete mesh snapshot in a single request-response exchange — no HTTP, no JSON,
no separate control plane.

After a node completes enrollment (Join Token → CSR → Certificate), it has
**no knowledge of the mesh topology**. The Bootstrap Protocol fills this gap by
returning a `BootstrapSnapshot` that contains every piece of state a newly
joined node needs to participate: manifest, authority list, seeds, epoch,
policy version, CRL digest, and capabilities.

## Motivation

- **Single transport**: Every protocol message flows through the same TCP
  Packet layer (RFC 0019). No HTTP server, no second control plane.
- **Embedded-friendly**: CBOR decoding is ~2 KB of code. No JSON parser
  required on constrained devices (ESP32, STM32, ARM Cortex-M).
- **Native Contract path**: The snapshot handler can later become
  `system.bootstrap.snapshot` — a native contract executed by the runtime,
  gated by policy, logged by audit.
- **Future-proof**: Adding fields to the snapshot is a CBOR map append.
  No HTTP route changes, no version negotiation.

## Namespace Allocation (RFC 0020)

| Namespace | Value | Name | Transport |
|---|---|---|---|
| Discovery | `0x01` | DISCOVERY | UDP |
| Control | `0x02` | CONTROL | TCP |
| Execution | `0x03` | EXECUTION | TCP |
| Data | `0x04` | DATA | TCP |
| **Bootstrap** | **`0x05`** | **BOOTSTRAP** | **TCP** |

## Message IDs

| ID | Name | Direction | Payload |
|---|---|---|---|
| `0x0001` | BOOTSTRAP_REQUEST | Node → Authority | BootstrapRequest |
| `0x0002` | BOOTSTRAP_RESPONSE | Authority → Node | BootstrapResponse |

## Payload Serialization

All bootstrap messages use **CBOR** (RFC 7049) for payload serialization.

Rationale:
- Compact (typically 30–50 % smaller than JSON for the same data).
- No string parsing on embedded targets — CBOR decoder is trivially small.
- Native support for binary, integer, and map types without base64.
- Deterministic encoding rules (RFC 7049 §3.9) ensure verifiable signatures.

### BootstrapRequest (CBOR map)

```
{
    "version": 1,                // uint, protocol version
    "nonce": h'...',             // bstr, 8 bytes random
    "node_id": "abc...",         // tstr, hex node_id
    "cert_fingerprint": "def..." // tstr, hex cert fingerprint (optional)
}
```

CBOR map keys:

| Key | Field | Type | Required |
|---|---|---|---|
| 1 | version | uint | yes |
| 2 | nonce | bstr (8 bytes) | yes |
| 3 | node_id | tstr | yes |
| 4 | cert_fingerprint | tstr | no |

### BootstrapResponse (CBOR map)

Carries the full `BootstrapSnapshot` as the payload.

```
{
    "version": 1,
    "nonce": h'...',     // echoes request nonce
    "snapshot": { ... }  // BootstrapSnapshot (nested CBOR map)
}
```

CBOR map keys:

| Key | Field | Type | Required |
|---|---|---|---|
| 1 | version | uint | yes |
| 2 | nonce | bstr (8 bytes) | yes |
| 3 | snapshot | map | yes |

### BootstrapSnapshot (CBOR map)

```
{
    "mesh_id": "...",
    "mesh_state": "Online",
    "epoch": 1,
    "genesis_manifest": { ... },          // embedded CBOR map
    "authorities": [                      // array of authority info
        { "id": "...", "endpoint": "..." }
    ],
    "seeds": [ "tcp://...", ... ],        // array of seed endpoint strings
    "policy_version": 1,
    "governance_version": 1,
    "crl_digest": h'...',                // Blake3 hash of serialized CRL
    "crl_count": 0,
    "health": { "level": "Healthy", "operational": true },
    "cipher_suite": 3,                   // uint: 1=Classical, 2=Hybrid, 3=PurePQC
    "opcodes": [ 0x01, 0x02, ... ],      // supported opcode values
    "active_proposals": 0
}
```

CBOR map keys:

| Key | Field | CBOR Type |
|---|---|---|
| 1 | mesh_id | tstr |
| 2 | mesh_state | tstr |
| 3 | epoch | uint |
| 4 | genesis_manifest | map |
| 5 | authorities | array of map |
| 6 | seeds | array of tstr |
| 7 | policy_version | uint |
| 8 | governance_version | uint |
| 9 | crl_digest | bstr (32 bytes) |
| 10 | crl_count | uint |
| 11 | health | map |
| 12 | cipher_suite | uint |
| 13 | opcodes | array of uint |
| 14 | active_proposals | uint |

## Message Flow

```
Joining Node                    Authority Node
     │                              │
     │   ── Packet[0x05,0x0001] ──▶ │  BOOTSTRAP_REQUEST
     │       {nonce, node_id}       │
     │                              │  ├── verify session+nonce
     │                              │  ├── assemble snapshot
     │                              │  │   ├── MeshConfig (epoch, mesh_id)
     │                              │  │   ├── GenesisManifest
     │                              │  │   ├── NodeRegistry (authorities)
     │                              │  │   ├── CRL (digest + count)
     │                              │  │   ├── GovernanceEngine (active)
     │                              │  │   └── capabilities
     │                              │  │   └── serialize to CBOR
     │                              │  └── sign with authority key
     │   ◀── Packet[0x05,0x0002] ──│  BOOTSTRAP_RESPONSE
     │       {nonce, snapshot}      │
     │                              │
     │  ┌───────────────────────────│
     │  │ Apply snapshot:
     │  │  • store manifest
     │  │  • register authorities
     │  │  • update epoch
     │  │  • cache CRL digest
     │  │  • set policy version
     │  │  • transition FSM: Joining → Online
     │  ▼                           │
     │     Node is ready             │
```

## Security

1. **Nonce**: 8-byte random per request prevents replay.
2. **Session binding**: Request MUST be sent over an established session
   (RFC 0014) with a valid certificate.
3. **Response signature**: The authority signs the response packet
   (standard Packet signature field, RFC 0019).
4. **No secret material**: The snapshot contains only public mesh state.
   No keys or secrets are transmitted.

## Native Contract Future

After the Runtime/ExecutionEngine is fully operational, the bootstrap handler
SHOULD be replaced by a native contract `system.bootstrap.snapshot`:

```
ContractID = Blake3("system.bootstrap.snapshot")
Category = Kernel (0x00–0x0F)
Implementation = BootstrapSnapshotContract::execute()
```

The contract is registered in `register_kernel_contracts()` and dispatched
through the standard `Runtime::execute()` pipeline. This gives audit logging,
policy gating, and scheduler integration for free.

## Error Codes

| Code | Name | Description |
|---|---|---|
| 1700 | BootstrapProtocolError | Generic bootstrap protocol error |
| 1701 | BootstrapSnapshotUnavailable | Snapshot not ready (mesh still in genesis) |
| 1702 | BootstrapNonceMismatch | Response nonce does not match request |
| 1703 | BootstrapNotAuthorized | Node not authorized for bootstrap |

## Consequences

- **Positive**: No HTTP server, no JSON, no dual control plane.
- **Positive**: Every node speaks the same transport — embedded, desktop, server.
- **Positive**: Adding fields is a CBOR map extension — no wire breakage.
- **Negative**: Requires a CBOR library (or minimal encoder/decoder) in the
  core runtime (~2 KB of C++).
- **Migration**: The existing HTTP-based BootstrapService (prototype) MUST be
  removed before this RFC is accepted.
