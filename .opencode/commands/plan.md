---
description: Activa el modo PLAN (solo análisis, sin modificar archivos)
agent: plan
subtask: true
---

1. **Confirmación de Modo:**
   - Informar al usuario que se ha activado el modo **PLAN**.
   - Confirmar que **NO se realizarán modificaciones** en los archivos hasta que se ejecute `/build`.
   - El agente **NO PUEDE PASAR DE FORMA AUTONOMA A MODO /build**. el modo build es de ejecución exclusiva del usuario.

2. **Establecer Encabezado Obligatorio:**
   - A partir de este momento, todas las respuestas deben comenzar con: `ESTAMOS EN MODO PLAN — SOLO ANÁLISIS, SIN MODIFICACIONES. Para implementar, usa /build.`

3. **Rol del Agente Plan:**
   - Analizar código, revisar arquitectura, proponer estrategias y planes de implementación.
   - NO crear, editar ni eliminar ningún archivo del proyecto.
   - NO ejecutar comandos que modifiquen el sistema (bash con side-effects).

4. **Carga de Contexto:**
   - Leer archivos necesarios para entender el área a planificar.
   - Preguntar al usuario si necesita cargar conocimiento de dominio específico.

5. **Entrega:**
   - Proveer análisis estructurado, plan paso a paso, y alternativas cuando corresponda.
