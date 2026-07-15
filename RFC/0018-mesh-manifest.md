# RFC 0018 — Mesh Manifest

## Status
DRAFT — pending review.

## Problem
Every mesh needs a "birth certificate" that encodes all mesh-level parameters: governance thresholds, crypto policy, transport policy, bootstrap seeds, trust defaults. Without a frozen manifest format, every node would need manual configuration to join, and mesh identity would not be verifiable from a single artifact.

## Decisions

### 1. Manifest is a YAML file (mesh.yaml)
YAML is chosen over JSON for: comments, multi-line values, git-diff friendly, human-writable. The manifest is a **deployment artifact**, not a protocol message — it is distributed out-of-band (file, URL, QR) and imported once at join time.

### 2. Manifest content is frozen
| Section | Fields | Purpose |
|---|---|---|
| `mesh` | uuid, name, genesis_time, protocol_version, description | Mesh identity |
| `root` | public_key (Base64) | Mesh Root Public Key — anchor of trust |
| `governance` | authority, policy, critical thresholds + authority_count | M-of-N config per level |
| `bootstrap` | nodes[], discovery method (seed/dns/mDNS/manual) | How to find first peers |
| `heartbeat` | interval_sec, timeout_sec | Liveness detection |
| `trust` | decay_window_sec, default_threshold, weights | Default trust parameters |
| `crypto` | allowed_suites[], min_suite | Crypto Suite ID policy |
| `transport` | allowed[], default_port | Permitted transports |
| `plugins` | allowed[] | Plugin whitelist |
| `capability_presets` | custom role → capability mappings | Override defaults |
| `policies` | default_allow[], default_deny[] | Mesh-wide policy (post-MVP) |
| `signatures` | authority_id + signature[] | M-of-N Authority co-signatures at creation |

### 3. Manifest is co-signed by M-of-N Authorities at creation
The manifest is NOT a single-authority document. At mesh creation, the Root signs the first Authority, then M-of-N initial Authorities co-sign the manifest. A new node verifies:
1. Root Public Key fingerprint matches expected
2. Manifest has M-of-N Authority signatures (per `governance.policy.threshold`)
3. All signatures chain up to the Root Public Key

### 4. Manifest is immutable after mesh creation
The manifest is the mesh's genesis record. Changing any field requires a governance proposal (Level 2+). The manifest is cached locally after import and never re-fetched unless explicitly updated via governance.

### 5. Multi-tenant directory layout
A single SMO runtime can hold multiple manifests:
```
~/.smo/meshes/
├── SOC-Production/
│   ├── manifest.yaml        (verified, co-signed)
│   ├── node.smoc            (membership certificate)
│   └── node.key             (encrypted private key)
├── IR-Prod/
│   ├── manifest.yaml
│   ├── node.smoc
│   └── node.key
└── Research/
    ├── manifest.yaml
    ├── node.smoc
    └── node.key
```
Each mesh is fully isolated: separate governance, separate trust, separate capabilities.

## Interfaces

```cpp
struct MeshManifest {
    // Section: mesh
    UUID     uuid;
    std::string name;
    TimePoint    genesis_time;
    uint16_t     protocol_version;
    std::string description;

    // Section: root
    std::vector<uint8_t> root_public_key;

    // Section: governance
    GovernanceConfig governance;

    // Section: bootstrap
    std::vector<Endpoint> bootstrap_nodes;
    std::string discovery_method;

    // Section: heartbeat
    uint32_t heartbeat_interval_sec;
    uint32_t heartbeat_timeout_sec;

    // Section: trust
    TrustConfig trust;

    // Section: crypto
    std::vector<uint16_t> allowed_crypto_suites;
    uint16_t min_crypto_suite;

    // Section: transport
    std::vector<std::string> allowed_transports;
    uint16_t default_port;

    // Section: signatures
    std::vector<ManifestSignature> signatures;

    static Result<MeshManifest> load(const std::filesystem::path& path);
    static Result<MeshManifest> parse(std::string_view yaml_content);
    Result<void> verify(const CryptoProvider& crypto) const;
    Result<void> verify_threshold() const;
};

struct GovernanceConfig {
    uint8_t issue_cert_threshold;       // default 1
    uint8_t revoke_cert_threshold;      // default 1
    uint8_t grant_cap_threshold;        // default 1
    uint8_t policy_threshold;           // default 2
    uint8_t policy_authority_count;     // total Authorities for policy level
    uint8_t critical_threshold;         // default 3
    uint8_t critical_authority_count;   // total Authorities for critical level
    uint8_t emergency_lockdown_threshold;
};

struct TrustConfig {
    uint32_t decay_window_sec;
    float default_threshold;
    float weight_citizen;
    float weight_execution;
    float weight_witness;
    float weight_consistency;
};

struct ManifestSignature {
    NodeID authority_id;
    std::vector<uint8_t> signature;
};
```

## Consequences
- Manifest is the single source of truth for mesh policy. No node needs per-node config beyond the manifest path.
- M-of-N co-signing prevents a single compromised Authority from forging a manifest.
- Multi-tenant directory layout enables one SMO runtime to serve multiple meshes simultaneously.
- Manifest changes require governance proposals — no silent mesh reconfiguration.
- YAML format enables human-readable review before import.
