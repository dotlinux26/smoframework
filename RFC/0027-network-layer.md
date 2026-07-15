# RFC 0027 — Network Layer: Bootstrap, Heartbeat, Gossip, PeerStore, MembershipSync

**Status:** ACCEPTED
**Date:** 2026-07-16
**Authors:** dotlinux26, D-O-T-Solutions

---

## Summary

This RFC defines the **Network Layer** that sits between the Connectivity Layer (STUN/ICE/NAT) and the Session Layer. It provides the glue that makes a distributed mesh operational: bootstrap discovery, persistent peer caching, liveness detection, membership synchronization, and epidemic gossip.

The Network Layer is the missing link that transforms SMO from a single-node framework into a true distributed mesh runtime.

---

## Motivation

Prior to this RFC, SMO had:
- Discovery Engine with local `MembershipTable` only
- TCP transport for Control/Execution/Data protocols
- Placeholder `smo node connect` that did nothing
- No peer persistence across restarts
- No heartbeat, no RTT, no gossip

Real-world mesh operation requires:
1. **Bootstrap** — new nodes must discover the mesh via seed nodes
2. **Persistence** — peer table survives node restarts
3. **Liveness** — PING/PONG with RTT for failure detection and routing
4. **Sync** — typed membership event bus for decoupled components
5. **Gossip** — epidemic membership propagation for scalability

Without these, SMO cannot operate beyond a single Docker bridge network.

---

## Design

### Layer Positioning

```
┌─────────────────────────────────────┐
│ CONNECTIVITY (STUN/ICE/NAT/TURN)    │  — produces connected socket
├─────────────────────────────────────┤
│ NETWORK LAYER (this RFC)            │
│   Bootstrap                         │
│   PeerStore (SQLite)                │
│   HeartbeatService (UDP)            │
│   MembershipSync (event bus)        │
│   GossipEngine (SWIM)               │
├─────────────────────────────────────┤
│ SESSION (keys, certs, caps)         │
├─────────────────────────────────────┤
│ PROTOCOL (Discovery/Control/Exec)   │
├─────────────────────────────────────┤
│ RUNTIME (FSM/Scheduler/Executor)    │
└─────────────────────────────────────┘
```

**Key rule:** Network Layer is **protocol-aware** (knows about PeerRecord, MembershipEvent, HELLO/WELCOME messages) but **runtime-agnostic** (never calls Executor, never touches Contracts).

---

## Components

### 6.1 Bootstrap (`core/discovery/discovery.cpp::Bootstrap`)

**Purpose:** Connect a fresh node to the mesh via seed nodes.

**API:**
```cpp
class Bootstrap {
public:
    // Try each seed until one responds with WELCOME
    static Result<PeerRecord> find_seed(
        const std::vector<Endpoint>& seeds,
        Transport& transport,
        const NodeID& local_id,
        int64_t now_ns);
};
```

**Flow:**
1. For each seed endpoint (from CLI `--seed` or Mesh Manifest):
   - TCP connect
   - Send `HelloMsg` (local NodeID, protocol version)
   - Await `WelcomeMsg` (seed's `PeerRecord`)
   - If success: return seed's `PeerRecord`
2. If all fail → `BootstrapNoSeeds` error

**Message Formats:**
```cpp
struct HelloMsg {
    NodeID    node_id;
    uint64_t  pubkey_fingerprint;
    uint16_t  protocol_version = 1;
};

struct WelcomeMsg {
    NodeID       node_id;        // seed's NodeID
    PeerRecord   peer_record;    // seed's full record
};
```

**Wire:** `DISCOVERY` namespace (0x01), HELLO=0x01, WELCOME=0x02

---

### 6.2 PeerStore (`core/discovery/PeerStore`)

**Purpose:** SQLite-backed persistent peer cache. Survives node restarts.

**Schema (`peer.db`):**
```sql
CREATE TABLE peers (
    node_id         BLOB PRIMARY KEY,        -- 32 bytes
    display_name    TEXT NOT NULL DEFAULT '',
    hostname        TEXT NOT NULL DEFAULT '',
    mesh_name       TEXT NOT NULL DEFAULT '',
    role            INTEGER NOT NULL DEFAULT 3,
    tags            TEXT NOT NULL DEFAULT '[]',   -- JSON
    platform        TEXT NOT NULL DEFAULT '',
    arch            TEXT NOT NULL DEFAULT '',
    version         TEXT NOT NULL DEFAULT '',
    location        TEXT NOT NULL DEFAULT '',
    aliases         TEXT NOT NULL DEFAULT '[]',   -- JSON
    endpoint_scheme TEXT NOT NULL DEFAULT 'tcp',
    endpoint_host   TEXT NOT NULL DEFAULT '',
    endpoint_port   INTEGER NOT NULL DEFAULT 0,
    state           INTEGER NOT NULL DEFAULT 0,
    last_seen       INTEGER NOT NULL DEFAULT 0,
    ping_misses     INTEGER NOT NULL DEFAULT 0,
    rtt_ms          REAL NOT NULL DEFAULT 0.0,
    created_at      INTEGER NOT NULL DEFAULT 0,
    updated_at      INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE peer_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    node_id     BLOB NOT NULL,
    event_type  INTEGER NOT NULL,   -- MembershipEventType
    payload     BLOB,
    created_at  INTEGER NOT NULL
);
```

**Indexes:** `display_name`, `state`, `role`, `mesh_name`

**API:**
```cpp
class PeerStore {
public:
    Result<void> open(std::string_view base_path);  // opens peer.db
    void close();

    Result<void> upsert(const PeerRecord& rec);
    Result<PeerRecord> lookup(const NodeID& id) const;
    Result<PeerRecord> lookup_by_name(std::string_view name) const;
    Result<std::vector<PeerRecord>> peers() const;
    Result<void> remove(const NodeID& id);

    // Filtered queries (for Selector)
    Result<std::vector<PeerRecord>> peers_by_role(Role role) const;
    Result<std::vector<PeerRecord>> peers_by_tag(std::string_view tag) const;
    Result<std::vector<PeerRecord>> peers_by_os(std::string_view os) const;
    Result<std::vector<PeerRecord>> peers_by_arch(std::string_view arch) const;
    Result<std::vector<PeerRecord>> peers_by_mesh(std::string_view mesh) const;
    Result<std::vector<PeerRecord>> peers_by_state(PeerState state) const;

    Result<void> sync_from_membership(const MembershipTable& table);
    Result<void> sync_to_membership(MembershipTable& table) const;

    Result<void> record_event(PeerEventType type, const NodeID& id, Bytes payload = {});
    Result<std::vector<PeerEvent>> recent_events(int64_t since_id = 0) const;

    Result<size_t> count() const;
};
```

**Sync Strategy:**
- On startup: `PeerStore::open()` → `sync_to_membership()` → populates `MembershipTable`
- On bootstrap: `sync_from_membership()` → persists seed + discovered peers
- On membership events: `record_event()` → audit trail

---

### 6.3 UDP Transport (`core/network/udp/UdpTransport`)

**Purpose:** Connectionless transport for Discovery/Heartbeat/Gossip.

**API:**
```cpp
class UdpTransport : public Transport {
public:
    std::string_view name() const override { return "udp"; }
    Result<ListenerPtr> listen(const Endpoint& ep) override;
    Result<SessionPtr> connect(const Endpoint& ep) override;
};

class UdpSession : public TransportSession {
public:
    Result<void> send(BytesView data) override;       // sendto()
    Result<Bytes> recv(size_t max_bytes) override;    // recvfrom()
    Result<void> close() override;
    Endpoint remote_endpoint() const override;
    bool is_open() const override;
};

class UdpListener : public TransportListener {
public:
    Result<std::unique_ptr<TransportSession>> accept() override;
    Result<void> close() override;
    Endpoint local_endpoint() const override;
};
```

**Characteristics:**
- Non-blocking sockets + `epoll`/`poll` for scalability
- Per-peer sessions via `connect()` (sets default destination) or `accept()` (peels first packet)
- Max payload: 65507 bytes (UDP max - IP/UDP headers)
- Schemes: `"udp"` in `Endpoint`

**Registered with TransportRegistry:**
```cpp
TransportRegistry::instance().register_transport(
    std::make_unique<UdpTransport>(), "udp");
```

---

### 6.4 HeartbeatService (`core/network/udp/HeartbeatService`)

**Purpose:** Liveness detection, RTT measurement, failure detection via PING/PONG over UDP.

**Config:**
```cpp
struct HeartbeatConfig {
    uint32_t ping_interval_ms = 5000;    // send PING every 5s
    uint32_t ping_timeout_ms  = 3000;    // wait 3s for PONG
    uint32_t max_misses       = 3;       // 3 misses = SUSPECT
};
```

**Messages:**
```cpp
struct PingMsg {
    int64_t timestamp;      // sender monotonic ns
    uint64_t sequence;      // monotonic per-sender
};

struct PongMsg {
    int64_t timestamp;      // echo of PingMsg.timestamp
};
```

**Operation:**
1. Every `ping_interval_ms`, send `PingMsg{timestamp=now_ns, sequence++}` to all `Online` peers via UDP
2. Receiver responds immediately with `PongMsg{timestamp=ping.timestamp}`
3. Sender computes RTT = `now_ns - pong.timestamp`, updates `PeerRecord.rtt_ms` (moving average)
4. `HealthMonitor::tick()` advances state: `Online → Suspect → Offline` based on `max_misses`

**Integration:**
- `SelectionEngine` uses `rtt_ms` for `NEAREST` mode
- `RoutingLayer` uses `rtt_ms` for path cost
- `TrustEngine` uses heartbeat success rate for Citizen Score

---

### 6.5 MembershipSync (`core/network/sync/MembershipSync`)

**Purpose:** Typed event bus for all membership changes. Decouples network components from membership state.

**Event Types:**
```cpp
enum class MembershipEventType : uint8_t {
    PeerAdded        = 1,   // new peer discovered/bootstrapped
    PeerRemoved      = 2,   // graceful OFFLINE or confirmed dead
    PeerUpdated      = 3,   // role/cap/endpoint/rtt change
    PeerRenamed      = 4,   // display_name change
    CapabilityChange = 5,   // role/caps added/removed
    CertificateRotate = 6,  // cert re-issued
    StateChange      = 7,   // Online→Suspect→Offline transitions
};
```

**MembershipEvent:**
```cpp
struct MembershipEvent {
    MembershipEventType type;
    NodeID              node_id;
    int64_t             timestamp_ns = 0;
    uint64_t            sequence = 0;

    // Event-specific payload
    std::string old_display_name;
    std::string new_display_name;
    Role        new_role = Role::Reader;
    std::vector<std::string> added_caps;
    std::vector<std::string> removed_caps;
    PeerState  new_state = PeerState::Unknown;
    Certificate new_cert;
};
```

**Publish/Subscribe:**
```cpp
class MembershipSync {
public:
    using Callback = std::function<void(const MembershipEvent&)>;

    uint64_t subscribe(Callback cb);  // returns subscription_id
    void unsubscribe(uint64_t id);

    // Emit helpers (called by DiscoveryEngine, HeartbeatService, etc.)
    void emit_peer_added(const PeerRecord& rec);
    void emit_peer_removed(const NodeID& id);
    void emit_peer_updated(const PeerRecord& old_rec, const PeerRecord& new_rec);
    void emit_peer_renamed(const NodeID& id, const std::string& old_name, const std::string& new_name);
    void emit_capability_changed(const NodeID& id, Role old, Role nw,
                                  const std::vector<std::string>& added,
                                  const std::vector<std::string>& removed);
    void emit_certificate_rotated(const NodeID& id, const Certificate& cert);
    void emit_state_changed(const NodeID& id, PeerState old, PeerState nw, int misses);

    // Gossip serialization
    Bytes serialize_events(const std::vector<MembershipEvent>&) const;
    Result<void> apply_events(const Bytes& data);
    std::vector<MembershipEvent> pending_events(uint64_t since_seq) const;
    void acknowledge(uint64_t seq);
};
```

**Consumers:**
- `HeartbeatService` → `emit_state_changed()`
- `DiscoveryEngine` → `emit_peer_added()`, `emit_peer_removed()`
- `GossipEngine` → `pending_events()` for push, `apply_events()` for pull
- `PeerStore` → persistence via `record_event()`

---

### 6.6 GossipEngine (`core/discovery/GossipEngine`)

**Purpose:** Epidemic membership dissemination using SWIM-inspired protocol.

**Config:**
```cpp
struct Config {
    uint32_t interval_ms = 5000;   // gossip round interval
    uint32_t fanout = 3;           // peers per round
    uint32_t max_payload = 4096;   // max gossip message size
};
```

**Gossip Message:**
```cpp
struct GossipMessage {
    uint64_t incarnation;    // local generation counter
    uint64_t sequence;       // monotonic per-sender
    Bytes payload;           // serialized MembershipEvent list
};
```

**Operation:**
1. Every `interval_ms`, `tick()` selects `fanout` random `Online` peers
2. Sends `GossipMessage` with `pending_events(since_last_sent)` to each
2. On receive: `apply_updates()` → merges into `MembershipTable`
3. Increments `incarnation` on local state change

**Gossip Flow:**
```
Node A (incarnation=5)          Node B
      │                            │
      │ Gossip(seq=10, events...) │
      ├───────────────────────────▶│
      │                            │ apply_updates()
      │                            │ membership += events
      │                            │
      │          Gossip(seq=7)     │
      │◀───────────────────────────┤
      │ apply_updates()            │
      │                            │
```

---

## Integration Points

### DiscoveryEngine
```cpp
// Bootstrap
auto rec = Bootstrap::find_seed(seeds, transport, local_id, now);
if (rec) {
    handle_welcome(WelcomeMsg{local_id, rec.value()}, now);
    peer_store.sync_from_membership(membership);
}

// Message handlers
handle_hello(HelloMsg, from, now)    → upsert peer, send Welcome
handle_welcome(WelcomeMsg, now)      → upsert seed, request Discover
handle_discover(DiscoverMsg, now)    → send NodeInfo (full table)
handle_node_info(NodeInfoMsg, now)   → merge peer table
handle_ping(PingMsg, now)            → record ping, send Pong
handle_pong(PongMsg, now)            → update RTT, health
```

### HeartbeatService Integration
```cpp
void tick(int64_t now_ns) {
    send_ping_all(now_ns);
    check_health(now_ns);  // uses HealthMonitor.tick()
}

void send_ping_all(int64_t now_ns) {
    for (auto& rec : membership.peers_with_state(Online)) {
        health.record_ping(rec.node_id, now_ns);
        udp.send(PingMsg{now_ns, ++seq}, rec.endpoint);
    }
}

void handle_pong(from, msg, now_ns) {
    auto rec = membership.lookup_by_endpoint(from);
    if (rec) {
        health.record_pong(rec->node_id, now_ns);
        rec->rtt_ms = (now_ns - msg.timestamp) / 1e6;
        membership.upsert(*rec);
    }
}
```

### GossipEngine Integration
```cpp
void tick(int64_t now_ns) {
    if (now_ns - last_gossip_ns < interval) return;
    last_gossip_ns = now_ns;
    incarnation_++;

    auto events = sync.pending_events(last_gossip_seq);
    if (events.empty()) return;

    auto targets = select_fanout_peers();
    for (auto& ep : targets) {
        send_gossip(ep, events);
    }
}

void send_gossip_to_peer(const Endpoint& target, const std::vector<MembershipEvent>& events) {
    GossipMessage msg{incarnation_, ++local_seq_, serialize_events(events)};
    udp.connect(target).value()->send(msg.serialize());
}
```

### MembershipSync → PeerStore
```cpp
void emit_peer_added(const PeerRecord& rec) {
    event_log.push_back({PeerAdded, rec.node_id, now_ns, ++seq});
    peer_store.record_event(PeerEventType::Added, rec.node_id, serialize(rec));
}
```

---

## Configuration

### Mesh Manifest (Heartbeat Section)
```toml
[network.heartbeat]
interval_sec = 5
timeout_sec = 3
max_misses = 3
fanout = 3

[network.bootstrap]
seeds = [
    "seed1.mesh.example.com:7777",
    "seed2.mesh.example.com:7777"
]
# Or DNS SRV: _smo._tcp.mesh.example.com
```

### Node Config
```toml
[node]
data_dir = "/var/lib/smo"
mesh_name = "SOC-Production"

[network]
listen_tcp = "0.0.0.0:7777"
listen_udp = "0.0.0.0:7777"
```

---

## Error Codes

| Code | Category | Description |
|------|----------|-------------|
| 406 | Discovery | `BootstrapNoSeeds` — no seed endpoints provided |
| 407 | Discovery | `BootstrapFailed` — all seed connections failed |
| 408 | Discovery | `SeedTimeout` — seed did not respond in time |
| 307 | Transport | `AddressInUse` — UDP/TCP bind failed |
| 400 | Discovery | `PeerNotFound` — peer not in membership table |
| 403 | Discovery | `PeerTimeout` — peer heartbeat timeout |
| 402 | Discovery | `PeerSuspect` — peer marked suspect |

---

## Implementation Order

| Sprint | Deliverable |
|--------|-------------|
| 4.1 | PeerStore (SQLite) + sync helpers |
| 4.2 | Bootstrap (HELLO/WELCOME/DISCOVER/NODE_INFO) |
| 4.3 | UDP Transport + HeartbeatService |
| 4.4 | MembershipSync event bus |
| 4.5 | GossipEngine (SWIM) |
| 4.6 | CLI/Selector wiring + smo-node integration |

---

## Testing

| Test | Description |
|------|-------------|
| `test_bootstrap` | Seed connect → WELCOME → peer table populated |
| `test_peerstore_crud` | upsert/lookup/remove/filter queries |
| `test_heartbeat_rtt` | PING/PONG → RTT updated |
| `test_health_transitions` | Online → Suspect → Offline after misses |
| `test_gossip_propagation` | 3 nodes, event fanout → all converge |
| `test_membership_sync` | Event bus → PeerStore persistence |
| `test_selector_integration` | `smo exec --name X --dry-run` → NodeSet |

---

## Future Extensions

| Extension | Description |
|-----------|-------------|
| STUN (RFC 8489) | `transport/stun/client.cpp` — discover public IP:port |
| ICE (RFC 8445) | `transport/ice/gatherer.cpp` — candidate gathering |
| NAT Hole Punch | `transport/nat/punch.cpp` — UDP hole punching |
| TURN (RFC 8656) | `transport/relay/turn.cpp` — relay fallback |
| Gossip Compression | Delta sync for large meshes |
| PeerStore Vacuum | GC old `peer_events`, compact DB |

---

## Backward Compatibility

- No changes to existing Control/Execution/Data protocols
- Transport Registry already supports scheme-based dispatch
- `DiscoveryEngine` message handlers unchanged (new handlers added)
- `MembershipTable` API unchanged (new `sync` methods added)

---

## References

- [SWIM: Scalable Weakly-consistent Infection-style Process Group Membership](https://www.cs.cornell.edu/projects/Quicksilver/public_pdfs/SWIM.pdf)
- RFC 8489 — STUN
- RFC 8445 — ICE
- RFC 8656 — TURN
- SPEC.md §VI — Networking Architecture