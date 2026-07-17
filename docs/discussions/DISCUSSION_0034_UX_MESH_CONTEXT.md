# Discussion 0034 — UX Redesign: Mesh Context, Offline Enrollment, Name-Based CLI

**Date:** 2026-07-17  
**Participants:** @dotlinux26, @D-O-T-Solutions  
**Status:** 🔴 DRAFT — For discussion, NOT for implementation

---

## 1. Vấn đề: `--mesh-dir` UX quá tệ

Hiện tại:

```bash
smo-admin --mesh-dir /var/lib/smo create-mesh SOC-Production
MESH_DIR=$(find /var/lib/smo/meshes ...)
smo-admin --mesh-dir "$MESH_DIR" generate-invite
```

User đã đặt tên `SOC-Production` từ đầu, sao phải `find`?

**Giải pháp:** MeshID chỉ là internal — user làm việc bằng Mesh Name.

```
~/.smo/
    meshes/
        SOC-Production/
            mesh.json        ← MeshID nằm trong đây, user không quan tâm
            registry.db
            authority.pub
            authority.sec
            root.cert
            authority.cert
```

CLI:

```bash
smo mesh create SOC-Production    # tạo ~/.smo/meshes/SOC-Production/
smo mesh use SOC-Production       # set current mesh
smo mesh invite Worker             # dùng current mesh
smo mesh publish                   # dùng current mesh
smo mesh serve start               # dùng current mesh
```

Không `--mesh-dir`, không `find`, không MeshID hex.

---

## 2. Current Mesh Context

```
~/.smo/context.json
{
    "current_mesh": "SOC-Production",
    "user": "admin@company.com",
    "last_used": "2026-07-17T10:00:00Z"
}
```

- `smo mesh list` → show * bên cạnh current
- `smo` (không args) → show prompt với current mesh
- Mọi command tự động dùng current mesh

```
$ smo mesh list
  Dev
* SOC-Production
  Lab
  IoT
```

```
$ smo
Current Mesh: SOC-Production
SOC-Production>
```

---

## 3. `.smo/` trong project (giống `.git/`)

```
cd /opt/app
.smo/
    current_mesh: SOC-Production
```

CLI tự biết mesh đang dùng mà không cần flag.

---

## 4. Hai Mode Join

### Mode 1: Online (HTTP)

```bash
smo join SMO-JOIN-xxxxx
```

→ HTTP POST /enroll → cert → done

Cần `smo mesh serve` đang chạy trên Authority.

### Mode 2: Offline (Clipboard/File/Pipe)

```bash
smo join
```

CLI hỏi:
```
Paste Join Token:
```

User copy token từ `smo mesh invite --copy`.

CLI sinh CSR, hiện:
```
Copy đoạn này gửi cho Authority:
<SMO-CSR-hex...>
```

Authority:
```bash
smo sign --paste
# paste CSR
# → cert copied to clipboard
```

Node:
```
Paste Certificate:
```

Done.

Không cần server. Không cần HTTP. Không cần mở port.

---

## 5. Transport Layer Chung

Cả Online và Offline dùng **cùng Enrollment Protocol**:

```
Join Token  →  Validate  →  CSR  →  Sign  →  Certificate
```

Khác nhau chỉ ở **Transport**:

| Transport | Mode | Khi nào dùng |
|-----------|------|-------------|
| HTTP POST | Online | Có server, muốn auto |
| Clipboard | Offline | Dev, demilitarized zone |
| File | Offline | Air-gapped, USB |
| Pipe | Offline | Scripting |
| QR | Offline | Mobile, IoT |

---

## 6. `serve` Optional

Giống `python -m http.server` — chỉ bật khi cần.

```bash
smo mesh serve start      # bật enroll server
smo mesh serve stop       # tắt
smo mesh serve status     # kiểm tra
```

Hoặc systemd:

```bash
systemctl enable smo-authority
systemctl start smo-authority
```

---

## 7. Lệnh Tổng Thể

### Người mới (Offline)

```bash
smo mesh create SOC-Production
smo mesh invite Worker --copy
# Gửi token qua Zalo/Discord

# Node
smo join
# Paste token
# Paste certificate
Done.
```

### Doanh nghiệp (Online)

```bash
smo mesh create SOC-Production
smo mesh publish
smo mesh serve start

smo mesh invite Worker
# Node
smo join SMO-JOIN-xxxxx
Done.
```

### Hỗn hợp (Air-gapped)

```bash
# Authority (có mạng nội bộ)
smo mesh create SOC-Production
smo mesh invite Worker --copy

# Node (không mạng)
smo join
# paste token
# CSR in ra màn hình
# mang USB sang Authority

# Authority
smo sign --paste
# USB trả về cert

# Node
# paste cert
Done.
```

---

## 8. `smo sign` thay `smo-admin sign`

```
smo sign <csr-file> -o <cert-file>
smo sign --paste           # từ clipboard
```

`smo-admin` sẽ được gộp vào `smo` CLI.

---

## 9. Implementation Plan

### Phase 1 — Context Infrastructure

- `~/.smo/context.json` — current mesh, user, last_used
- `~/.smo/meshes/<name>/` — per-mesh storage
- CLIContextManager đọc/ghi context
- `smo mesh list/use/current`

### Phase 2 — Mesh Commands

- `smo mesh create <name>` — tạo mesh trong `~/.smo/meshes/<name>/`
- `smo mesh publish` — publish current mesh
- `smo mesh invite` — generate invite for current mesh
- `smo mesh serve start/stop/status`

### Phase 3 — `smo-admin` → `smo mesh`

- Chuyển hết logic từ `smo-admin` sang `smo mesh` subcommand
- `smo-admin` thành deprecated alias
- `--mesh-dir` vẫn support cho backward compat

### Phase 4 — Offline Join

- `smo join` interactive mode
- `smo sign --paste`
- Pipe/file transport

### Phase 5 — `.smo/` in project dir

- Auto-detect `.smo/` trong cwd
- Fallback to `~/.smo/`

---

## References

- [RFC 0032 — Context-Aware CLI](../../RFC/0032-context-cli.md)
- [Discussion 0033 — Three-Mode Architecture](../../docs/discussions/DISCUSSION_0033_CLI_THREE_MODE_ARCHITECTURE.md)
- [SPEC.md](../../SPEC.md)
