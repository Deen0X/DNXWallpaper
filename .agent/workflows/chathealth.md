---
description: Analyze current chat health based on Step Id and context.
---

1.  **Analyze Step Id:**
    - Identify the current `Step Id` from the User Request.

2.  **Detect Context (Auto):**
    - **Agent Manager Detection:**
        - Multiple workspaces active
        - OR: User mentions "Agent Manager" in recent messages
        - OR: Frequent project switching
    - **Editor Detection:**
        - Single workspace
        - OR: User mentions "Editor" in recent messages
        - OR: Consistent file paths in same project
    - **Default:** Assume Agent Manager (conservative)

3.  **Determine Phase (Context-Aware):**
    
    **If Agent Manager (Conservative Limits):**
    - **Step 0-600:** 🟢 Fresca (Optimum)
    - **Step 600-1000:** 🟡 Madura (Productive)
    - **Step 1000-1400:** 🟠 Pesada (Lag Warning - Consider switching)
    - **Step >1400:** 🔴 Crítica (High Risk - Switch recommended)
    
    **If Editor (Relaxed Limits):**
    - **Step 0-1200:** 🟢 Fresca (Optimum)
    - **Step 1200-2000:** 🟡 Madura (Productive)
    - **Step 2000-2800:** 🟠 Pesada (Lag Warning)
    - **Step >2800:** 🔴 Crítica (Saturation - Restart Recommended)

4.  **Report Status:**
    - Output detected context (Agent Manager / Editor)
    - Show styled alert/table with current status
    - If in Orange/Red phase, remind user to run `updateproject`
    - Provide context-specific recommendations

5.  **Manual Override (Optional):**
    - User can specify: `/chathealth agent` or `/chathealth editor`
    - This overrides auto-detection for this session
