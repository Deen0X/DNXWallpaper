# Template Workflow IA (OpenCode + Antigravity)

Sistema de contexto persistente para trabajar con IA en cualquier proyecto.
Objetivo: que un chat nuevo tenga contexto sin re-analizar el codigo ni gastar tokens.

## Uso

1. Copia el contenido de esta carpeta a la raiz del nuevo proyecto.
2. Abre el proyecto en OpenCode (lee `AGENTS.md` automaticamente) o Antigravity.
3. Ejecuta `/bootstrap`.
4. Rellena la **seccion 1 de `Memory.txt`** (contexto del proyecto).
5. Trabaja. Al cerrar sesion, ejecuta `/updateproject`.

## Estructura

```
AGENTS.md                     Config OpenCode (persona + comandos)
Antigravity_Directives.txt    Bootstrap universal (protocolos, golden rules)
.agent/
  config.json                 Modelo por defecto Antigravity
  workflows/*.md              Workflows Antigravity (7)
.antigravity/
  Memory.txt                  INDICE de contexto (se lee siempre) <- rellenar
  Summary.txt                 Estado de las ultimas 3 sesiones
  ToDo.txt                    Tareas
  History.txt                 Archivo de sesiones antiguas
  memory/*.txt                Dominios RAG (bajo demanda)
  Knowledge/                  Conocimiento extendido (opcional)
.opencode/
  .gitignore
  commands/*.md               Comandos slash OpenCode
  skills/dnx-ui-style/        Skill de UI Windows (WPF/WinUI, Fluent/DNX)
```

## Comandos

| Comando | Descripcion |
|---|---|
| `/bootstrap` | Carga contexto inicial de la sesion |
| `/updateproject` | Persiste conocimiento al cerrar sesion |
| `/chathealth` | Estado del contexto (Step Id) |
| `/analyze_health` | Detecta ficheros grandes / refactor |
| `/architecture` | Recarga `Project_Architecture.md` |
| `/brainstorming` | Modo diseno, sin modificar ficheros |
| `/brainstormingend` | Sale de brainstorming |

## Filosofia de memoria

- `Memory.txt` = indice corto y estable. Se lee siempre, debe caber en la cabeza.
- `Summary.txt` = estado reciente (3 sesiones max).
- `memory/{dominio}.txt` = detalle profundo, se carga SOLO si la sesion lo toca.
- `Project_Architecture.md` = SSoT de arquitectura, opcional pero recomendado.
- Nunca dupliques informacion entre niveles: indice apunta, dominio detalla.

## Skills incluidas

- `dnx-ui-style`: sistema de diseno para apps Windows (WPF/WinUI). Se activa al
  crear o modificar XAML, estilos, temas o controles DNX/Fluent.
