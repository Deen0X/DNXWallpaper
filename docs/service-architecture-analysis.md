# Servicio externalizado + clientes multiplataforma — análisis

Fecha: 2026-10-29
Estado: **análisis de concepto** (sin cambios de código)

## 1. La pregunta correcta

No es "¿servidor sí o no?", sino **dónde está la frontera**. Hay dos planos
distintos y mezclarlos es lo que hace fracasar este tipo de arquitecturas:

| Plano | Qué hace | Dónde tiene que vivir |
|---|---|---|
| **Control** | Catálogo, metadatos, miniaturas, transcode/export, BD, ajustes, playlist, perfiles de dispositivo, UI web | Donde sea: mismo PC, NAS, Raspberry, VPS |
| **Ejecución** | Enumerar monitores, crear la superficie de fondo, decodificar vídeo, shaders, aplicar | **Solo en la máquina del usuario** |

El plano de control sí se puede externalizar entero y sin perdidas. El plano de
ejecución no se puede externalizar nunca: necesita sesión gráfica, GPU y
compositor.

Con esa separación, la propuesta del usuario **es correcta** y mi objeción
anterior ("no compensa") deja de aplicar, porque estaba evaluando solo el caso
Windows-monolítico. Con 4 sistemas operativos y 3 dispositivos el cálculo
cambia por completo.

## 2. Qué se gana de verdad (y qué no)

### Ganancias reales

1. **Una sola UI para todo.** Es la ganancia más grande y la más subestimada.
   La UI web es el único código de interfaz: Windows, Linux, Android, tablet.
   No hay que rehacer pantallas por plataforma. Esto solo justifica el split.
2. **Catálogo y caché únicos.** Un transcode de 1080p se hace una vez y sirve a
   5 dispositivos. Hoy cada cliente repetiría el trabajo.
3. **Perfiles por dispositivo.** El servidor sabe que el móvil es 1080×2400 y
   el monitor 3440×1440, y devuelve el export correcto. Hoy eso lo adivina el
   cliente.
4. **El agente se vuelve tonto.** Si los plugins viven en el servidor, el
   agente no necesita Python ni PowerShell ni ffmpeg. Un binario por SO que
   sabe: pintar, hablar HTTP, cachear. Esto es lo que hace viable Linux y
   Android sin multiplicar el mantenimiento.
5. **Datos que hoy no existen**: historial, favoritos compartidos, "lo que
   estaba puesto en cada equipo".

### Lo que no se gana

- No reduce el consumo del PC: el motor sigue local.
- No permite "wallpaper en la nube": el vídeo se decodifica en el cliente.
- No elimina la instalación local: hay que instalar el agente igual.

## 3. El coste real del split

| Coste | Magnitud | Mitigación |
|---|---|---|
| Descubrimiento del servidor | Medio | Ya hay UDP (`WPX/Ping`). Extender a "modo servidor" |
| Autenticación | Medio | Token por dispositivo + QR de emparejamiento |
| TLS | Bajo si es LAN | HTTP en LAN; TLS solo si sale a internet |
| Fallos de red | Alto si no se diseña | El agente aplica desde caché local sin servidor |
| Sincronización de estado | Medio | El servidor manda; el agente guarda el último estado válido |
| Dos instaladores | Alto para usuario base | **Un solo binario con tres modos** (ver §5) |

## 4. Plugins: la corrección importante

"Migrar los plugins a Python" es correcto **a medias**. Hay que partir el
concepto en dos clases, porque hoy están mezcladas:

| Clase | Ejemplo | Dónde corre | Runtime |
|---|---|---|---|
| **Remoto** | wallhaven, pixabay, wallsflow, lively | Servidor | Python (stdlib + requests) |
| **Local** | "mis carpetas" | **Agente** (accede al disco del usuario) | Ninguno: el agente escanea |

El plugin `local` **no se puede mover al servidor** si el servidor está en otro
equipo: no ve `D:\Videos`. Excepciones: carpetas de red (SMB/NFS) montadas en el
servidor, o el agente publicando su índice al servidor (recomendado: el agente
escanea y hace `POST /api/local/index`; el servidor solo lo cataloga).

Consecuencia buena: el agente no necesita runtime de plugins. Deja de necesitar
PowerShell embebido, lo cual es un alivio en Linux y Android.

Python en el servidor: sí, con **Python embebido** (no instalación aparte). Unos
25 MB, sin dependencias del sistema. Los plugins pasan a ser módulos con el
mismo contrato JSON que ya tienen (health/categories/list/detail/thumb/fetch),
así que la migración es mecánica.

## 5. Forma del producto (lo que decide si esto es usable)

Un binario, tres modos:

```
DNXWallpaper.exe                 # AGENTE + servidor embebido (por defecto)
                                 #   servidor en 127.0.0.1:8477, invisible
DNXWallpaper.exe --server        # solo servidor (NAS, Raspberry, VPS)
DNXWallpaper.exe --server=http://nas:8477   # agente puro, sin servidor local
```

Reglas de diseño:

- **Doble clic = todo funciona.** Nunca dos instaladores ni dos servicios.
- El agente **arranca sin red**: aplica el último wallpaper y la playlist
  cacheados.
- La UI se abre siempre igual: `http://<servidor>/ui2/`. Si el servidor es
  local, es transparente.
- El modo servidor externo es **opt-in** y se configura en la UI.

Esto responde a tu preocupación del usuario base: el 95 % de usuarios nunca sale
del modo 1 y no sabe que hay un servidor dentro.

## 6. Viabilidad por plataforma

### Windows — resuelto
WorkerW/Progman. Sin cambios. Es la referencia de comportamiento.

### Linux X11 — viable, coste medio
La técnica es la misma familia que Windows: una ventana con
`_NET_WM_WINDOW_TYPE_DESKTOP`, override-redirect, enviada al fondo, o pintar en
la ventana raíz (`_XROOTPMAP_ID`/`ESETROOT_PMAP_ID`).

- Cubre: **Linux Mint (Cinnamon/MATE/XFCE)**, Ubuntu X11, KDE X11, XFCE.
- Multi-monitor: una ventana por salida vía XRandR.
- Render: **no portar el motor de shaders a GL de entrada**. Usar `libmpv`/
  `mpv` embebido (ventana propia) para vídeo, y dejar shaders para fase 2.
  mpv ya resuelve VA-API, HDR, sync y decodificación por hardware.
- Estimación: 3–5 semanas para producto usable.

### Linux Wayland — viable pero **por compositor**, no "en general"
Wayland prohíbe por diseño que un cliente controle el fondo. Cada escritorio
expone su propio mecanismo:

| Entorno | Mecanismo | Coste | Riesgo |
|---|---|---|---|
| **KDE Plasma (KWin)** | plugin de wallpaper (C++/QML) | 2–3 sem | Medio, API estable |
| **GNOME (Mutter)** | extensión de GNOME Shell (JS) | 2–3 sem | Medio, rompe por versión |
| **wlroots** (Sway, Hyprland) | `layer-shell` capa `background` | 1–2 sem | Bajo |
| **SteamOS (modo juego)** | gamescope | **imposible** | — |
| **SteamOS (modo escritorio)** | es KDE → plugin KWin | igual que KDE | Medio |

Conclusiones duras:
- **SteamOS solo en modo escritorio.** En gaming mode gamescope ocupa la
  pantalla y no existe superficie de fondo. No hay solución limpia.
- Cada DE es un mini-proyecto **con mantenimiento permanente** (GNOME rompe
  extensiones en cada release). Es el mayor coste oculto de Linux.
- Recomendación: no prometer "soporte Linux". Prometer **matriz de soporte**:
  KDE Wayland + GNOME Wayland + X11 genérico.

### Android — ya cubierto
Live wallpaper funciona desde API 7. La app actual vale. Solo falla en
contenedores que lo deshabilitan (ChromeOS/FydeOS ARC).

### Android sin live wallpaper (launcher)
Técnicamente posible, pero es un producto distinto: hay que implementar cajón
de apps, widgets, iconos, gestos y mantenerlo contra cada versión de Android y
las políticas de Play. **Descartar** salvo experimento; el caso real
(live wallpaper deshabilitado) es marginal y ocurre en contenedores, donde un
launcher tampoco puede dibujar el fondo del sistema.

### ChromeOS / FydeOS — **descartar**
La idea de "ventana siempre detrás, ocupando todo menos la barra" **funciona en
Linux X11** (es exactamente `_NET_WM_WINDOW_TYPE_DESKTOP`), pero **no en
ChromeOS**:

- El escritorio de ChromeOS lo dibuja el compositor (Ash); no hay capa
  "detrás de todo" expuesta a aplicaciones.
- No existe "always at bottom"; el gestor de ventanas es del sistema.
- Las Chrome Apps están en retirada; kiosco requiere enrolamiento empresarial.
- En FydeOS el contenedor Android no expone `WallpaperService` (ya lo
  comprobaste).
- La vía Linux (Crostini) solo aplica si el usuario vive dentro de Linux, y ahí
  el compositor es Sommelier → tampoco hay capa de fondo real.

Es la plataforma con peor relación esfuerzo/resultado de toda la lista. Si algún
día se hace, sería como "visor a pantalla completa en modo kiosco", que no es un
wallpaper.

### macOS / iOS
De acuerdo en descartar (firma, notarización, sandbox, 99 $/año, APIs privadas
para el fondo animado).

## 7. Riesgos que pueden matar el proyecto

1. **Multiplicación de mantenimiento.** 4 SO × 3 DE = 12 combinaciones. Sin el
   agente "tonto" (sin plugins, sin Python) esto es inasumible para una persona.
2. **Wayland cambia bajo los pies.** Una extensión GNOME rota puede dejar sin
   producto a todos los usuarios de Ubuntu en una actualización.
3. **Descubrimiento y emparejamiento.** Es la primera causa de abandono en apps
   cliente/servidor domésticas ("no encuentra el PC").
4. **Tamaño de los medios.** Un wallpaper de 300 MB por LAN va bien; por
   internet no. El modo servidor remoto debe asumirse **LAN-first**.
5. **Licencias.** Centralizar el catálogo multiplica la exposición de
   atribuciones; hay que mantener `attribution` por item y por plugin.
6. **HDR y códecs** distintos por plataforma; sin pruebas reales es deuda
   silenciosa.
7. **Complejidad percibida.** Si el usuario ve "servidor" en la UI, se pierde.
   El modo servidor debe estar enterrado en Ajustes avanzados.

## 8. Veredicto

**Externalizar el servicio sí merece la pena**, pero por una razón distinta de
la que parece: no por "tener servidor", sino porque **permite tener una sola UI
y un solo catálogo para N plataformas**, y porque deja al agente sin lógica de
negocio (que es lo único que hace viable Linux y Android a la vez).

Condiciones sin las cuales no lo haría:

- Un solo binario, tres modos. Instalación básica idéntica a hoy.
- El agente funciona **sin servidor** (caché + último estado).
- El servidor es **LAN-first**, no internet-first.
- Plugins remotos en el servidor; plugins locales **en el agente**.
- La UI es la web, única, para todos.

Lo que **no** haría:
- Docker para el agente/servidor de escritorio. No aporta: necesita sesión
  gráfica, GPU y enumerar monitores. Docker en Windows/Linux sin escritorio no
  puede aplicar wallpapers. Solo tendría sentido un servidor "solo catálogo"
  sin render, que no es el producto.
- ChromeOS/FydeOS.
- Launcher para Android.
- Portar el motor de shaders a Linux en la fase 1.

## 9. Orden recomendado

| Fase | Contenido | Valor | Coste |
|---|---|---|---|
| **A** | Separar en código `server` / `agent` **en el mismo proceso**. Sin cambio visible | Prepara todo | Bajo |
| **B** | UI web completa en Windows: monitores, aplicar, persistencia, playlist | Producto usable | Medio |
| **C** | Modo `--server` + agente apuntando a servidor remoto | Multi-dispositivo real | Medio |
| **D** | Plugins remotos a Python; agente sin runtime de plugins | Base multiplataforma | Medio |
| **E** | Agente Linux X11 (libmpv) | Mercado Linux amplio | Alto |
| **F** | Plugin KDE Wayland (cubre SteamOS escritorio) | Steam Deck | Alto |
| **G** | Extensión GNOME Wayland (Ubuntu) | Ubuntu | Alto |

A y B no dependen de la decisión de plataformas y hay que hacerlas igual.
E, F y G solo si B y C demuestran uso real.

## 10. Nota sobre "servidor en la propia máquina"

Es el caso por defecto y el que hay que optimizar: servidor embebido en el
agente, escuchando solo en `127.0.0.1` salvo que el usuario active "acceso desde
la red". El coste es despreciable (un hilo + sockets), y así el usuario remoto
(consola, tablet) funciona solo cambiando la URL.

## 11. Docker: por quÃ© no como requisito del producto, y dÃ³nde sÃ­

### 11.1 Lo que hay que conceder (y es importante)

Tu descripciÃ³n **elimina mi objeciÃ³n principal**: el servidor no aplica el
wallpaper. Si el servidor es solo catÃ¡logo + ffmpeg + UI web + entrega de
ficheros, entonces **sÃ­ es una carga contenedorizable por naturaleza**. No
necesita sesiÃ³n grÃ¡fica, ni GPU del escritorio, ni enumerar monitores. Eso era
exactamente lo que hacÃ­a inviable Docker antes y ya no aplica.

Y el valor que buscas (una sola instancia del catÃ¡logo, mismas capacidades de
recorte/resoluciÃ³n para todos los clientes) **no depende de Docker**: depende
del split. Docker es el *cÃ³mo*, no el *quÃ©*.

### 11.2 Los cinco bloqueos reales, en orden de gravedad

**1. No puedes distribuir Docker Desktop.**
La EULA de Docker Desktop no permite redistribuirlo. No se puede meter en el
instalador. El usuario tendrÃ­a que instalarlo a mano: ~2 GB, WSL2 o Hyper-V,
virtualizaciÃ³n activada en BIOS, reinicio. En Windows 10/11 Home no hay
contenedores Windows nativos, asÃ­ que la Ãºnica vÃ­a es una VM Linux (WSL2). Para
el usuario base que quiere doble clic, esto lo mata.

**2. El descubrimiento en LAN deja de ser fiable.**
El trÃ¡fico de broadcast UDP (`WPX/Ping` en 7884) **no cruza** el NAT del bridge
de Docker ni el de WSL2. Docker Desktop aÃ±adiÃ³ *host networking* hace poco, pero
es reciente y el broadcast sigue siendo el punto frÃ¡gil. El emparejamiento
mÃ³vil-PC es justo el flujo que no puedes permitirte romper.

**3. Las carpetas locales del usuario dejan de funcionar bien.**
Montar `D:\Videos` en un contenedor significa pasar por virtiofs/9p: escaneo
recursivo lento (decenas de miles de ficheros = minutos) y lecturas secuenciales
de ficheros de 300 MB degradadas. AdemÃ¡s se rompe el atajo actual del plugin
local: `fetch` devuelve la **ruta real** y el cliente lee del disco sin copia.
Dentro de un contenedor esa ruta no existe fuera.

**4. Transcode por hardware: no.**
VA-API requiere `/dev/dri`, que en WSL2 no existe. NVENC necesita el NVIDIA
Container Toolkit, no disponible en Windows para contenedores Linux. QuedarÃ­a
transcodificaciÃ³n por CPU: un 1080p de 30 s pasa de segundos a minutos.

**5. Ciclo de vida y soporte.**
El launcher tendrÃ­a que: comprobar que el daemon corre, hacer `pull` la primera
vez (cientos de MB), crear/recrear el contenedor, mapear puertos y volÃºmenes.
Cuando algo falla, el mensaje al usuario es "no encuentra el servidor" y el
diagnÃ³stico real es "el daemon no arrancÃ³ tras el reinicio". Y WSL2 reserva
memoria por defecto (hasta ~50 % de la RAM del host): para una app de wallpapers
en un portÃ¡til, eso se nota.

### 11.3 Lo que Docker aporta de verdad, y por quÃ© aquÃ­ no rinde

| Beneficio de Docker | Â¿Se aplica aquÃ­? |
|---|---|
| Dependencias reproducibles (ffmpeg siempre igual) | **SÃ­** â€” pero se resuelve con ffmpeg estÃ¡tico embebido |
| Aislamiento | Poco: el servidor ya bindea solo donde toca |
| OrquestaciÃ³n / escalado | No: 1 instancia domÃ©stica |
| Rollback de versiones | Marginal: un binario tambiÃ©n se versiona |
| Multi-tenant / SaaS | No es el producto |

El 90 % del beneficio se obtiene con **un binario estÃ¡tico + ffmpeg estÃ¡tico**,
sin VM, sin daemon, sin reinstalar nada.

### 11.4 DÃ³nde Docker SÃ es la respuesta correcta

Servidor **remoto opcional**: NAS, VPS, Raspberry, mini-PC Linux. AhÃ­ Docker
gana de calle: el usuario no compila, no pelea con dependencias, actualiza con
`docker pull`. Publicar `docker-compose.yml` + imagen del servidor es un extra
gratis, no un requisito.

### 11.5 RecomendaciÃ³n: dos artefactos, un cÃ³digo

| Escenario | Entrega | Por quÃ© |
|---|---|---|
| InstalaciÃ³n por defecto (mismo PC) | Binario nativo sin instalaciÃ³n; servidor embebido en el agente | Doble clic, sin VM, LAN, carpetas locales, hw transcode |
| Servidor remoto (NAS/VPS/RPi) | Imagen Docker + compose | Reproducible, sin dependencias, actualizable |
| Servidor en otro PC Windows | Binario nativo con `--server` | Evita WSL2 y el NAT |

Cero divergencia de cÃ³digo: `DNXWallpaper.exe --server` y `docker run` arrancan
el mismo servidor.

### 11.6 QuÃ© me harÃ­a cambiar de opiniÃ³n

- Que el servidor acabe siendo Python con muchas dependencias nativas (entonces
  el contenedor ahorra dolor real).
- Que el objetivo sea **solo Linux** y con usuarios tÃ©cnicos.
- Que haya multiinquilino o varias instancias que orquestar.
- Que quieras actualizaciones atÃ³micas y rollback del lado servidor.
- Que tus usuarios ya tengan Docker o un NAS.

Mientras sea "un usuario, un PC, doble clic", Docker como requisito resta.


## 12. Correcciones tras la aclaraciÃ³n (2026-10-29)

La aclaraciÃ³n desactiva buena parte de mis objeciones. Se anotan las que
quedan vivas y el diseÃ±o concreto que se deriva.

### 12.1 Docker: aceptado como artefacto, no como runtime

Lo que se pide es: **el servidor debe poder empaquetarse en una imagen**. Eso no
lo discuto, es lo correcto. El contrato que lo hace trivial:

```
dnxwallpaperd --port 8477 --data-dir /data
```

- Sin rutas absolutas asumidas; todo bajo `--data-dir`.
- Config por entorno (`DNX_PORT`, `DNX_DATA`, `DNX_TOKEN`).
- Sin dependencia del escritorio: no toca Windows API, no enumera monitores.
- ffmpeg **estÃ¡tico** embebido (choca con la idea de "dependencias
  reproducibles": se resuelve igual, dentro del binario).

Con eso, `Dockerfile` (FROM scratch + el binario + ffmpeg) y `compose` con un
volumen son 20 lÃ­neas. Cero coste de mantenimiento.

Retiro por tanto los puntos 11.2.1 (no distribuir Docker Desktop) y 11.2.5
(ciclo de vida) **como objeciones**: no se distribuye y no se recrea nada.

### 12.2 Puerto: identificaciÃ³n por firma, no por sondeo a ciegas

DiseÃ±o acordado:

```
1. Si hay server_url en config  -> usarla (fin).
2. Si no, para p en 8477..8576  (paralelo, timeout 250 ms):
     GET http://127.0.0.1:p/api/info
       product == "dnxwallpaper"  -> ES NUESTRO: adjuntarse (no arrancar otro)
       responde, otro producto    -> siguiente puerto
       no responde                -> libre: arrancar aquÃ­
3. Persistir el puerto elegido (server.json).
```

`/api/info` debe devolver `{product:"dnxwallpaper", instanceId, version, port}`.
La firma es lo que permite distinguir "es nuestro servidor" de "hay un Tomcat".

Dos detalles que evitan fallos reales:

- **No arrancar un segundo servidor si ya hay uno nuestro en el puerto base.**
  Si no, dos catÃ¡logos, dos SQLite, dos caches compitiendo.
- **Persistir el puerto.** Si no, tras un reinicio el puerto cambia y todos los
  QR y clientes emparejados dejan de valer.

### 12.3 QR: hay que elegir bien la interfaz de red

Es el fallo clÃ¡sico. Un PC tiene varias IPv4: Ethernet, Wi-Fi, Hyper-V, WSL,
Docker, VPN (WireGuard/Tailscale). El QR con la IP de la VPN **no conecta**.

Regla: elegir la interfaz con la puerta de enlace por defecto, excluyendo
adaptadores virtuales (nombres conocidos + `IF_TYPE_SOFTWARE_LOOPBACK`), y si
hay duda mostrar un desplegable con las candidatas.

El QR lleva: `http://<ip>:<puerto>/?t=<token>` y el cliente lo guarda. Un solo
uso, y el token se puede rotar desde Ajustes.

### 12.4 El plugin local desaparece: consecuencia buena

Sustituido por el flujo del cliente: elegir fichero local â†’ usar tal cual, o
subir para optimizar (`/api/upload`) â†’ pedir export con `key="up:<id>"` +
`rect` + `target` â†’ recibir el resultado â†’ aplicar.

Ventajas:

- **Un solo camino de cÃ³digo** para mÃ³vil y escritorio: el escritorio pasa a ser
  un cliente mÃ¡s. La arquitectura queda validada por construcciÃ³n.
- El servidor no necesita acceso al disco de nadie: puede vivir en un NAS.
- Desaparece la clase "plugin local" y con ella el escaneo recursivo, las rutas
  absolutas y los IDs basados en ruta.

Lo que hay que asumir:

- Cada cliente necesita **store local propio** (Ã­ndice + miniaturas), como ya
  tiene la app Android.
- El material local es **por dispositivo**: el mÃ³vil no ve los vÃ­deos del PC.
  Es correcto; si algÃºn dÃ­a se quiere compartir, el cliente publica su Ã­ndice y
  sirve los bytes (fase futura, no ahora).

### 12.5 Transcode distribuido entre clientes: viable, con matices

La idea (registro de capacidades, ranking, reparto de trabajos) es sÃ³lida en
concepto. El problema es que **el cuello de botella suele ser la red, no la CPU**.

Ã“rdenes de magnitud para 1080p, 30 s:

| OperaciÃ³n | Tiempo tÃ­pico |
|---|---|
| Transcode NVENC / QSV | 3â€“8 s |
| Transcode CPU 8 nÃºcleos (`veryfast`) | 8â€“20 s |
| Transcode CPU portÃ¡til U-series | 25â€“60 s |
| Transcode en mÃ³vil de gama media | 20â€“40 s |
| Enviar 60 MB por Gigabit | 1â€“2 s |
| Enviar 60 MB por Wi-Fi 5 (5 GHz) | 8â€“15 s |
| Enviar 60 MB por Wi-Fi 2,4 GHz | 40â€“90 s |

De ahÃ­ la regla: **merece la pena externalizar si**

```
transferencia_ida + transferencia_vuelta + transcode_remoto
   <  transcode_local
```

Consecuencias:

- **Cliente con GPU y buen Wi-Fi**: transcode local gana casi siempre. Repartir
  solo aÃ±ade latencia.
- **Cliente dÃ©bil** (mÃ³vil antiguo, tablet, SBC): externalizar gana claro.
- **PortÃ¡til en 2,4 GHz**: externalizar puede perder contra transcodificar en
  local por CPU.
- **Trabajos en lote** (reprocesar 200 wallpapers): externalizar gana siempre,
  porque lo que importa es el rendimiento agregado, no la latencia.

Y el matiz que hace que todo esto importe menos: **cache por
(fuente, parÃ¡metros)**. En una casa con 5 dispositivos, la segunda peticiÃ³n del
mismo wallpaper + mismo recorte + misma resoluciÃ³n es gratis. Ese es el 80 % del
beneficio con el 5 % del coste.

#### Ranking: no ordenar por potencia, sino por tiempo estimado

Un escalar "potencia" pierde informaciÃ³n. Lo correcto es puntuar cada candidato
por **tiempo total estimado**:

```
coste(worker) = transferencia(fuente -> worker)
              + transcode_estimado(worker, params)
              + transferencia(worker -> servidor)
```

Elegir el mÃ­nimo, no el mÃ¡s potente. Un PC de sobremesa con NVENC conectado por
Ethernet gana a un portÃ¡til mÃ¡s potente pero en Wi-Fi mala.

AdemÃ¡s el ranking debe filtrar por **capacidad**, no solo por velocidad:

- cÃ³dec de salida soportado por hardware (H.264 / HEVC / AV1),
- filtros necesarios (`pad`, `scale`, tonemap HDR),
- HDR: un worker sin tonemap devuelve colores lavados â†’ debe ser un flag duro,
- segmento de red (misma LAN),
- estado: CPU/GPU/RAM por debajo del umbral, y **sin juego en primer plano**.

#### Riesgos reales del reparto

1. **Resultados no reproducibles.** NVENC, QSV, AMF y x264 dan calidades
   distintas. Dos clientes pueden recibir ficheros diferentes para la misma
   peticiÃ³n. Se mitiga con la cache: el primero que lo hace fija el resultado.
2. **Seguridad.** Un worker recibe un fichero y ejecuta ffmpeg sobre Ã©l: ffmpeg
   es una superficie de ataque amplia. MÃ­nimo: token por worker, lÃ­mites de
   tamaÃ±o/duraciÃ³n, `-protocol_whitelist file`, sin `-i` remoto.
3. **Robar recursos al usuario.** Sin detecciÃ³n de juego/llamada, el reparto se
   percibe como "me va lento el PC".
4. **Complejidad de protocolo.** submit/claim/heartbeat/progress/cancel/retry +
   idempotencia + limpieza de intermedios. Es un subsistema, no una funciÃ³n.

#### RecomendaciÃ³n por fases

| Fase | QuÃ© | Coste | CuÃ¡ndo |
|---|---|---|---|
| 1 | El cliente transcodifica si puede (lo que ya pasa) | 0 | ya |
| 2 | **Cache del servidor por (fuente, params)** | Bajo | primero |
| 3 | Offload solo si el cliente declara `canTranscode:false` | Medio | si hace falta |
| 4 | Registro de capacidades + ranking + disponibilidad | Alto | solo con datos |

La fase 3 es el 90 % del valor de la idea original: un flag y un endpoint. La
fase 4 (ranking fino) solo si los datos demuestran que hace falta.

### 12.6 Objeciones que siguen vivas

1. **Linux Wayland** sigue siendo el coste grande (por compositor, mantenimiento
   permanente). No depende de Docker.
2. **HDR**: sin pruebas en monitores reales es deuda silenciosa, y ahora tambiÃ©n
   afecta a la elecciÃ³n de worker.
3. **Deriva de puerto / interfaces virtuales**: resuelto en diseÃ±o, pero hay que
   implementarlo bien o el QR falla "a veces" (el peor fallo posible).
4. **Store local duplicado** en cada cliente: mÃ¡s cÃ³digo de cliente, menos de
   servidor. Aceptable, pero es trabajo real.
5. **Seguridad del servidor en LAN**: hoy es HTTP sin auth. Con servidor externo
   y varios dispositivos, hace falta token desde el principio.

### 12.7 Veredicto actualizado

La propuesta es **coherente y viable**. Lo que la hace viable no es Docker, es
que el servidor dejÃ³ de ser "quien aplica el wallpaper". Docker pasa a ser un
artefacto de despliegue mÃ¡s, y el `Dockerfile` es barato.

Orden recomendado:

1. Contrato del servidor (`--port`, `--data-dir`, `/api/info` con firma).
2. SelecciÃ³n y persistencia de puerto + QR con la NIC correcta.
3. UI web completa (monitores, aplicar, persistencia) sobre servidor local.
4. Flujo cliente de fichero local en escritorio (mismo camino que el mÃ³vil).
5. Cache de transcodes por parÃ¡metros.
6. `Dockerfile` + `compose`.
7. Offload a workers (fase 3) solo si los datos lo piden.


## 13. Decisiones cerradas (2026-10-29, fin de brainstorming)

### 13.1 Servidor

- Un solo binario, tres modos: agente + servidor embebido (defecto),
  `--server`, y agente apuntando a servidor remoto.
- Contrato: `--port`, `--data-dir`, `--no-ui`. Sin rutas absolutas asumidas.
- **Docker es un artefacto de despliegue**, no el runtime del producto. Se
  entrega `Dockerfile` + `compose` para servidores remotos (NAS/VPS/RPi).
- `ffmpeg` estÃ¡tico embebido: elimina la dependencia de un `ffmpeg.exe` del
  sistema y hace el contenedor reproducible.

### 13.2 IdentificaciÃ³n y emparejamiento

- `/api/info` incluye `product: "dnxwallpaper"`, `instanceId` y `interfaces`.
  La firma es lo que distingue nuestro servidor de cualquier otro servicio.
- SelecciÃ³n de puerto: `8477` y hasta `+30`, con `SO_EXCLUSIVEADDRUSE`.
- El puerto elegido se **persiste** en `res/config/server.json`; si no, deriva
  en cada arranque y los QR dejan de valer.
- QR con **checkboxes de interfaces**: el usuario marca las que quiere ofrecer
  (Ethernet y Wi-Fi pueden ser vÃ¡lidas a la vez). Con una sola URL el QR lleva
  la URL; con varias, un JSON `{product, urls}` que el cliente prueba en orden.
- Descubrimiento UDP: se mantiene como extra, ya no es el camino principal.

### 13.3 Seguridad

- Token por cliente. El QR de emparejamiento transporta el token; al escanearlo
  queda asociado cliente y servidor.
- Las peticiones llevan token; sin token vÃ¡lido, no hay catÃ¡logo.
- DDNS / internet es **responsabilidad del usuario** (avanzado). La app no
  configura el router.
- Herramienta de diagnÃ³stico Ãºtil: comparar la IP pÃºblica observada por el
  servidor con la que resuelve el DDNS para detectar **CGNAT** y avisar. No es
  requisito, pero evita horas de soporte.

### 13.4 Cliente

- Store propio para lo recibido del servidor (Ã­ndice + miniaturas).
- Selector de fichero local: **drag & drop** primero; `showDirectoryPicker`
  cuando el contexto sea seguro (`localhost` sÃ­, `http://LAN-IP` no); diÃ¡logo
  nativo como Ãºltimo recurso.
- Miniaturas de vÃ­deo local: `<video>` + `<canvas>` en el navegador, sin ffmpeg
  ni proceso aparte.
- Subida con `persist`: `1` deja el vÃ­deo en el catÃ¡logo del servidor como
  provider `uploads`; `0` se borra tras procesar (o por TTL si falla).

### 13.5 Salidas (niveles de compatibilidad)

| Nivel | Formato | Requiere agente | Control |
|---|---|---|---|
| 0 | Imagen estÃ¡tica | no | ninguno |
| 1 | Imagen animada (WebP/APNG) | no | ninguno |
| 2 | VÃ­deo | sÃ­ | total |

El nivel 1 es la vÃ­a barata para plataformas sin agente: Plasma
(`Animated Image` con formatos de Qt), y sirve de escaparate antes de instalar
nada. **WebP animado por defecto; APNG como alternativa; GIF solo si falla
algo**, porque 256 colores y tamaÃ±o lo hacen peor en todo.

### 13.6 Transcode distribuido (aparcado, con criterio)

- Solo merece la pena si
  `transferencia_ida + vuelta + transcode_remoto < transcode_local`.
- Ordenar candidatos por **tiempo total estimado**, no por potencia.
- Filtrar por capacidad (cÃ³dec, filtros, HDR) antes de puntuar.
- Primero **cache por (fuente, params)**: en una casa con 5 dispositivos es el
  80 % del beneficio con el 5 % del coste.
- DespuÃ©s: offload solo si el cliente declara `canTranscode:false`.
- El ranking fino (registro de capacidades + disponibilidad) queda para cuando
  los datos lo pidan.

### 13.7 Plugins

- Remotos en el servidor (Python).
- **El plugin local desaparece**: cada cliente gestiona sus ficheros y los sube
  si quiere optimizarlos. Un solo camino de cÃ³digo para mÃ³vil y escritorio.
- El material local es **por dispositivo**; compartir no es una funciÃ³n, es
  consecuencia de subir con `persist=1`.

### 13.8 Plataformas

| Plataforma | DecisiÃ³n |
|---|---|
| Windows | agente completo |
| Android | live wallpaper (ya hecho) |
| Linux X11 | agente con libmpv, viable |
| Linux Wayland | por compositor (KDE, GNOME, wlroots) |
| SteamOS | solo modo escritorio (KDE); gaming mode imposible |
| ChromeOS / FydeOS | descartado |
| Launcher Android | descartado |
| macOS / iOS | descartado |

