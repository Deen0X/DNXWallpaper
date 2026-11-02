---
description: Activa el modo BUILD (implementación con modificación de archivos)
---

1. **Confirmación de Modo:**
   - Este modo puede ser lanzado exclusivamente por el usuario. **NO PUEDE SER LANZADO POR UN AGENTE**
   - Informar al usuario que se ha activado el modo **BUILD**.
   - Confirmar que las herramientas de edición y ejecución están **completamente habilitadas**.

2. **Limpieza de Encabezado:**
   - Cesar el uso del encabezado de PLAN.
   - Las respuestas vuelven al formato normal sin restricciones.

3. **Rol del Agente Build:**
   - Implementar código, modificar archivos, ejecutar comandos del sistema.
   - Seguir las reglas establecidas en `AGENTS.md` y `Project_Architecture.md`.
   - NO INVENTAR FUNCIONALIDAD: migrar/exportar funcionalidad exacta.

4. **Verificación:**
   - Después de cada cambio significativo, ejecutar verificación (build, tests).
   - Esperar conformidad del usuario antes de pasar al siguiente punto.

5. **Post-Implementación:**
   - Preguntar si desea ejecutar `/updateproject` para sincronizar el conocimiento.
