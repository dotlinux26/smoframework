# RFC 0032 — Context-Aware CLI

**Status:** ACCEPTED  
**Date:** 2026-07-16  
**Authors:** dotlinux26, D-O-T-Solutions

---

## Summary

This RFC defines the **Context-Aware CLI** for SMO — an interactive shell that maintains mesh, selection, execution, and session context, enabling a distributed shell experience.

---

## Motivation

Traditional orchestration tools require repeating targets:

```bash
# Tedious
smo exec --name node-1 --scope mesh hostname
smo exec --name node-1 --scope mesh uptime
smo exec --name node-1 --scope mesh df -h
```

SMO introduces **persistent context**:

```bash
smo context use production
smo select --role Storage

production(storage:12)> ls /data
production(storage:12)> put agent.bin /opt/bin/
production(storage:12)> exec "systemctl restart storage"
```

---

## Context Model

### Three-Layer Context Stack

```
┌─────────────────────────────────────────────────────────────┐
│                    GLOBAL CONTEXT                           │
│  Current Mesh: production                                   │
│  Current User: admin@company.com                            │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                   SELECTION CONTEXT                          │
│  Mesh: production                                           │
│  Role: Storage                                              │
│  Nodes: 12 selected                                         │
│  Filter: trust > 0.8                                        │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                  EXECUTION CONTEXT                           │
│  Control: safe    Scope: mesh                               │
│  Timeout: 30s    Retry: 3                                   │
│  Policy: enterprise-standard                                 │
│  Dry-run: false                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## Context Layers

### 1. Mesh Context

```bash
# List available meshes
smo mesh list

# Switch mesh
smo mesh use production

# Current mesh
smo mesh current
# → production

# Create a new mesh (creates directory, sets as current)
smo mesh create staging

# Publish mesh (via smo-admin under the hood)
smo mesh publish

# Generate invite (calls smo-admin generate-invite)
smo mesh invite --role Worker --expire 1h

# Start enroll server
smo mesh serve --port 5454

# Join a mesh using a token
smo mesh join --token SMO-JOIN-<base64url>
```

### 2. Selection Context

```bash
# Select by role
smo select --role Storage

# Select by tag
smo select --tag Backup

# Select by expression
smo select --where 'trust>0.9 && os=="linux"'

# Select nearest
smo select --nearest

# Combine
smo select --role Worker --tag GPU --where 'mem>32GB'

# View current selection
smo select show

# Save/restore selections
smo select save nightly-backup
smo select load nightly-backup

# Clear
smo select clear
```

### 3. Execution Context

```bash
# Control level
smo control safe      # Read-only, no side effects
smo control normal    # Standard operations
smo control force     # Override some protections
smo control emergency # Authority only, break glass

# Scope
smo scope single      # Single node (pick_one)
smo scope mesh        # All selected nodes
smo scope quorum      # Quorum required
smo scope witness     # Witness nodes only

# Policy
smo policy use enterprise-standard
smo policy show

# Timeout/Retry
smo timeout 60000      # 60 seconds
smo retry 3
```

### 4. Session Context (Persistent Connection)

```bash
smo connect storage-01
# → Connected to storage-01 (production)
# Prompt changes:
production(storage-01)>

# Now all commands target this node
production(storage-01)> ls /var/log
production(storage-01)> put agent.bin /opt/bin/
production(storage-01)> exec "systemctl restart storage"

# Disconnect
smo disconnect
# Back to mesh prompt
production(storage:12)>
```

### 5. Session Context Stack (push/pop)

```bash
# Save current context, switch to new
smo push --role Backup
# → Backup context active

smo pop
# Restores previous context
```

---

## Context-Aware Commands

### Native Commands (Shell built-ins)

| Command | Description |
|---------|-------------|
| `help` | Show help |
| `exit` / `quit` | Exit shell |
| `mesh` | Mesh management |
| `select` | Node selection |
| `policy` | Policy management |
| `control` | Control level |
| `scope` | Execution scope |
| `context` | Context management |
| `connect` | Connect to node |
| `disconnect` | End session |
| `history` | Command history |
| `alias` | Define alias |

### Native Contracts (dispatch via Runtime)

| Command | Contract | Description |
|---------|----------|-------------|
| `ls` | `fs.list` | List directory |
| `mkdir` | `fs.mkdir` | Create directory |
| `rm` | `fs.remove` | Remove file/dir |
| `cp` | `fs.copy` | Copy file |
| `mv` | `fs.move` | Move/rename |
| `cat` | `fs.read` | Read file |
| `put` | `file.put` | Upload file |
| `get` | `file.get` | Download file |
| `sync` | `file.sync` | Sync directories |
| `exec` | `proc.exec` | Execute command |
| `ps` | `proc.ps` | List processes |
| `kill` | `proc.kill` | Kill process |
| `systemctl` | `systemd.control` | Service management |

---

## Interactive Shell (`smo shell`)

```bash
$ smo shell
SMO Runtime v1.0.0

Mesh: production
Selection: Storage (12 nodes)
Policy: enterprise-standard
Control: safe
Scope: mesh

production(storage:12)>
```

### Prompt Format

```
[mesh](selection:count)[control/scope]>
```

Examples:
```
production(storage:12)[safe/mesh]>
production(backup:3)[force/single]>
lab(none)[safe/single]>
```

### Built-in Commands

| Command | Description |
|---------|-------------|
| `help` | Show help |
| `exit` / `quit` | Exit shell |
| `mesh` | Mesh management |
| `select` | Node selection |
| `policy` | Policy management |
| `control` | Control level |
| `scope` | Execution scope |
| `discover` | Discover peers |
| `connect` | Connect to node |
| `disconnect` | End session |
| `history` | Command history |
| `alias` | Define alias |
| `push` / `pop` | Context stack |

---

## Selection Persistence

### Save/Load Selections

```bash
smo select --role Storage --tag Backup --where 'trust>0.9'
# → Selected 18 nodes

smo select save nightly-backup
# → Selection saved as "nightly-backup"

# Later...
smo select load nightly-backup
# → Restored 18 nodes
```

### Persistent Storage

```
~/.smo/
├── contexts/
│   ├── selections/
│   │   ├── nightly-backup.json
│   │   ├── incident-response.json
│   │   └── ...
│   └── execution-contexts/
│       ├── default.json
│       └── incident-response.json
```

---

## Context Stack (Push/Pop)

```bash
# Save current context, switch to incident response
smo push --role IncidentCommander --policy incident-response --control emergency
# → Incident context active

# Work...

smo pop
# Restored previous context
```

---

## Context Stack Structure

```json
{
  "stack": [
    {
      "mesh": "production",
      "selection": {"role": "Storage", "nodes": 12},
      "execution": {"control": "safe", "scope": "mesh"},
      "session": null
    },
    {
      "mesh": "production",
      "selection": {"role": "IncidentCommander", "nodes": 3},
      "execution": {"control": "emergency", "scope": "single"},
      "session": "storage-01"
    }
  ],
  "current": 1
}
```

---

## Completion & UX

### Tab Completion

```
production(storage:12)> ls <TAB>
bin/  boot/  dev/  etc/  home/  lib/  media/  mnt/  opt/  proc/  root/  run/  sbin/  srv/  sys/  tmp/  usr/  var/

production(storage:12)> connect sto<TAB>
storage-01  storage-02  storage-03

production(storage:12)> select --role <TAB>
Storage  Backup  Compute  Gateway
```

### Aliases

```bash
alias ll="ls -la"
alias cls="clear"
alias k="exec kubectl"
alias tf="exec terraform"

# Persisted in ~/.smo/aliases
```

### Scripting

```bash
# deploy.smo
use production
select --role Web
exec "systemctl status nginx"
put nginx.conf /etc/nginx/
exec "systemctl reload nginx"

# Run
smo run deploy.smo

# Or
source deploy.smo
```

---

## Configuration

```toml
# ~/.smo/runtime.toml

[cli]
prompt_template = "{mesh}({selection})[{control}/{scope}]> "
history_file = "~/.smo/history"
history_size = 10000
auto_complete = true
colors = true

[context]
auto_save_selection = true
max_saved_selections = 50
context_stack_size = 10

[mesh]
default = "production"
auto_connect = true
```

---

## Security

- **Context isolation** — Each mesh has isolated secrets, audit logs
- **Audit trail** — Every context switch, selection change, command logged
- **Privilege escalation** — `--control emergency` requires Authority cert
- **Session timeout** — Auto-disconnect after inactivity (configurable)

---

## Implementation Priority

| Phase | Feature |
|-------|---------|
| 1 | Mesh context + Selection context + Basic prompt |
| 2 | Execution context (control, scope, policy) |
| 3 | Session context (`connect`/`disconnect`) |
| 4 | Context stack (`push`/`pop`) |
| 5 | Interactive shell (`smo shell`) |
| 6 | Completion, aliases, history |
| 7 | Scripting (`smo run script.smo`) |
| 8 | Workflow DSL |

---

## C++ Implementation

### Source Files

```
cmd/smo-cli/
├── main.cpp              # CLIApplication with REPL loop + all command handlers
├── cli_context.hpp       # CLIContextManager, SelectionContext, ExecutionContext, SessionContext
├── cli_context.cpp       # Context state management, prompt generation, history
├── intent_parser.hpp     # IntentParser, Intent, ParsedCommand, IntentType enum
├── intent_parser.cpp     # Tokenization, flag parsing, help generation
└── CMakeLists.txt        # Links smo_sdk + smo_tooling + readline stub
```

### CLIContextManager (`cmd/smo-cli/cli_context.hpp`)

```cpp
class CLIContextManager {
public:
    // Mesh context
    Result<void> set_mesh(const std::string& mesh_name);
    Result<std::string> get_current_mesh() const;

    // Selection context (by role/tag/expression/name)
    Result<void> set_selection(const SelectionContext& ctx);
    Result<void> clear_selection();
    Result<SelectionContext> get_selection() const;
    Result<void> save_selection(const std::string& name);
    Result<void> load_selection(const std::string& name);

    // Execution context (control level, scope, timeout, retry)
    void set_control_level(ControlLevel level);
    ControlLevel get_control_level() const;
    void set_scope(ExecutionScope scope);
    ExecutionScope get_scope() const;
    void set_timeout(int ms);
    int get_timeout() const;
    void set_retry(int count);
    int get_retry() const;
    void set_dry_run(bool dry);
    bool get_dry_run() const;

    // Session management
    Result<void> connect(const std::string& node_address);
    Result<void> disconnect();
    bool is_connected() const;

    // Context stack (push/pop)
    void push_context();
    Result<void> pop_context();

    // Prompt generation
    std::string get_prompt() const;

    // History
    void add_history(const std::string& command);
    const std::vector<std::string>& get_history() const;
};
```

### Intent Parser (`cmd/smo-cli/intent_parser.hpp`)

Supports 22+ IntentTypes: `Execute`, `Transfer`, `Filesystem`, `Process`, `Deploy`, `Undeploy`, `Status`, `History`, `Select`, `Policy`, `Control`, `Mesh`, `Connect`, `Disconnect`, `Context`, `Help`, `Exit`, `Discover`, `Export`, `Session`, `Trace`.

Parsing features:
- Long flags (`--flag=value`, `--flag value`)
- Short flags (`-hvfd`)
- Positional args with validation against required args list
- Quoted tokenization (`"` and `'`)
- Escape sequences (`\`)
- Command aliases

### CLIApplication (`cmd/smo-cli/main.cpp`)

**Interactive REPL** — launched when running `smo-cli` without arguments:

```
  ╔══════════════════════════════════════╗
  ║    SMO Interactive Shell v0.1        ║
  ║    Type 'help' for commands          ║
  ║    Type 'exit'  or Ctrl-D to quit    ║
  ╚══════════════════════════════════════╝

[default][none][safe][single]> help
```

**Implemented command handlers:**

| Handler | Intent Types | Actions |
|---------|-------------|---------|
| `handle_help` | Help | Print help/usage for commands |
| `handle_select` | Select | Filter by name/role/tag/where/mesh/OS/arch/trust, save selections |
| `handle_exec` | Execute | Execute command on selected nodes or connected session |
| `handle_transfer` | Transfer | File transfer (get/put/sync) — stub |
| `handle_filesystem` | Filesystem | ls/cat/mkdir/rm/cp/mv/echo — stub |
| `handle_process` | Process | ps/kill/top — stub |
| `handle_deploy` | Deploy | Deploy contract — stub |
| `handle_undeploy` | Undeploy | Undeploy contract — stub |
| `handle_status` | Status | Show current context (mesh, selection, control, scope, session) |
| `handle_policy` | Policy | List/set policy presets: `default`, `enterprise`, `emergency` |
| `handle_control` | Control | Set level/scope/timeout/retry interactively |
| `handle_context` | Context | Save/load/clear context |
| `handle_mesh` | Mesh | List/use/create/leave mesh |
| `handle_connect` | Connect | Connect/disconnect to node |
| `handle_history` | History | Show command history with optional limit |
| `handle_trace` | Trace | Show execution trace — stub |

**Prompt format:**

```
[{mesh}][{selection}][{control}][{scope}]>
```

**Auto-complete:** Tab-completion for all 30+ commands using readline's `rl_completion_matches`.

### Readline Stub (`third_party/readline/`)

Since the system readline library headers are not available (no root access), a minimal stub provides:

```
readline(), add_history(), read_history(), write_history()
rl_completion_matches(), rl_redisplay(), rl_on_new_line()
rl_replace_line(), rl_free_line_state(), rl_cleanup_after_signal()
rl_line_buffer, rl_attempted_completion_function, rl_attempted_completion_over
```

### History Persistence

- History file: `~/.smo_history`
- Loaded on startup via `read_history()`
- Saved on exit via `write_history()`
- In-memory dedup (no consecutive duplicates, max 1000 entries)

### CMakeLists.txt

```cmake
add_executable(smo-cli main.cpp cli_context.cpp intent_parser.cpp
    ${CMAKE_SOURCE_DIR}/third_party/readline/readline.cpp)
target_include_directories(smo-cli PRIVATE ${CMAKE_SOURCE_DIR}/third_party/readline)
target_link_libraries(smo-cli PRIVATE smo_sdk smo_tooling)
```

---

## Implementation Status

| Phase | Feature | Status |
|-------|---------|--------|
| 1 | Mesh context + Selection context + Basic prompt | ✅ Done |
| 2 | Execution context (control, scope, policy) | ✅ Done |
| 3 | Session context (`connect`/`disconnect`) | ✅ Done |
| 4 | Context stack (`push`/`pop`) | ✅ Done |
| 5 | Interactive shell (`smo shell`) | ✅ Done (via `smo-cli`) |
| 6 | Completion, aliases, history | ✅ Done |
| 7 | Scripting (`smo run script.smo`) | ⏳ Planned |
| 8 | Workflow DSL | ⏳ Planned |

---

## References

- [RFC 0028] Contract Runtime
- [RFC 0029] Policy Engine
- [RFC 0031] Mesh Manager
- [RFC 0030] Native Contracts

---

**End of RFC 0032**