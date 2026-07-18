# Discussion 0035 — PKI & Governance: Architecture Chốt

**Date:** 2026-07-17  
**Participants:** @dotlinux26, @D-O-T-Solutions  
**Status:** 🔴 DRAFT — For discussion, NOT for implementation

---

## 1. Root không còn là "private key" — Root là một Node

Sai lầm lớn nhất của các bản trước: Root chỉ là một private key ephemeral.

**Đúng:** Root là một **Node** với role=ROOT, có:

```
NodeID
Certificate (self-signed)
Mesh Manifest
Store (SQLite)
API endpoint
FSM state machine
Event log
```

Root **không** là Authority:
- ❌ Không bỏ phiếu proposal
- ❌ Không tham gia quorum
- ❌ Không ký certificate sau bootstrap (trừ Recovery)
- ✅ Có thể online để phục vụ bootstrap
- ✅ Đồng bộ manifest
- ✅ Cấp bootstrap certificate trong giai đoạn Genesis/Bootstrap
- ✅ Phục vụ Recovery khi được mở khóa

---

## 2. Mesh States

```
Draft       ← đang cấu hình, chưa có mesh trên disk
  ↓
Genesis     ← Root Node duy nhất, mesh vừa được tạo
  ↓
Bootstrap   ← Root + Authority slots đang được lấp đầy
  ↓
Online      ← Bootstrap hoàn tất, Root → Dormant, Governance điều hành
  ↓
Maintenance ← update manifest, cipher suite, runtime upgrade
  ↓
Recovery    ← mất quorum, Root thức dậy
  ↓
Archived    ← mesh bị xóa, chỉ còn audit log
```

---

## 3. Bootstrap Slots — Giải bài toán "Authority đầu tiên từ đâu?"

Không còn "Root tạo authority trực tiếp".

Root tạo **Bootstrap Slots** — những "chiếc ghế chờ".

```
Stage 0 — Root Bootstrap
─────────────────────────
smo mesh create Company
  → Root Node sinh ra
  → mesh.json, recovery.pkg, root.pub
  → Mesh state: Genesis
  → CLI hỏi: "How many bootstrap authorities? [3]"
  → Tạo N Bootstrap Slots:

    Slot #1: state=Waiting
    Slot #2: state=Waiting
    Slot #3: state=Waiting
```

Sau Stage 0, CLI hiện:

```
Genesis created.

Bootstrap Authorities: 3 Slots (Waiting)

Slot #1: SMO-BOOT-XXXX...   ← Join Code cho máy #1
Slot #2: SMO-BOOT-YYYY...   ← Join Code cho máy #2
Slot #3: SMO-BOOT-ZZZZ...   ← Join Code cho máy #3

Mesh State: Bootstrap
```

---

```
Stage 1 — Authority Bootstrap
──────────────────────────────
Máy #2 (máy vật lý khác):
  smo join SMO-BOOT-XXXX...
  → Máy #2 tự sinh ML-DSA keypair (private key không bao giờ rời máy này)
  → Máy #2 sinh CSR
  → Gửi CSR + Slot Token tới Root

Root:
  → Xác thực Slot Token (slot #1 còn Waiting)
  → Root ký CSR
  → Certificate với role=Authority
  → Slot #1: Waiting → Active
  → Authority #1 online

Máy #3:
  smo join SMO-BOOT-YYYY...
  → tương tự → Authority #2

Máy #4:
  smo join SMO-BOOT-ZZZZ...
  → tương tự → Authority #3
```

Khi tất cả slots Active:

```
Bootstrap complete.

Mesh State: Online

3/3 Slots filled.
3 Authorities online.
Quorum: 1/1 (auto-upgrade available).

Root → Dormant
```

---

## 4. Nguyên tắc: Private key không bao giờ rời máy nguồn

Root **không tạo private key cho authority**. Root chỉ:

1. Tạo Bootstrap Slot (một "vé" online)
2. Xác thực slot token
3. Ký CSR — ký lên public key do máy authority tự sinh

Private key của authority **chỉ tồn tại trên máy của authority đó**.

---

## 5. Bootstrap ≠ Expansion

| Giai đoạn | Người ký | Cơ chế |
|-----------|----------|--------|
| Bootstrap (Slot → Active) | Root | Slot Token + CSR |
| Expansion (Authority mới) | Governance | Proposal → Vote → Commit |

Sau bootstrap, Root không ký thêm certificate nào nữa (trừ Recovery).

---

## 6. Hai cấp Governance

### Level A — Membership

Chỉ thay đổi registry, không thay đổi manifest.

```
Action              Quorum (mặc định)
──────────────────────────────────────
Add Authority       2/3  (hoặc floor(N/2)+1)
Remove Authority    2/3
Suspend Authority   2/3
Resume Authority    2/3
```

Không tăng manifest_version, không tăng epoch.

### Level B — Constitution

Thay đổi "Hiến pháp Mesh". Tăng manifest_version + epoch + broadcast.

```
Action              Quorum (mặc định)
──────────────────────────────────────
Change Maximum      3/4
Change Minimum      3/4
Change Quorum       3/4
Change Policy       3/4
Manifest Update     3/4
Runtime ABI         3/4
Unanimous:
  Change CipherSuite   unanimous
  Change Governance Rules  unanimous
  Destroy Mesh         unanimous
  Change Recovery      unanimous
```

---

## 7. Mesh Health

Không block. Không auto-remediate. Chỉ alert + suggest.

```
Healthy:    current >= preferred
Warning:    min <= current < preferred
Critical:   current = min (còn 1, không fault tolerance)
Recovery:   current = 0
```

```
$ smo mesh health

Mesh: Company
State: Online
Authorities: 5 (min=1, preferred=5, max=15)
Online: 4  Offline: 1
Health: ⚠ Warning (below preferred)
Operational: YES (quorum 2/3 satisfied)
Fault tolerance: can still tolerate 1 failure
Risk: LOW
Recommendation: Add one authority to reach preferred.
```

---

## 8. Recovery

### Soft Recovery — vẫn còn quorum

```
Mesh: 5 authority, 3 online, 2 offline nhưng vẫn đạt quorum.

Recovery Mode:
  → User mở recovery package
  → Root Session mở (task-based, không timeout dài)
  → Recovery Proposal (cần quorum recovery riêng)
  → Vote + Root sign
  → Thay authority / thêm authority
  → Root session đóng
```

Recovery Quorum riêng, không dùng Operational Quorum:

```
Operational Quorum: 2/3
Recovery Quorum:    1/2 + Recovery Package
```

### Hard Recovery — mất quorum

```
Mesh: 5 authority, 4 chết, còn 1 → không đạt quorum.

Recovery Mode:
  → User mở recovery package
  → Cảnh báo: "Mesh đang trong tình trạng khẩn cấp"
  → Force Recovery:
      epoch++
      Invalidate ALL certificates
      CRL mới
      Bootstrap authority mới (giống bootstrap lần đầu)
  → Clean slate
  → Root session đóng
```

Hard Recovery luôn là **Clean Slate** — không giữ authority nào vì không biết authority nào đã compromise.

### Recovery Mode không auto

Chỉ cảnh báo, không tự mở.

```
Mesh Lost Quorum
  → Health: CRITICAL
  → Suggestion: smo recovery restore
  → User chủ động mở
```

---

## 9. Manifest hoàn chỉnh

```yaml
schema_version: 1
mesh_id: "abc..."
root_public_key: "ed25519:..."
manifest_version: 1
epoch: 1
state: online

genesis:
  created_at: "2026-07-17T10:00:00Z"
  profile: enterprise
  wizard_version: 1

governance:
  enabled: true
  current_threshold: "2/3"

  authorities:
    minimum:    1
    preferred:  5
    maximum:    15

  # Level A — Membership (registry only, no epoch++)
  membership:
    add_authority:       "2/3"
    remove_authority:    "2/3"
    suspend_authority:   "2/3"
    resume_authority:    "2/3"

  # Level B — Constitution (manifest_version++, epoch++, broadcast)
  constitution:
    change_maximum:      "3/4"
    change_minimum:      "3/4"
    change_quorum:       "3/4"
    change_policy:       "3/4"
    update_manifest:     "3/4"
    upgrade_runtime:     "3/4"
    # Unanimous:
    change_ciphersuite:  "unanimous"
    change_governance:   "unanimous"
    destroy_mesh:        "unanimous"
    change_recovery:     "unanimous"

  fault_tolerance:
    can_tolerate:
      - "2 authority failures"
      - "1 compromised authority"

  revocation:
    freeze_before_revoke: true
    revoke_timeout: 24h

  recovery:
    enabled: true
    soft_quorum: "1/2 + Recovery Package"
    hard: "Recovery Package only (force reset)"
```

---

## 10. So sánh tổng thể

| Aspect | Cũ (hiện tại) | Mới |
|--------|--------------|-----|
| Root | Private key ephemeral | **Node** với role=ROOT, có NodeID/cert/store |
| Root role | Super admin | No voting power, no sign after bootstrap |
| Stage 0 | `create_mesh` tạo authority | Root tạo Bootstrap Slots (waiting seats) |
| Stage 1 | Root ký thẳng | Máy tự sinh keypair → Claim slot → Root ký CSR |
| Private key | Root tạo hộ | **Không bao giờ rời máy nguồn** |
| Mesh states | Không có | Draft → Genesis → Bootstrap → Online → Maintenance → Recovery → Archived |
| Governance | 1 cấp | 2 cấp: Membership + Constitution |
| Quorum | 1/1 | Membership: 2/3, Constitution: 3/4, Unanimous: 5/5 |
| Recovery | 1 kiểu | Soft (còn quorum) + Hard (force reset) |
| Recovery Quorum | — | Riêng, không dùng operational quorum |
| Mesh Health | Không có | Healthy/Warning/Critical/Recovery |
| Auto-remediate | — | **Không** — chỉ alert + suggest |
| Authority weight | — | **1 authority = 1 vote** (giữ đơn giản) |
| Conflict resolution | — | 1 proposal/resource, proposal còn lại → Conflicted |
| Restart Root | — | Root Dormant, không cần chạy liên tục |

---

## 11. Q1–Q17 Đã chốt

### Q1: Membership và Constitution quorum khác nhau?
✅ **Có, nên tách.** Membership 2/3, Constitution 3/4, Unanimous 5/5.

### Q2: Action nào unanimous?
**CipherSuite, Governance Rules, Destroy Mesh, Recovery Rules.** Đây là "Hiến pháp" — thay đổi cần toàn bộ đồng thuận.

### Q3: Root Session timeout?
**Task-based, không timeout dài.** Mở → Authenticate → Execute → Destroy. Không có "Recovery Shell" 30 phút.

### Q4: Recovery Package format?
Hỗ trợ **cả 3**: USB / QR / Clipboard. Recovery Code dạng `SMO-REC-XXXX`.

### Q5: Min/Preferred/Max thuộc cấp nào?
- **Minimum → Constitution** (ảnh hưởng quorum)
- **Preferred → Membership** (chỉ là recommendation)
- **Maximum → Constitution**

### Q6: Soft Recovery quorum?
**Recovery Quorum riêng**, không dùng Operational Quorum. Ví dụ: `1/2 + Recovery Package`.

### Q7: Hard Recovery giữ authority nào không?
**Không.** epoch++ invalidate ALL. Clean slate — không biết authority nào compromised.

### Q8: Recovery Mode auto?
**Không.** Chỉ cảnh báo + suggest. User chủ động mở.

### Q9: Auto Remediation?
**Không.** SMO không tự Add Authority, Rotate Cert... Chỉ Alert → Suggestion → Health Report. Con người quyết.

### Q10: Authority ≠ Seed enforce?
**Không enforce.** Role orthogonal. Node có thể `Authority + Seed` hoặc `Authority, không Seed`.

### Q11: Auto Upgrade Quorum?
**Không.** Chỉ Suggest. User quyết.

### Q12: Bootstrap Authority qua Governance?
**Không.** Bootstrap là Genesis Session, tất cả authority sinh trong Bootstrap. Sau Bootstrap Complete → Governance bật. Authority #4 mới phải qua Proposal.

### Q13: Recovery cho phép replace 1 authority?
✅ **Cả hai.** `Recovery One` (replace 1 authority) và `Recovery Mesh` (rebuild toàn bộ). Không bắt rebuild hết.

### Q14: Mesh Health display?
✅ **100%.** Màn hình health đẹp: State, Online/Offline, Operational, Tolerance, Risk, Quorum, Recommendation.

### Q15: Authority weight?
**Không hỗ trợ phiên bản đầu.** 1 authority = 1 vote. Đơn giản, dễ kiểm toán.

### Q16: Maximum authorities hardcode?
**Không.** Configurable trong manifest. Validator chỉ check `min ≤ preferred ≤ max`.

### Q17: Conflict proposal?
✅ **Cần cơ chế:**
- Conflict detection theo resource (Authority, Policy, Config...)
- 1 proposal/resource ở trạng thái active
- Proposal còn lại → `Conflicted` hoặc `Blocked`

---

## 12. Thứ tự triển khai

### Phase 1 — Root as Node
- Root không còn là private key ephemeral — là Node với role=ROOT
- Root có NodeID, cert self-signed, store, API
- Root Dormant state

### Phase 2 — Bootstrap Slots
- Slot struct: Waiting → Active (hoặc Expired)
- Slot Token generation + verification
- Root ký CSR khi slot token hợp lệ
- `smo mesh create` → Stage 0 + Stage 1 wizard

### Phase 3 — Mesh States
- State machine: Draft → Genesis → Bootstrap → Online → Maintenance → Recovery → Archived
- State transitions có điều kiện (vd: Bootstrap → Online khi all slots filled)

### Phase 4 — Two-level Governance
- Membership vs Constitution trong GovernanceEngine
- Membership: registry only, no manifest++, no epoch++
- Constitution: manifest_version++, epoch++, broadcast
- Quorum khác nhau cho mỗi cấp

### Phase 5 — Mesh Health
- Health levels: Healthy / Warning / Critical / Recovery
- So sánh current state vs template
- Alert + suggest, không auto-remediate

### Phase 6 — Recovery
- Soft Recovery (quorum + Recovery Package + Root sign)
- Hard Recovery (force reset, epoch++, invalidate all)
- Recovery Quorum riêng

### Phase 7 — Conflict Resolution
- Conflict detection per resource
- Block conflicted proposals
- Dashboard display

---

## References

- [Discussion 0034 — UX Mesh Context](DISCUSSION_0034_UX_MESH_CONTEXT.md)
- [Discussion 0033 — Three-Mode Architecture](DISCUSSION_0033_CLI_THREE_MODE_ARCHITECTURE.md)
- [RFC 0016 — Governance Protocol](../../RFC/0016-governance.md)
- [RFC 0018 — Mesh Manifest](../../RFC/0018-mesh-manifest.md)
- [`core/governance/governance.hpp`](../../core/governance/governance.hpp)
- [`core/authority/authority.hpp`](../../core/authority/authority.hpp)
- [`core/authority/registry.hpp`](../../core/authority/registry.hpp)
- [`core/fsm/fsm.hpp`](../../core/fsm/fsm.hpp)
