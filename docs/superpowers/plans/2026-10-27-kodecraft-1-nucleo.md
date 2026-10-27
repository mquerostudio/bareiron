# KodeCraft · Plan 1 de 3: el núcleo (bareiron) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convertir bareiron en un núcleo de servidor embebible y rápido. Debe arrancar y parar sin `exit()`, no congelarse por un cliente lento, comprimir el protocolo, indexar los cambios de bloques, exponer estado y eventos para una interfaz, y guardar el mundo por lotes a través de ganchos de plataforma. Todo probado en el Mac antes de tocar el Kode Dot.

**Architecture:**
- Se trabaja en un fork (`mquerostudio/bareiron`, rama `kode-dot`).
- Una capa de red nueva (`net.c`) se mete entre los manejadores de paquetes y los sockets:
  - las escrituras van a un búfer por cliente y se envían sin bloquear;
  - las lecturas solo empiezan cuando hay una trama completa en memoria;
  - la compresión se hace al enmarcar.
- Lo dependiente de la plataforma (memoria grande, cerrojo, tiempo, ceder CPU, eventos, guardar/cargar) entra por una estructura `BareironPlatform`. El Mac usa `platform_posix.c`; el Kode Dot dará la suya en el Plan 2.

**Tech Stack:**
- C11 (clang en macOS para las pruebas).
- miniz 3.0.2 (deflate/inflate, MIT, incluido en el repo).
- Python 3.9 para los bots de prueba (`zlib` y `socket` de la librería estándar).
- Node para `build_registries.js`; Java 21 para extraer los registros.

**Spec:** `docs/superpowers/specs/2026-10-27-kodecraft-design.md` (en el repo `kodecraft`).

**Hoja de ruta (cada plan deja software que funciona):**
- **Plan 1 (este):** núcleo en el fork de bareiron, probado en el Mac con bots y con un Minecraft real.
- **Plan 2:** app KodeCraft en el Kode Dot: plantilla del SDK, radio y WiFi, tarea del servidor en el núcleo 1, pantallas Inicio/Arrancando/Panel/Error con el arte, y PRs al SDK que salgan.
- **Plan 3:** guardado en SD (con su PR al SDK), mapa en vivo, prueba de 1 h con 6 jugadores, issues/PRs a p2r3/bareiron y borrador en apps.kode.diy.

Los Planes 2 y 3 se escriben cuando termine el anterior, porque dependen de sus interfaces.

## Global Constraints

- Versión de Minecraft **1.21.8**, protocolo **772**. Solo cliente vanilla.
- Modo de juego por defecto **supervivencia** (`GAMEMODE 0`). MOTD **"KodeCraft"**, brand **"kodecraft"**.
- Puerto por defecto **25565**.
- Conexiones simultáneas máximas: **8** (`MAX_CONNECTIONS`). Jugadores recordados: **64** (`MAX_PLAYERS`, que cuenta UUIDs distintos, no conexiones).
- Umbral de compresión: **256** bytes (el de vanilla). Nivel de deflate **1**.
- Ningún `send`/`recv` puede bloquear el bucle. Un cliente cuyo búfer de salida supere **`NET_TX_CAP` = 4 MiB** se desconecta.
- El núcleo no incluye cabeceras de ESP-IDF ni usa `ESP_PLATFORM`: todo lo de plataforma va por `BareironPlatform`.
- Nada llama a `exit()` dentro del núcleo.
- Guardado por lotes: cada **60 s** si hay cambios, y al parar. Nunca un acceso a disco por cada cambio de bloque.
- Los registros generados (`src/registries.c`, `include/registries.h`) no se suben al repo (siguen en `.gitignore`).
- Estilo: el de bareiron (dos espacios, `snake_case` para variables, `camelCase` para funciones existentes, prefijo `bareiron_` / `net_` / `bc_` para las nuevas, comentarios `//` con la densidad del código vecino).
- Commits con la línea `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. **Un cliente que deja de leer** (juego en pausa por el sistema, red mala) no debe frenar a los demás. Se le desconecta al pasar `NET_TX_CAP` y el resto sigue recibiendo keep-alives. → `test_stalled_client_does_not_freeze_others` (Task 9).
2. **Un cliente que corta de golpe en mitad de la entrada** (TCP RST con chunks en el búfer). No hay caída, el hueco se reutiliza y no queda un jugador fantasma en el estado. → `test_abrupt_close_during_join` (Task 9).
3. **Reconectar con el mismo nombre justo después de caerse.** Recupera su inventario y aparece una sola vez en `bareiron_get_status`. → `test_rejoin_same_name_single_entry` (Task 9).
4. **El launcher de Minecraft hace ping de estado una y otra vez.** No cuenta como jugador ni agota huecos de conexión. → `test_status_pings_do_not_leak_slots` (Task 9).
5. **Parar con jugadores dentro y volver a arrancar.** El mundo se guarda, `bareiron_run` vuelve y el siguiente arranque tiene la misma semilla y los mismos bloques. → `test_stop_and_restart_keeps_world` (Task 8).

## Estructura de archivos (fork `~/Documents/git/bareiron`, rama `kode-dot`)

| Archivo | Responsabilidad |
|---|---|
| `include/bareiron.h` (nuevo) | API pública: config, plataforma, eventos, estado, `bareiron_run`/`bareiron_request_stop`/`bareiron_get_status` |
| `src/server.c` (nuevo) | Ciclo de vida: inicializar y reiniciar globales, socket de escucha, bucle principal (sale de `main.c`) |
| `src/main.c` | Se queda con `handlePacket` y un `main()` mínimo para PC que llama a `bareiron_run` |
| `include/net.h`, `src/net.c` (nuevos) | Búferes por cliente, enmarcado y desenmarcado, compresión, envío no bloqueante |
| `src/tools.c` | `send_all` y `recv_all` pasan a usar `net.c` (sin espera activa ni `disconnectClient` dentro) |
| `include/blockindex.h`, `src/blockindex.c` (nuevos) | Índice por columna de chunk sobre `block_changes` |
| `src/procedures.c`, `src/worldgen.c` | Usan el índice en lugar de recorrer todo el array; eventos de entrada y salida |
| `src/serialize.c` | Instantánea del mundo con cabecera y versión; guardado por lotes vía plataforma |
| `src/platform_posix.c` (nuevo) | Plataforma para Mac/Linux: `malloc`, pthread mutex, `clock_gettime`, archivo `world.bin` |
| `include/globals.h`, `src/globals.c` | Nuevos límites; `block_changes` pasa a ser un puntero con capacidad en tiempo de ejecución |
| `third_party/miniz/` (nuevo) | miniz 3.0.2 amalgamado (`miniz.c`, `miniz.h`, `LICENSE`) |
| `build.sh` | Compila también `third_party/miniz/miniz.c`, con `-Ithird_party/miniz` |
| `tests/unit/` (nuevo) | Pruebas en C, cada una un ejecutable con `main` y `assert`; `tests/unit/run.sh` las compila y ejecuta |
| `tests/e2e/mcbot.py`, `tests/e2e/test_*.py` (nuevos) | Cliente mínimo del protocolo 772 y pruebas extremo a extremo con `unittest` |
| `src/CMakeLists.txt` | Componente ESP-IDF: lista explícita de fuentes, `INCLUDE_DIRS "../include" "../third_party/miniz"` y sin dependencias de WiFi |

---

### Task 1: Repos, entorno y línea base medida

**Files:**
- Create: fork `mquerostudio/bareiron`, rama `kode-dot`, clonado en `~/Documents/git/bareiron`.
- Create: repo `mquerostudio/kodecraft` (público, GPL-3.0), clonado en `~/Documents/git/kodecraft`, con `README.md`, `LICENSE` y `docs/superpowers/{specs,plans}/` copiados desde la carpeta de esta sesión.
- Create: `tests/unit/run.sh`, `tests/e2e/mcbot.py`, `tests/e2e/test_baseline.py`.

**Interfaces:**
- Produces: `mcbot.Bot(host, port, name)` con `status() -> dict` (JSON del ping), `login() -> None` (hasta el estado Play), `play_for(seconds) -> None` (responde keep-alives y teleport confirm), `bytes_received: int`, `chunks_received: int`, `close(abrupt=False)`. El bot detecta Set Compression (login, `0x03`) y desde ahí lee y escribe tramas comprimidas con `zlib`.
- Produces: `tests/e2e/server.py` con `ServerProcess(port, world_dir)` (`start()`, `stop()` con SIGINT, `log` capturado) para lanzar `./bareiron`.

- [ ] **Step 1: Fork y clones.**
  `gh repo fork p2r3/bareiron --clone=false`, clonar en `~/Documents/git/bareiron` y crear la rama `kode-dot` desde `main`. `gh repo create mquerostudio/kodecraft --public --license gpl-3.0 --clone` en `~/Documents/git/kodecraft`. Copiar spec y planes a `docs/superpowers/`, añadir `.superpowers/` a `.gitignore` y hacer commit.
- [ ] **Step 2: Herramientas.**
  `brew install openjdk@21`. Ejecutar `./extract_registries.sh` (o el proceso manual del README con el jar 1.21.8) y `node build_registries.js`.
  Esperado: existen `include/registries.h` y `src/registries.c`, y `git status` no los lista.
- [ ] **Step 3: Compilación en PC sin cambios.**
  `./build.sh` (quitando de él la ejecución final `./bareiron` y moviéndola a `run.sh`).
  Esperado: el binario `bareiron` se genera sin errores.
- [ ] **Step 4: Escribir `mcbot.py` y `test_baseline.py`.**
  `test_baseline_join` lanza el servidor, entra con un bot, juega 5 s e imprime `bytes_received` y `chunks_received`.
  Asserts: `chunks_received == 25` (`VIEW_DISTANCE` 2) y `bytes_received > 3_000_000` (sin compresión).
- [ ] **Step 5: Ejecutar.**
  `python3 -m unittest tests/e2e/test_baseline.py -v`.
  Esperado: PASS. Anotar la cifra de bytes por entrada en `docs/perf.md` como línea base.
- [ ] **Step 6: Prueba con un cliente real.**
  Minecraft 1.21.8 → "Multijugador" → `localhost`. Esperado: se entra, se ve el mundo y se puede picar un bloque.
- [ ] **Step 7: Commit** (`tests/`, `build.sh`, `docs/perf.md`) y `git push -u origin kode-dot`.

### Task 2: API pública y ciclo de vida (`bareiron_run` / `bareiron_request_stop`)

**Files:**
- Create: `include/bareiron.h`, `src/server.c`, `src/platform_posix.c`, `tests/unit/test_lifecycle.c`.
- Modify: `src/main.c` (el bucle y el arranque de sockets salen a `server.c`; borrar `wifi_init`, `app_main` y `bareiron_main`), `src/globals.c` (borrar `task_yield` de ESP), `include/globals.h` (borrar `WIFI_SSID`/`WIFI_PASS`; `task_yield()` llama a `platform->yield`), `include/tools.h` (`get_program_time` llama a `platform->now_us`), `src/serialize.c` (sin `exit`).

**Interfaces:**
- Produces (en `include/bareiron.h`):
  ```c
  typedef struct {
    void   *(*alloc_large) (size_t size);   // big buffers (PSRAM on the Dot); NULL on failure
    void    (*free_large) (void *ptr);
    void    (*lock) (void);                  // guards the status snapshot
    void    (*unlock) (void);
    int64_t (*now_us) (void);                // monotonic microseconds
    void    (*yield) (void);                 // called once per loop iteration
    void    (*on_event) (const BareironEvent *event); // may be NULL
    int     (*save_world) (const void *data, size_t len);            // 0 = ok
    int     (*load_world) (void *data, size_t capacity, size_t *len); // 0 = ok, 1 = no world yet, <0 = error
  } BareironPlatform;

  typedef struct {
    uint16_t port;               // 25565
    uint32_t world_seed;         // used only when there is no saved world
    uint32_t max_block_changes;  // capacity of block_changes
    const BareironPlatform *platform;
  } BareironConfig;

  int  bareiron_run (const BareironConfig *config);  // blocks; 0 after a requested stop, <0 on startup error
  void bareiron_request_stop (void);                  // any thread; run returns within 1 s
  const BareironPlatform *bareiron_posix_platform (void);
  ```
- Códigos de error de `bareiron_run`: `-1` socket, `-2` bind, `-3` listen, `-4` sin memoria, `-5` mundo corrupto.

- [ ] **Step 1: Escribir el test que falla.**
  `test_run_and_stop`: un hilo llama a `bareiron_run` con `port = 25599` y plataforma posix (`save_world`/`load_world` en un directorio temporal). Tras 200 ms otro hilo llama a `bareiron_request_stop()`.
  Asserts: `bareiron_run` devuelve `0` en menos de 1,5 s. Una segunda llamada a `bareiron_run` con la misma config también arranca y para con `0`, porque el puerto se libera.
- [ ] **Step 2: Escribir `test_bind_error_returns`.**
  Ocupar el puerto 25598 con otro socket. Assert: `bareiron_run` devuelve `-2` y el proceso sigue vivo.
- [ ] **Step 3: Ejecutar.**
  `tests/unit/run.sh test_lifecycle`. Esperado: FAIL (no existe `bareiron_run`).
- [ ] **Step 4: Implementar.**
  - `bareiron_run` guarda `config`, reinicia **todas** las globales listadas abajo y aplica `splitmix64` a una copia de la semilla, no a la global (arregla que cada arranque genere otro mundo).
  - Reserva `block_changes` con `alloc_large(max_block_changes * sizeof(BlockChange))`.
  - El bucle sale cuando se ha pedido parar (flag `volatile`/atómico), cierra todos los clientes y el socket, y libera la memoria.
  - Globales a reiniciar: `block_changes`, `block_changes_count`, `player_data`, `player_data_count`, `mob_data`, `client_states`, `client_count`, `world_time`, `server_ticks`, `rng_seed`, `world_seed`, `total_bytes_received`, `last_disk_sync_time`.
- [ ] **Step 5: `main()` de PC.** Rellena `BareironConfig` con los valores por defecto de `globals.h` y llama a `bareiron_run`. SIGINT llama a `bareiron_request_stop`.
- [ ] **Step 6: Ejecutar** los tests unitarios y `test_baseline`. Esperado: PASS los dos.
- [ ] **Step 7: Commit:** `feat: embeddable lifecycle API (bareiron_run/request_stop), no exit()`.

### Task 3: Capa de red — salida con búfer y sin bloqueo

**Files:**
- Create: `include/net.h`, `src/net.c`, `tests/unit/test_net_tx.c`.
- Modify: `src/tools.c` (`send_all` → `net_write`; los `write*` no cambian de firma), `src/server.c` (llama a `net_open`/`net_close`/`net_flush` y pone `TCP_NODELAY` al aceptar), `src/procedures.c:198-211` (`disconnectClient` llama a `net_close`).

**Interfaces:**
- Produces:
  ```c
  #define NET_TX_CAP (4u * 1024 * 1024)
  int     net_open (int fd);                 // 0 ok, -1 no free slot
  void    net_close (int fd);                // frees buffers; closes nothing
  ssize_t net_write (int fd, const void *buf, size_t len); // appends; returns len, or -1 if fd unknown
  int     net_flush (int fd);                // frame + non-blocking send; 0 ok, -1 peer gone, -2 over NET_TX_CAP
  void    net_flush_all (void);
  size_t  net_pending (int fd);              // bytes waiting in the wire buffer
  ```
- Los búferes se reservan con `platform->alloc_large` y crecen por duplicación hasta `NET_TX_CAP`.

- [ ] **Step 1: Tests que fallan** (con `socketpair(AF_UNIX, SOCK_STREAM)` y el extremo del servidor en `O_NONBLOCK`):
  - `test_write_is_buffered_until_flush`: `net_write` de 3 bytes; el otro extremo no lee nada; `net_flush`; lee exactamente esos 3 bytes.
  - `test_unknown_fd_is_ignored`: `net_write(-1, ...)` devuelve `-1` y no hay caída. Hoy pasa en `handlePlayerJoin`, que escribe a jugadores offline.
  - `test_peer_not_reading_hits_cap`: escribir tramas hasta superar `NET_TX_CAP` sin leer el otro extremo. Assert: `net_flush` devuelve `-2` y la llamada no tarda más de 50 ms.
- [ ] **Step 2: Ejecutar.** Esperado: FAIL.
- [ ] **Step 3: Implementar `net.c` (salida).**
  - Tabla de `MAX_CONNECTIONS` entradas indexada por búsqueda de fd.
  - `net_flush` hace `send` con `MSG_DONTWAIT` hasta `EAGAIN` y deja el resto en el búfer.
  - `send_all` en `tools.c` llama a `net_write` y no reintenta ni desconecta.
- [ ] **Step 4: Arreglar la desconexión.**
  El bucle de `server.c` es el único que decide desconectar. Después de procesar cada paquete y de cada tick, llama a `net_flush` de todos los clientes; un `-1`/`-2` lleva a `disconnectClient(&clients[i], ...)` sobre el hueco real. Así desaparece el doble `close()` y el doble decremento de `client_count`.
- [ ] **Step 5: Ejecutar.** Unit + `test_baseline`. Esperado: PASS.
- [ ] **Step 6: Commit:** `feat(net): per-client tx buffers, non-blocking flush, TCP_NODELAY; fix double close`.

### Task 4: Capa de red — entrada por tramas completas

**Files:**
- Modify: `src/net.c`, `include/net.h`, `src/tools.c` (`recv_all`/`discard_all` leen del paquete actual), `src/varnum.c` (sin cambios de firma), `src/server.c` (bucle nuevo).
- Create: `tests/unit/test_net_rx.c`.

**Interfaces:**
- Produces:
  ```c
  int  net_pump (int fd);                         // read what's available; 0 ok, -1 peer gone/error
  int  net_next_packet (int fd, int *length);     // 1 = a whole packet is current, 0 = not yet, -1 = bad frame
  ssize_t net_read (int fd, void *buf, size_t n); // reads from the current packet; returns < n at its end
  void net_finish_packet (int fd);                // drops unread bytes of the current packet
  ```
- `recv_all(fd, buf, n, require_first)` pasa a ser `net_read`, que nunca espera. `total_bytes_received` sigue contando, para que la comprobación de longitud de `handlePacket` funcione igual.

- [ ] **Step 1: Tests que fallan.**
  - `test_partial_frame_waits`: el otro extremo envía la mitad de una trama. Assert: `net_next_packet == 0`. Tras enviar el resto, `== 1` con `length` correcto.
  - `test_two_frames_in_one_read`: dos tramas en un único `write`. Assert: se obtienen en orden, cada una con su longitud.
  - `test_read_past_end_is_short`: `net_read` pide más bytes de los que quedan en el paquete. Assert: devuelve lo que queda y no bloquea.
  - `test_oversized_frame_rejected`: una trama que anuncia más de 2 MiB devuelve `-1`.
- [ ] **Step 2: Ejecutar.** Esperado: FAIL.
- [ ] **Step 3: Implementar.**
  - El bucle de `server.c` por cliente es: `net_pump` → mientras `net_next_packet == 1`: leer el ID con `readVarInt` → `handlePacket` → `net_finish_packet`. Hay un máximo de 8 paquetes por cliente y vuelta, para ser justo con los demás.
  - Los ticks se ejecutan cuando toca **aunque no haya clientes**.
  - Se conserva el caso del ping heredado (`length == 254 && packet_id == 122`).
- [ ] **Step 4: Ejecutar.** Unit + `test_baseline` + entrar con el cliente real. Esperado: PASS y el juego funciona igual.
- [ ] **Step 5: Commit:** `feat(net): frame-complete rx, no busy-wait reads`.

### Task 5: Compresión del protocolo

**Files:**
- Create: `third_party/miniz/{miniz.c,miniz.h,LICENSE}` (release 3.0.2, amalgamado), `tests/unit/test_net_compression.c`, `tests/e2e/test_compression.py`.
- Modify: `src/net.c` (enmarcado comprimido en las dos direcciones), `src/packets.c` (nuevo `sc_setCompression`), `src/main.c` (en `STATE_LOGIN`, `sc_setCompression` antes de `sc_loginSuccess`), `include/globals.h` (`#define ENABLE_COMPRESSION`, `#define COMPRESSION_THRESHOLD 256`), `build.sh`, `src/CMakeLists.txt`.

**Interfaces:**
- Produces:
  - `void net_enable_compression (int fd, int threshold);`. Las tramas ya escritas antes de la llamada salen sin comprimir, y las posteriores en formato comprimido. Internamente se enmarca lo pendiente antes de cambiar el modo.
  - `int sc_setCompression (int client_fd, int threshold);` (login, ID `0x03`).
- El compresor `tdefl` (nivel 1) y el descompresor `tinfl` se reservan una sola vez con `alloc_large`.

- [ ] **Step 1: Tests que fallan.**
  - `test_small_packet_uncompressed_format`: con compresión activa, una trama de 10 bytes sale como `[len][0x00][datos]`.
  - `test_large_packet_roundtrip`: una trama de 150 000 bytes sale como `[len][150000][deflate]`; al inflarla con `tinfl` se obtienen los bytes originales.
  - `test_frames_before_enable_stay_plain`: escribir A, llamar a `net_enable_compression` y escribir B. Assert: A sale en formato plano y B en comprimido.
  - `test_rx_compressed_frame`: el otro extremo envía una trama comprimida (`[len][data_len][deflate]`). Assert: `net_read` entrega los datos inflados.
- [ ] **Step 2: Ejecutar.** Esperado: FAIL.
- [ ] **Step 3: Implementar.** Enmarcado comprimido en `net_flush` y desenmarcado en `net_next_packet`, con un búfer de paquete actual en `alloc_large` que crece hasta 2 MiB.
- [ ] **Step 4: Test e2e `test_join_bytes_with_compression`.**
  Assert: `chunks_received == 25` y `bytes_received < 0.25 × línea base` (la cifra de `docs/perf.md`).
- [ ] **Step 5: Ejecutar** todo y entrar con el cliente real. Esperado: PASS. Anotar en `docs/perf.md` los bytes por entrada con compresión.
- [ ] **Step 6: Commit:** `feat(net): protocol compression (threshold 256, miniz level 1)`.

### Task 6: Índice de cambios de bloques y límites nuevos

**Files:**
- Create: `include/blockindex.h`, `src/blockindex.c`, `tests/unit/test_blockindex.c`.
- Modify:
  - `src/procedures.c`:
    - `getBlockChange` (`:478-492`) y la búsqueda del cofre (`:1264-1270`) usan `bc_find`;
    - `makeBlockChange` (`:512-646`) usa `bc_find` y mantiene el índice;
    - el hueco libre sale de `bc_first_free`.
  - `src/worldgen.c:464-485`: la superposición recorre `bc_column`.
  - `include/globals.h`: `MAX_PLAYERS 64`, `#define MAX_CONNECTIONS 8`, `MAX_MOBS 16`; `MAX_BLOCK_CHANGES` pasa a ser el valor por defecto de la config (20000).
  - `src/globals.c`: `BlockChange *block_changes; uint32_t block_changes_cap;`.
  - `clients[]` y `client_states[]` se dimensionan con `MAX_CONNECTIONS`.

**Interfaces:**
- Produces:
  ```c
  int  bc_index_init (uint32_t capacity);          // 0 ok, -1 no memory (alloc_large)
  void bc_index_free (void);
  void bc_index_rebuild (void);                    // after loading a world
  int  bc_find (short x, uint8_t y, short z);      // index into block_changes, or -1
  void bc_on_set (int index);                      // entry index now holds a block (chest entries: only the chest itself)
  void bc_on_clear (int index);
  int  bc_first_free (int run_length);             // lowest index with run_length consecutive 0xFF entries, or -1
  int  bc_column (short cx, short cz, const int **indices); // count of entries in chunk column (x>>4, z>>4)
  ```
- Los 14 slots de contenido que siguen a un cofre **no** se indexan. `bc_find` solo devuelve entradas de bloque.

- [ ] **Step 1: Tests que fallan.**
  - `test_find_after_set_and_clear`.
  - `test_column_lists_only_that_column`: un bloque en (15,64,0) y otro en (16,64,0) caen en columnas distintas.
  - `test_chest_contents_not_indexed`: un cofre en `i` y 14 slots de datos. Assert: `bc_find` en las coordenadas del cofre devuelve `i`, y nada devuelve `i+1..i+14`.
  - `test_first_free_finds_run_of_15`.
  - `test_rebuild_matches_incremental`: añadir 1000 cambios aleatorios, reconstruir y comparar con el índice incremental.
  - `test_200k_changes_lookup_fast`: con 200 000 entradas, 10 000 `bc_find` tardan menos de 50 ms en el Mac.
- [ ] **Step 2: Ejecutar.** Esperado: FAIL.
- [ ] **Step 3: Implementar.**
  - Tabla hash abierta de columnas (clave `(cx<<16)|(cz&0xFFFF)`) con vectores de índices, todo en `alloc_large`.
  - `bc_first_free` mantiene una pista del menor hueco y la avanza en lugar de recorrer desde 0.
- [ ] **Step 4: Ejecutar** unit + e2e. Con el cliente real: poner y quitar bloques, poner un cofre, guardar objetos en él, salir y volver a entrar. Esperado: todo persiste en memoria.
- [ ] **Step 5: Commit:** `perf: column index for block changes; raise limits (64 remembered, 8 connections)`.

### Task 7: Estado y eventos para la interfaz

**Files:**
- Modify: `include/bareiron.h`, `src/server.c`, `src/procedures.c` (`handlePlayerJoin` y `handlePlayerDisconnect` emiten eventos y actualizan la instantánea), `src/packets.c:30-33` (la respuesta de estado incluye `players.online` y `players.max`).
- Modify: `src/main.c` (el `main()` de PC). Su `on_event` imprime una línea `EVENT {"type":"join","name":"bot1"}` por evento, y con SIGUSR1 imprime `STATUS {...}` con `bareiron_get_status` en JSON. `ServerProcess` (Task 1) ofrece `events()` y `status()` leyendo esas líneas.
- Create: `tests/e2e/test_status_api.py`.

**Interfaces:**
- Produces:
  ```c
  typedef enum { BAREIRON_EVT_READY, BAREIRON_EVT_PLAYER_JOIN, BAREIRON_EVT_PLAYER_LEAVE,
                 BAREIRON_EVT_WORLD_SAVED, BAREIRON_EVT_STOPPED, BAREIRON_EVT_ERROR } BareironEventType;
  typedef struct { BareironEventType type; char name[16]; int code; } BareironEvent;
  typedef struct { char name[16]; short x; uint8_t y; short z; int64_t joined_us; } BareironPlayerInfo;
  typedef struct {
    uint8_t  running;
    uint8_t  player_count;                         // players in game (not status pings)
    BareironPlayerInfo players[MAX_CONNECTIONS];
    uint16_t world_time;                           // 0..23999
    int64_t  started_us;
    uint32_t loop_max_us;                          // worst loop iteration in the last second
    uint32_t block_changes_used;
  } BareironStatus;
  void bareiron_get_status (BareironStatus *out);  // any thread; copies under platform->lock
  ```
- Los eventos se emiten desde el hilo del servidor. `on_event` no debe bloquear.

- [ ] **Step 1: Tests que fallan.**
  - `test_join_leave_events`: entra `bot1` y sale. Assert: las líneas `join bot1` y `leave bot1` llegan en ese orden.
  - `test_status_lists_players`: dos bots dentro. Assert: el volcado tiene `player_count == 2`, los nombres y una posición cerca de la aparición.
  - `test_server_list_shows_count`: `Bot.status()["players"]["online"] == 1` con un bot dentro.
- [ ] **Step 2: Ejecutar.** Esperado: FAIL.
- [ ] **Step 3: Implementar.**
  - La instantánea se actualiza al entrar o salir alguien, y una vez por segundo para las posiciones y `world_time`.
  - Se emite `READY` tras `listen`, y `STOPPED` justo antes de que `bareiron_run` vuelva.
  - `loop_max_us` se mide en cada vuelta y se publica cada segundo.
- [ ] **Step 4: Ejecutar.** Esperado: PASS.
- [ ] **Step 5: Commit:** `feat: status snapshot and join/leave events for host UIs`.

### Task 8: Guardado del mundo por lotes

**Files:**
- Modify: `src/serialize.c` (reescrito), `include/serialize.h`, `src/procedures.c` (las llamadas `writeBlockChangesToDisk`/`writeChestChangesToDisk`/`writePlayerDataToDisk` se quedan pero solo marcan `world_dirty = 1`), `src/server.c` (guardar cada 60 s si está sucio y al parar), `src/platform_posix.c` (`save_world` escribe `world.bin.tmp` y lo renombra; `load_world` lee `world.bin`).
- Create: `tests/unit/test_serialize.c`, `tests/e2e/test_persistence.py`.

**Interfaces:**
- Produces:
  ```c
  #define WORLD_MAGIC 0x4B435731u   // "KCW1"
  typedef struct { uint32_t magic; uint16_t version; uint16_t reserved;
                   uint32_t world_seed; uint32_t block_changes_count;
                   uint32_t player_count; uint32_t player_size; uint16_t world_time; } WorldHeader; // packed
  size_t worldSnapshotSize (void);
  size_t worldSnapshotWrite (uint8_t *dst, size_t capacity); // header + block_changes[0..count) + player_data[]
  int    worldSnapshotRead (const uint8_t *src, size_t len);  // 0 ok, -5 bad magic/version/size
  void   markWorldDirty (void);
  int    saveWorldNow (void);   // snapshot into an alloc_large buffer, then platform->save_world
  ```
- Antes de guardar, los `client_fd` de `player_data` se ponen a `-1` en la copia, no en el original.

- [ ] **Step 1: Tests que fallan.**
  - `test_snapshot_roundtrip`: 500 cambios (con un cofre) y 3 jugadores → write → reiniciar globales → read. Assert: los mismos bloques (vía `bc_find`), inventarios y semilla.
  - `test_bad_magic_rejected`: devuelve `-5`.
  - `test_truncated_rejected`: devuelve `-5`.
- [ ] **Step 2: Test e2e que falla: `test_stop_and_restart_keeps_world`** (Review Focus 5).
  Un bot entra y rompe el bloque que tiene bajo los pies (Player Action con status 0 y luego 2 sobre `(x, y-1, z)` de su posición sincronizada). Se envía SIGINT con el bot dentro.
  Asserts:
  - el log contiene `EVENT {"type":"world_saved"...}` y el proceso sale con 0;
  - el segundo arranque imprime `World loaded: N block changes, seed XXXXXXXX` con `N >= 1` y la misma semilla que el primero;
  - el `STATUS` del segundo arranque tiene `block_changes_used >= 1`.

  (El contenido exacto de los bloques lo cubre `test_snapshot_roundtrip`; el bot no decodifica chunks.)
- [ ] **Step 3: Ejecutar.** Esperado: FAIL.
- [ ] **Step 4: Implementar.**
  - Si `load_world` devuelve 1 (no hay mundo), se usa la semilla de la config.
  - Si devuelve 0, la semilla sale del archivo.
  - Si devuelve <0 o `worldSnapshotRead` falla, `bareiron_run` devuelve `-5` **sin sobrescribir** el archivo.
  - Se borran el código de LittleFS y el de `DEV_ENABLE_BEEF_DUMPS` del serializador.
- [ ] **Step 5: Ejecutar.** Esperado: PASS.
- [ ] **Step 6: Commit:** `feat: batched world snapshots via platform hooks (60 s + on stop)`.

### Task 9: Carga con 6 jugadores y casos difíciles

**Files:**
- Create: `tests/e2e/test_load.py`, `tests/e2e/test_edge_cases.py`.
- Modify: lo que los tests destapen.

- [ ] **Step 1: Escribir los tests.**
  - `test_six_bots_one_minute`: 6 bots entran escalonados (1 s) y caminan en círculo (Set Player Position cada 50 ms, radio 40 bloques, cruzando chunks) durante 60 s.
    Asserts: ninguno se desconecta, todos reciben keep-alive al menos cada 20 s, y el log de `loop_max_us` nunca supera 250 000.
  - `test_stalled_client_does_not_freeze_others` (Review Focus 1): 3 bots normales y 1 que entra y deja de leer el socket. Assert: el atascado se desconecta antes de 30 s y los otros 3 siguen recibiendo keep-alives sin huecos de más de 20 s.
  - `test_abrupt_close_during_join` (Review Focus 2): un bot cierra con RST (`SO_LINGER` 0) justo tras Login Success, 10 veces seguidas. Assert: el servidor sigue vivo, `player_count == 0` al final, y un bot nuevo entra bien.
  - `test_rejoin_same_name_single_entry` (Review Focus 3): `bot1` entra, camina hasta 20 bloques en X desde la aparición, cierra con RST y vuelve a entrar en menos de 1 s.
    Asserts:
    - la Synchronize Player Position de la nueva entrada lo deja a menos de 2 bloques de donde estaba;
    - `STATUS` muestra `player_count == 1` y `bot1` una sola vez.
  - `test_status_pings_do_not_leak_slots` (Review Focus 4): 50 pings de estado seguidos y luego 6 bots entran. Assert: los 6 entran y `player_count == 6`.
- [ ] **Step 2: Ejecutar.** `python3 -m unittest discover tests/e2e -v`. Arreglar lo que falle, con un commit por arreglo (`fix: ...`).
- [ ] **Step 3: Prueba manual con el cliente real y 5 bots a la vez.** Jugar 10 minutos: moverse lejos, picar, construir, usar un cofre, morir y reaparecer. Esperado: sin tirones visibles ni desconexiones.
- [ ] **Step 4: Anotar en `docs/perf.md`** los bytes por entrada, `loop_max_us` (p50/máx) con 6 bots y la memoria total reservada con `alloc_large`.
- [ ] **Step 5: Commit y push** a `mquerostudio/bareiron` rama `kode-dot`.

### Task 10: Componente ESP-IDF listo para el Plan 2

**Files:**
- Modify: `src/CMakeLists.txt`.
- Create: `idf_component.yml` en la raíz del fork (para usarlo como componente por git) y `tests/esp/` con un proyecto ESP-IDF mínimo para el objetivo `esp32p4`. Su `main.c` arranca `bareiron_run` sin red, solo para comprobar que compila y enlaza.

**Interfaces:**
- Produces: el componente `bareiron` que el Plan 2 usará desde `kodecraft/components/bareiron` (submódulo). `REQUIRES lwip esp_timer` y nada de `esp_wifi` ni `nvs_flash`.

- [ ] **Step 1: Configurar el CMake del componente.**
  `idf_component_register(SRCS <lista explícita de src/*.c sin platform_posix.c> third_party/miniz/miniz.c INCLUDE_DIRS include third_party/miniz REQUIRES lwip esp_timer)`, con `registries.c` incluido si existe y un error claro si falta.
- [ ] **Step 2: Compilar.**
  `. ~/.espressif/v6.1/esp-idf/export.sh && cd tests/esp && idf.py set-target esp32p4 && idf.py build`.
  Esperado: compila sin warnings nuevos del núcleo.
- [ ] **Step 3: Comprobar el tamaño de SRAM.**
  `idf.py size` y anotar en `docs/perf.md` el `.bss`/`.data` interno del componente. Esperado: menos de 64 KB, porque `block_changes` y los búferes ya no son estáticos. Si los arrays de registros generados quedan en DRAM, marcarlos `const` en `build_registries.js` (así quedan en flash) y volver a medir.
- [ ] **Step 4: Commit y push:** `build: ESP-IDF component for esp32p4 (no wifi), const registries`.

---

## Self-review

- **Cobertura del spec (sección "Cambios al núcleo"):**
  - plataforma → T2 y T10;
  - bugs → T3 (doble `close`) y T2 (`exit`, semilla); el hueco de jugador recordado se resuelve subiendo `MAX_PLAYERS` a 64 en T6;
  - red y compresión → T3-T5;
  - límites e índice → T6;
  - ganchos → T7;
  - supervivencia, MOTD y brand → Global Constraints, aplicados en T2;
  - guardado por lotes → T8 (la SD real va en el Plan 3);
  - rendimiento con 6 jugadores → T9.
  - El mapa en vivo, la interfaz y la publicación van en los Planes 2-3.
- **Consistencia de tipos:** `BareironPlatform`, `BareironConfig`, `BareironStatus`, `BareironEvent` y `MAX_CONNECTIONS` se definen en T2/T6/T7 y se usan con esos nombres en T8-T10.
