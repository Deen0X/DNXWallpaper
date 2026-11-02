---
description: Analyze current chat health based on Step Id and context.
---

1. **Analyze Step Id:**
   - Identify the current `Step Id` from the User Request.

2. **Determine Phase:**

| Phase | Range (Steps) | Status | Action |
|-------|---------------|--------|--------|
| 🟢 **Fresca** | 0 - 800 | Óptimo | None |
| 🟡 **Madura** | 800 - 1400 | Productivo | Maximum efficiency |
| 🟠 **Pesada** | 1400 - 1800 | Lag Leve | Add subtle warning: `(🟠 Chat Pesado: Step XXXX)` |
| 🔴 **Crítica** | > 1800 | Lag Alto | **SUGGEST CLOSE:** "⚠️ Step {id}: Contexto saturado. Ejecuta `/updateproject` e inicia nuevo chat." |

3. **Report Status:**
   - Show styled alert/table with current status
   - If in Orange/Red phase, remind user to run `/updateproject`
   - Provide context-specific recommendations
