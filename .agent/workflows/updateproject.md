---
description: Sync session knowledge back to persistent storage.
---

1.  **Update Architecture (SSoT):**
    *   If any new architectural patterns, critical workflows, or system-wide rules were discovered, update `Project_Architecture.md`.
    *   Examples: new synchronization patterns, configuration flows, critical file dependencies, debugging procedures.
    *   This is the **Single Source of Truth** - prioritize updating this over Memory.txt for structural knowledge.

2.  **Update Memory (RAG System):**
    *   Reflect on any new technical patterns learned this session.
    *   Identify the appropriate domain (see `.antigravity/Memory.txt` index):
        - `ui_patterns` - UI design patterns, components, styling
        - `architecture` - System architecture, design patterns, data flows
        - `integration` - External integrations, APIs, services
        - `data` - Data models, database patterns, migrations
        - `testing` - Testing strategies, patterns, fixtures
    *   Update `.antigravity/memory/{domain}.txt` with new patterns.
    *   If a new domain is needed, create the file and register it in `.antigravity/Memory.txt` index.

3.  **Update Status (with Rotation):**
    *   Add new session to top of `.antigravity/Summary.txt`.
    *   **Rotation Check:** If more than 3 sessions exist in Summary.txt:
        - Move the oldest session to `.antigravity/History.txt`.
        - Keep only the last 3 sessions in Summary.txt.
    *   Format: `## Sesión YYYY-MM-DD: [Title]`

4.  **Update Tasks:**
    *   Update `.antigravity/ToDo.txt` (mark completed, add new).

5.  **Orphan Check:**
    *   Scan for any new files created in this session that are not referenced by other files.
    *   Warn the user if any are found.
