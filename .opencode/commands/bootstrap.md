---
description: Initialize the session by loading context and Project Rules.
---

1. **Read Universal Directives:**
   - Read `Antigravity_Directives.txt` (project-agnostic seed).

2. **Load Core Architecture:**
   - Read `Project_Architecture.md` (SSoT - ALWAYS).

3. **Load Recent Context:**
   - Read `.antigravity/Summary.txt` (last 3 sessions).
   - Read `.antigravity/ToDo.txt`.

4. **Domain-Specific Knowledge (RAG System):**
   - Read `.antigravity/Memory.txt` (knowledge index).
   - Ask user: "¿Área de trabajo específica? (UI/Architecture/Integration/Data/Testing/General)"
   - **If "General" or no response:** Continue with index only (lightweight).
   - **If specific domain:** Read `.antigravity/memory/{domain}.txt` or `.antigravity/Knowledge/{domain}.txt` for detailed patterns.
   - **Available domains (examples):**
     - `ui_patterns` - UI design patterns, components, styling
     - `architecture` - System architecture, design patterns
     - `integration` - External integrations, APIs, services
     - `data` - Data models, database patterns, migrations
     - `testing` - Testing strategies, patterns, fixtures

5. **Project Injection (Optional):**
   - Check for `.antigravity/Project_Rules.txt`.
   - **IF EXISTS:** Read it and the files referenced in its "Active Knowledge Modules" section.

6. **Status Report:**
   - Provide a concise summary of the current state.
   - List next priority tasks from ToDo.txt.
