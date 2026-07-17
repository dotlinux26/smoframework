# RFC 0032 — Zero-Touch Enrollment & Mesh Join

**Status:** PROPOSED  
**Date:** 2026-07-17  
**Authors:** dotlinux26, D-O-T-Solutions  

---

## Summary

This RFC defines the **Zero-Touch Enrollment** flow — a single-command mesh join experience that automates the entire enrollment pipeline from Join Token to running daemon. The operator runs one command (`smo mesh join <token>` or `smo node --join <token>`) and the system handles the complete enrollment pipeline automatically.

---

## Problem

Current enrollment requires **7 manual steps**:

```
1. smo-node --init --name node1 --data /tmp/node1
2. smo-node --export /tmp/node1.csr.smor --data /tmp/node1
3. scp /tmp/node1.csr.smor cloud:/tmp/
4. smo-admin --mesh production sign /tmp/node1.csr.smor -o /tmp/node1.cert.smoc
7. scp cloud:/tmp/node1.cert.smoc /tmp/
8. smo-node --import /tmp/node1.cert.smoc --data /tmp/node1
9. smo-node --daemon --data /tmp/node1 --seed 140.245.39.19:5454
```

This is **unacceptable for production**. Operators should run **one command** and the system handles the rest.

---

## Design

### New CLI Commands

#### `smo mesh join <token>` (in smo-cli)
```bash
smo mesh join SMO-JOIN-<base64url>
```

#### `smo-node --join <token>` (in smo-node)
```bash
smo-node --join SMO-JOIN-<token> --data /tmp/my-node
```

### Pipeline (Automated)

```
mesh join TOKEN
    │
    ├─► Decode token (CBOR + HMAC)
    │     ├─ Validate HMAC (error 212)
    │     ├─ Check expiry (error 213)
    │     └─ Extract: mesh_id, bootstrap_endpoints[], role, cipher_suite_id
    │
    ├─► Initialize identity (or load existing)
    │     ├─ Generate keypair (if new)
    │     ├─ Build CSR (.smor) with display_name, role, mesh_id
    │     └─ Sign CSR with node's secret key
    │
    ├─► Connect to bootstrap endpoint(s)
    │     ├─ Try each bootstrap_endpoints[] in order
    │     ├─ POST /enroll with CSR + token
    │     └─ Authority validates token, verifies CSR, signs cert
    │
    ├─► Receive certificate (.smoc)
    │     ├─ Verify cert chain to Root
    │     ├─ Import into local identity store
    │     └─ Update identity.json (state: Enrolled)
    │
    ├─► Start daemon
    │     ├─ Read mesh config (listen_address, mesh_id, cipher_suite)
    │     ├─ Bind listen_address
    │     ├─ Connect to bootstrap_endpoints[]
    │     ├─ Start heartbeat/gossip
    │     └─ Ready
    │
    └─► Print post-import summary
          NodeID, Cert fingerprint, Cipher Suite, Mesh, Role, Epoch, Expiry
```

### Error Handling

| Error | Code | Recovery |
|-------|------|----------|
| Invalid HMAC | 212 | Token corrupted, request new |
| Token expired | 213 | Request new invite |
| Bootstrap unreachable | 214 | Check network, try next endpoint |
| CSR rejected | 215 | Check policy, retry |
| Cert verification failed | 216 | Corrupt response, retry |
| Mesh not found | 217 | Wrong token, check mesh_id |

---

## CLI Specification

### `smo mesh join <token>` (smo-cli)

```bash
smo mesh join --token SMO-JOIN-<base64url> [--data <dir>] [--name <name>] [--port <port>]
```

**Flags:**
- `--data <dir>` — Data directory (default: `~/.smo/node`)
- `--name <name>` — Display name (default: hostname)
- `--port <port>` — Listen port (default: from token's listen_address)

Mesh context is used automatically. No `--mesh` or `--mesh-dir` needed.

**Behavior:**
1. Decodes token, validates HMAC & expiry
2. Creates/loads identity in `--data`
3. Connects to bootstrap endpoints from token
4. Sends CSR, receives cert, imports
5. Starts daemon with bootstrap endpoints as seeds
6. Prints post-import summary, exits 0 on success

### `smo-node --join <token>` (smo-node)

```bash
smo-node --join SMO-JOIN-... [--data <dir>] [--name <name>] [--port <port>]
```

**Behavior:** Same as `smo mesh join` but in smo-node binary. Non-interactive, exits after daemon starts (or fails).

**Data directory:** `~/.smo/node/` by default (overridable via `--data`).

---

## Authority Side: `/enroll` Endpoint

### Request

```
POST /enroll
Content-Type: application/cbor

{
  "token": "<SMO-JOIN-base64url>",
  "csr": <CBOR-encoded .smor>
}
```

### Response (Success)

```
200 OK
Content-Type: application/cbor

{
  "cert": <CBOR-encoded .smoc>,
  "chain": [<authority_cert>, <root_cert>]
}
```

### Response (Error)

```
4xx/5xx
Content-Type: application/cbor

{
  "error": 212,
  "message": "Invalid HMAC"
}
```

---

## Security

### Token Validation (Authority)

```go
func validateJoinToken(token string, hmacSecret []byte) (JoinToken, error) {
    // 1. Decode base64url
    // 2. Split: payload || HMAC (last 32 bytes)
    // 3. Verify HMAC-SHA256(payload, hmacSecret)
    // 4. Decode CBOR payload
    // 4. Check expiry > now
    // 5. Return parsed JoinToken
}
```

### CSR Validation (Authority)

```go
func validateCSR(csr CSR, token JoinToken) error {
    // 1. Verify CSR signature with csr.new_public_key
    // 2. csr.mesh_id == token.mesh_id
    // 3. csr.requested_role in allowed_roles (policy)
    // 4. csr.display_name valid & unique (check registry)
    // 5. csr.cipher_suite_id == token.cipher_suite_id
}
```

### Certificate Issuance

```go
func issueCertificate(csr CSR, token JoinToken, authority Authority) Certificate {
    cert := Certificate{
        MeshID:        token.MeshID,
        SubjectPubKey: csr.NewPublicKey,
        DisplayName:   csr.DisplayName,
        Role:          csr.RequestedRole,
        Epoch:         token.MeshEpoch,
        NotBefore:     now(),
        NotAfter:      now() + certValidityDays,
        CipherSuiteID: token.CipherSuiteID,
        IssuedBy:      authority.PublicKey,
    }
    cert.Signature = authority.Signer.Sign(cert.SerializeForSigning())
    return cert
}
```

---

## Error Codes (Extend error_codes.md)

| Code | Name | Severity | Retry | Description |
|------|------|----------|-------|-------------|
| 212 | ENROLL_TOKEN_INVALID | Error | NoRetry | HMAC mismatch or malformed |
| 213 | ENROLL_TOKEN_EXPIRED | Error | NoRetry | Token past expiry |
| 214 | BOOTSTRAP_UNREACHABLE | Error | RetrySafe | All endpoints unreachable |
| 215 | CSR_REJECTED | Error | NoRetry | Policy violation |
| 216 | CERT_VERIFICATION_FAILED | Error | RetrySafe | Chain verify failed |
| 217 | MESH_NOT_FOUND | Error | NoRetry | MeshID mismatch |

---

## Implementation Tasks

| Task | Component | Effort |
|------|-----------|--------|
| `smo mesh join --token` | `cmd/smo-cli/main.cpp` | 2h |
| `smo-node --join` flag | `cmd/smo-node/main.cpp` | 1h |
| Auto-enrollment pipeline | `core/enroll/auto_enroll.cpp` | 4h |
| `/enroll` endpoint (Authority) | `core/authority/authority.cpp` | 3h |
| `/enroll` HTTP handler | `cmd/smo-node/enroll_client.cpp` | 2h |
| Error codes 212-217 | `core/errors/error_codes.md` | 30m |
| Integration test | `tests/integration/test_join.cpp` | 2h |

---

## Backward Compatibility

- **No breaking changes** — existing manual flow (`--init`, `--export`, `--import`, `--daemon`) remains unchanged
- `--join` is **additive** — new flag, new command
- Existing `smo-admin sign` + `smo-node --import` flow unchanged
- Existing `smo mesh publish` + `generate-invite` unchanged

---

## Security Considerations

1. **Token is single-use** — Authority marks token consumed after successful enrollment (prevent replay)
2. **Token expiry** — Short TTL (default 1h, max 24h) limits exposure
3. **CSR signed by node key** — Proves possession of private key
3. **Cert chain verified** — Node verifies cert chain to Root before import
4. **Private key never leaves node** — Generated locally, never transmitted
5. **Token binds to mesh_id + epoch** — Prevents cross-mesh/epoch replay

---

## References

- RFC 0007 — Enrollment Protocol (base protocol)
- RFC 0006 — Mesh Identity & Certificate Model
- RFC 0031 — Mesh Manager
- SPEC.md §7.5 — Enrollment Transport

---

## Appendix: Join Token CBOR Structure (Recap)

```cbor
{
  1: 1,                    // version
  2: h'mesh_id_bytes',     // mesh_id (16 bytes)
  3: 1,                    // mesh_epoch
  4: 3,                    // cipher_suite_id (Suite 3 = Pure PQC)
  5: ["140.245.39.19:5454"], // bootstrap_endpoints[]
  6: "Worker",             // role
  7: 1700000000,           // expiry (unix timestamp)
  8: h'nonce1234'          // nonce (8-16 bytes)
}
```

Wire format: `SMO-JOIN-<base64url(CBOR_payload || HMAC_SHA256(payload, hmac_secret))>`

---

**End of RFC 0032**

---

# Update: SPEC.md §7.5 Enrollment Transport (Add Zero-Touch)

Add to SPEC.md §7.5 after §7.5.11:

### 7.5.13 Zero-Touch Enrollment (`smo mesh join` / `smo-node --join`)

**Problem:** Manual enrollment (init → export → copy → sign → copy → import → daemon) is error-prone and slow.

**Decision:** Single-command zero-touch enrollment via Join Token.

**Flow:**
```
smo mesh join SMO-JOIN-...
    │
    ├─ Decode token, validate HMAC & expiry
    ├─ Initialize local identity (or reuse)
    ├─ Generate CSR, sign with node key
    ├─ POST /enroll to bootstrap_endpoints[] with CSR + token
    │   └─ Authority validates token, verifies CSR, signs cert
    ├─ Receive .smoc, verify chain, import
    ├─ Start daemon with bootstrap_endpoints[] as seeds
    └─ Print post-import summary
```

**Authority `/enroll` endpoint:**
- `POST /enroll` with CBOR `{token, csr}`
- Validates token HMAC, expiry, mesh_id, epoch
- Verifies CSR signature, checks policy
- Issues certificate signed by Authority key
- Returns `{cert, chain}` CBOR

**Error codes:** 212 (INVALID), 213 (EXPIRED), 214 (UNREACHABLE), 215 (REJECTED), 216 (VERIFY_FAILED), 217 (MESH_NOT_FOUND)

**Invariants:**
- Private key never leaves node
- Cert chain verified to Root before import
- Token single-use (Authority marks consumed)
- Token TTL ≤ 24h (default 1h)

---

## Update: Error Codes (Add to error_codes.md)

| Code | Name | Severity | Retry | Description |
|------|------|----------|-------|-------------|
| 212 | ENROLL_TOKEN_INVALID | Error | NoRetry | HMAC mismatch or malformed |
| 213 | ENROLL_TOKEN_EXPIRED | Error | NoRetry | Token past expiry |
| 214 | BOOTSTRAP_UNREACHABLE | Error | RetrySafe | All endpoints unreachable |
| 215 | CSR_REJECTED | Error | NoRetry | Policy violation |
| 216 | CERT_VERIFICATION_FAILED | Error | RetrySafe | Chain verify failed |
| 217 | MESH_NOT_FOUND | Error | NoRetry | MeshID mismatch |
| 223 | BOOTSTRAP_NOT_CONFIGURED | Error | NoRetry | Mesh not published (was 214) |
| 224 | PORT_UNAVAILABLE | Warn | NoRetry | Port in use |
| 225 | NO_PUBLIC_IP_DETECTED | Warn | NoRetry | No public IP found |

---

## Update: SPEC.md §7.5 Port Check Fix

In §7.5.6 Join Token, update port check description:

> **Port availability is verified at publish time** via `bind()` probe to the **listen_address port** (not hardcoded 7777). The check uses `SO_REUSEADDR` and immediately closes — it does NOT open a permanent socket. The daemon opens the socket on startup. Race conditions exist but operator is informed.