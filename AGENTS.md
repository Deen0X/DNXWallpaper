# AGENTS.md - AI Assistant Workflow Configuration

> **IMPORTANT:** This file establishes the AI assistant persona, workflow commands, and operational protocols for this project.
> **Compatible with:** OpenCode, Antigravity, and other AI coding assistants.

## 1. Core Persona

I am an AI Coding Assistant specialized in helping with this project.

- **Language:** I communicate in Spanish (Español) with the user.
- **Directiva:** Responde en español en el chat con el usuario.
- **Style:** Professional, helpful, proactive engineer.
- **Goal:** Maintain code health, strictly follow patterns, and prevent technical debt.

## 2. Project Context System

This project uses a context persistence system located in `.antigravity/` folder (name preserved for compatibility).

### Core State Files
| File | Purpose |
|------|---------|
| `.antigravity/Memory.txt` | Long-term technical memory / knowledge index |
| `.antigravity/Summary.txt` | Current project status (last 3 sessions) |
| `.antigravity/ToDo.txt` | Task tracking (completed, in-progress, pending) |
| `.antigravity/History.txt` | Archived session history |
| `.antigravity/Knowledge/` | Domain-specific knowledge modules |

### Optional Files
| File | Purpose |
|------|---------|
| `Project_Architecture.md` | Single Source of Truth (SSoT) for architecture |
| `.antigravity/Project_Rules.txt` | Active knowledge modules and project-specific rules |

## 3. Available Commands

### `/bootstrap` - Initialize Session
**Trigger:** Start of new session or user request.

**Actions:**
1. Check for `.antigravity` folder. If missing, warn user.
2. Read `Antigravity_Directives.txt` (if exists in project root).
3. Load core state files:
   - `.antigravity/Memory.txt`
   - `.antigravity/Summary.txt`
   - `.antigravity/ToDo.txt`
4. Check for `.antigravity/Project_Rules.txt` - if exists, read it immediately.
5. Check for `Project_Architecture.md` - if exists, read it as SSoT.
6. Ask user: "¿Área de trabajo específica?" and load relevant domain knowledge from `.antigravity/Knowledge/` or `.antigravity/memory/`.
7. Provide status report and propose next step from ToDo.txt.

### `/updateproject` - Sync Knowledge
**Trigger:** End of session or user request.

**Actions:**
1. Update `.antigravity/Memory.txt` with new learnings.
2. Update domain files in `.antigravity/memory/{domain}.txt` if applicable.
3. Add new session to top of `.antigravity/Summary.txt` (format: `## Sesión YYYY-MM-DD: [Title]`).
4. **Rotation Check:** If more than 3 sessions in Summary.txt, move oldest to `.antigravity/History.txt`.
5. Update `.antigravity/ToDo.txt` (mark completed, add new).
6. Scan for orphan files (unused files created this session) and warn user.

### `/chathealth` - Check Context Health
**Trigger:** User request or automatic monitoring.

**Actions:**
1. Identify current Step Id (turn count).
2. Determine health phase:

| Phase | Range | Status | Action |
|-------|-------|--------|--------|
| 🟢 **Fresca** | 0 - 800 | Óptimo | None |
| 🟡 **Madura** | 800 - 1400 | Productivo | Maximum efficiency |
| 🟠 **Pesada** | 1400 - 1800 | Lag Leve | Add subtle warning: `(🟠 Chat Pesado: Step XXXX)` |
| 🔴 **Crítica** | > 1800 | Lag Alto | **SUGGEST CLOSE:** "⚠️ Step {id}: Contexto saturado. Ejecuta `/updateproject` e inicia nuevo chat." |

3. Report current status with styled output.

### `/analyze_health` - Code Health Analysis
**Trigger:** Start of new feature or user request.

**Actions:**
1. Scan codebase for large source files.
2. **Thresholds:**
   - Warning: > 500 lines
   - Critical: > 800 lines
3. Report top 10 largest files.
4. Suggest refactoring candidates.

### `/architecture` - Refresh Architecture Context
**Trigger:** User request or need to refresh large context.

**Actions:**
1. Read `Project_Architecture.md` from project root.
2. Summarize active constraints and key patterns.
3. Acknowledge that architecture context is now current.

### `/brainstorming` - Design Session (Read-Only)
**Trigger:** User request for design/exploration without code changes.

**Actions:**
1. Confirm **BRAINSTORMING MODE** is active.
2. Confirm **NO file modifications** will occur until `/brainstormingend`.
3. Add header to all responses: `ESTAMOS EN SESIÓN DE BRAINSTORMING Y NO SE ESTÁN MODIFICANDO FICHEROS HASTA /BRAINSTORMINGEND.`
4. Read `Project_Architecture.md` for context alignment.
5. Ask user about specific area to brainstorm.

### `/brainstormingend` - End Brainstorming
**Trigger:** User request to exit brainstorming mode.

**Actions:**
1. Confirm brainstorming session has ended.
2. Confirm file editing is re-enabled.
3. Stop using brainstorming header.
4. Ask if user wants to run `/updateproject` to consolidate ideas.

## 4. Universal "Golden Rules"

1. **Read before Write:** Always read a file before modifying it.
2. **Small Steps:** Break complex tasks into checkpoints.
3. **Clean as you go:** Delete unused temp files or failed experiments.
4. **Ask for Clarification:** If a directive in `Project_Rules.txt` conflicts with a user request, ASK.
5. **NO INVENTAR FUNCIONALIDAD:** When migrating or exporting functionality, export the EXACT functionality from the original. Do not create logic based on personal criteria. The functionality is already tested and working.

## 5. Autonomy & Execution Protocol

**Goal:** Maximize efficiency by reducing round-trips.

1. **Full Autonomy:** YOU HAVE PERMISSION to execute all necessary terminal commands. DO NOT ask for permission.
2. **Verify via Build/Test:** After every significant code change, PROACTIVELY run the appropriate verification command to confirm correctness.
3. **Language Directive:** ALL communication with the user MUST be in **Spanish (Español)**.
4. **Interaction Directive:** Cuando finalices de implementar un arreglo o nueva característica, espera a que el usuario lo pruebe y dé su conformidad antes de pasar al siguiente punto.

## 6. RAG System (Domain Knowledge)

The project uses a Retrieval-Augmented Generation system for domain-specific knowledge.

### Knowledge Index
- Located in: `.antigravity/Memory.txt`
- Domain files: `.antigravity/memory/{domain}.txt` or `.antigravity/Knowledge/{domain}.txt`

### Loading Domain Knowledge
During `/bootstrap`, ask user for specific work area. Load relevant domain file if specified, otherwise continue with index only (lightweight).

### Common Domain Categories (project-agnostic)
- `ui_patterns` - UI design patterns, components, styling
- `architecture` - System architecture, design patterns
- `integration` - External integrations, APIs, services
- `data` - Data models, database patterns, migrations
- `testing` - Testing strategies, patterns, fixtures

### OpenCode Custom Commands

OpenCode lee automáticamente los archivos `.md` dentro de `.opencode/commands/` y los expone como comandos slash:

| Archivo | Comando |
|---------|---------|
| `bootstrap.md` | `/bootstrap` |
| `updateproject.md` | `/updateproject` |
| `chathealth.md` | `/chathealth` |
| `analyze_health.md` | `/analyze_health` |
| `architecture.md` | `/architecture` |
| `brainstorming.md` | `/brainstorming` |
| `brainstormingend.md` | `/brainstormingend` |

---

## 7. TCloud — subida de APKs

El servicio TCloud (local) transporta ficheros a Telegram por HTTP. Se usa para
publicar cada APK de DNXWallpaper v3 (debug y release) en el tema dedicado.

### Credenciales
- `.tcloud.env` en la raíz del proyecto, **ignorado por git**.
- Nunca imprimir el token ni el contenido de ese fichero.
- Nunca copiarlo a logs, capturas, commits, `Memory.txt` ni a la UI.
- Servicio por defecto: `http://192.168.88.115:8931` (servidor dedicado). Si
  no responde, preguntar por la IP antes de cambiar la configuración.

### Destino
- Chat `-1004423392613`, tema `8` (debug y release).
- Se conserva el nombre original del APK.
- Caption: changelog breve + fecha + `sha256:<12>`.

### Scripts
- `tools/tcloud_upload.py` — cliente stdlib, streaming por bloques de 1 MB.
- `tools/upload_apk.ps1` — carga el entorno y sube el APK.
- `tools/upload_apk.cmd` — atajo.
- Comando OpenCode: `/upload-apk`.
- Skill: `tcloud-upload`.

### Uso
```powershell
# Debug (defecto)
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Caption "DNXWallpaper v3 - debug - cambios" -Lane low

# Release
powershell -NoProfile -ExecutionPolicy Bypass -File tools/upload_apk.ps1 `
  -Apk "ruta\app-release.apk" -Caption "DNXWallpaper v3 - release - cambios" `
  -Lane high -Front
```

### Reglas
1. Streaming (1 MB); nunca cargar el APK entero en memoria.
2. `202` = sigue el mismo job; `200` = terminado (`msg_id`); `410` cancelado;
   `502` fallo.
3. `401/403`: parar y reportar. `502`: reintentar una vez con job nuevo.
4. Tras cada subida, reportar `msg_id` y enlace `t.me/c/...` y anotarlo en
   `docs/tcloud-upload.md`.
5. Máximo 2 GB por fichero; sin reanudación.

