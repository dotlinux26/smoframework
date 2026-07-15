# RFC 0015 — Discovery Engine

## Status
ACCEPTED — MVP uses UDP HELLO/PING only. Full SWIM gossip deferred to Stage 5.

## Problem
A new node joining a mesh must discover existing peers, broadcast its own presence, and maintain a live membership table. Nodes leaving (gracefully or by failure) must be detected and removed. Without a dedicated Discovery Engine, membership knowledge becomes stale and the mesh fragments.

## Decisions

### 1. Discovery Engine is separate from Transport (I-23)
Transport provides byte-stream connectivity. Discovery Engine provides peer awareness. They are independent subsystems that communicate through well-defined events. Transport never knows about membership; Discovery never sends raw bytes.

### 2. MVP uses basic UDP HELLO/PING; full SWIM gossip deferred
**MVP protocol** (Stage 1-2):
- `HELLO`: broadcast on bootstrap (UDP multicast or configured seed list).
- `WELCOME`: response to HELLO containing the responder's peer record.
- `PING` / `PONG`: periodic liveness check (every 15 seconds).
- `DISCOVER`: request additional peer records from a known peer.
- `NODE_INFO`: metadata exchange (protocol version, capabilities, public key fingerprint).
- `OFFLINE`: graceful departure notification.

**Post-MVP** (Stage 5+): SWIM gossip protocol for scalable membership dissemination, suspicion-based failure detection, and indirect probing.

### 3. Peer Record is the unit of membership
Each known peer has a `PeerRecord` containing:
- `node_id` (32 bytes)
- `public_key_fingerprint` (8 bytes prefix)
- `addresses[]` (multiple endpoints for connectivity fallback)
- `protocol_version`
- `capability_digest` (hash of the node's capability set)
- `trust_digest` (composite trust score hint)
- `last_seen` (timestamp)
- `state` (ONLINE, SUSPECT, OFFLINE, REMOVED)

### 4. Health monitor tracks liveness with 3-strike rule
After 3 consecutive missed PING responses, the peer is marked SUSPECT. After 3 additional SUSPECT cycles with no recovery, the peer is marked OFFLINE and removed from the active membership table. An OFFLINE peer is retained in the peer store for 24 hours for fast re-discovery.

### 5. Bootstrap via seed list or UDP multicast
On first join, the node either:
- Contacts configured seed nodes (preferred for production: `bootstrap = ["10.0.0.1:8443", "10.0.0.2:8443"]`).
- Sends a UDP multicast HELLO on the configured discovery address (LAN deployments).

After bootstrap succeeds, the node maintains membership through gossip.

## Interfaces

```cpp
enum class PeerState : uint8_t {
    Unknown, Online, Suspect, Offline, Removed
};

struct PeerRecord {
    NodeID             node_id;
    uint64_t           pubkey_fingerprint;
    std::vector<Endpoint> addresses;
    uint16_t           protocol_version;
    Hash256            capability_digest;
    float              trust_digest;       // 0.0 – 1.0, hint only
    TimePoint          last_seen;
    PeerState          state;
};

class MembershipTable {
    Result<void> upsert(const PeerRecord& peer);
    Result<PeerRecord> lookup(const NodeID& id) const;
    Result<std::vector<PeerRecord>> peers() const;
    Result<std::vector<PeerRecord>> peers_with_state(PeerState state) const;
    Result<void> remove(const NodeID& id);
    Result<size_t> count() const;
};

class HealthMonitor {
    Result<void> ping(const NodeID& id, TransportSession& session);
    Result<void> pong(const NodeID& id);
    Result<void> suspect(const NodeID& id);
    Result<void> confirm(const NodeID& id);   // peer responded, clear suspicion
    Result<PeerState> state(const NodeID& id) const;
    Result<void> tick();  // check pending PINGs, advance strike counters
};

class Bootstrap {
    Result<std::vector<PeerRecord>> find_seed_peers(
        const std::vector<Endpoint>& seeds);
    Result<std::vector<PeerRecord>> discover_multicast(
        const Endpoint& multicast_group);
};

class GossipProtocol {
    virtual Result<std::vector<GossipMessage>> tick() = 0;  // produce outgoing messages
    virtual Result<void> process(const GossipMessage& msg) = 0;  // handle incoming
};
```

## Consequences
- Discovery Engine is a standalone subsystem. It does not depend on contracts, sessions, or capability resolution.
- MVP UDP HELLO/PING is trivially testable with two processes on localhost.
- PeerRecord doubles as the routing table input (RFC 0015). Routing selects among `addresses[]` based on latency and state.
- Bootstrap via seed list is production-ready; multicast is convenient for LAN but not WAN.
- 3-strike failure detection is simple and adequate for MVP. SWIM suspicion (post-MVP) adds scalability for 1000+ node meshes.
- OFFLINE peers retained for 24 hours prevent unnecessary re-discovery after transient disconnections.
