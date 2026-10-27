# KodeCraft — diseño

Fecha: 2026-10-27 · Autor: Manuel Quero (MQuero) con Claude

KodeCraft es un servidor de Minecraft Java que corre en el Kode Dot nuevo (ESP32-P4). Es un port de [p2r3/bareiron](https://github.com/p2r3/bareiron) (GPL-3.0, Minecraft 1.21.8, protocolo 772) como app del Kode Dot, construida sobre el SDK oficial ([kodediy/kode-sdk](https://github.com/kodediy/kode-sdk)) y publicada en apps.kode.diy.

En noviembre de 2025 Manuel portó bareiron al Kode Dot antiguo (ESP32-S3) para un vídeo. Ese código se perdió y aquella versión iba sobrecargada. Esta es una versión nueva, no una recuperación.

## Intención

- **El efecto "wow":** un servidor de Minecraft de verdad en algo que cabe en el bolsillo. Tiene que lucir en pantalla y en cámara (vídeo de MQuero).
- **Jugar de verdad en la oficina:** 5-6 personas a la vez, cada una con su Minecraft vanilla, sin tirones ni desconexiones, y que el mundo siga ahí otro día.
- **Hacer crecer el SDK de Jorge:** cada carencia del SDK que encontremos se arregla en el SDK con un PR a `kodediy/kode-sdk`, no con un apaño dentro de la app.

## Objetivos

1. 5-6 jugadores simultáneos en supervivencia durante al menos 1 hora seguida, sin desconexiones ni congelaciones del servidor.
2. Entrar al servidor y moverse por el mundo tiene que sentirse fluido. La red es el cuello de botella, no la CPU (ver "Rendimiento").
3. El mundo (bloques cambiados, inventarios, cofres) persiste entre sesiones y entre reinicios.
4. Interfaz en el Kode Dot con pantalla de inicio, Panel y Mapa en vivo, según las maquetas aprobadas.
5. App publicable en apps.kode.diy (slug `kodecraft`, categoría `game`, con icono).

## No-objetivos

- Servidor para Internet o para gente de fuera de la red local.
- Autenticación con cuentas de Mojang/Microsoft. Es modo offline, como bareiron.
- Compatibilidad con clientes modificados (Fabric, etc.) u otras versiones distintas de 1.21.8.
- Paridad con vanilla: se hereda lo que bareiron hace y lo que no.
- Modo punto de acceso (que el Kode Dot cree su propia WiFi). Se usa el WiFi de la oficina.

## Anti-objetivos (fallaría aunque "funcione")

- Que un jugador con mala conexión congele el servidor para todos (bareiron hoy puede congelarse hasta 15 s).
- Que el Kode Dot se reinicie o se cuelgue por un error del servidor: bareiron llama a `exit()` en varios sitios.
- Usar marca o recursos de Mojang: capturas o texturas del juego, o "Minecraft" como nombre de la app. Se puede decir "compatible con Minecraft Java 1.21.8" en la descripción.
- Parchear el SDK dentro del repo de la app.

## Decisiones de Manuel

- Nombre **KodeCraft**, para la app y para el repo. Repo **`mquerostudio/kodecraft`**, en su cuenta de GitHub.
- Port de bareiron. Se puede modificar libremente; las mejoras que funcionen se proponen al repo original.
- Para el Kode Dot nuevo (P4), sobre kode-sdk (el SDK de Jorge), publicable en apps.kode.diy.
- 5-6 jugadores simultáneos en la oficina. El mundo se guarda entre sesiones.
- Red: WiFi de la oficina.
- El servidor **no arranca solo**: hay una pantalla de inicio con un botón "Iniciar servidor" (para pulsarlo en el vídeo).
- Con el servidor en marcha hay dos páginas:
  - **Panel:** fondo de paisaje de bloques desenfocado, la IP en grande y la lista de jugadores con el tiempo conectado, sobre paneles de cristal.
  - **Mapa en vivo:** minimapa visto desde arriba con los jugadores en tiempo real y un aviso cuando alguien entra.
- Se descarta la pantalla "Escaparate".
- **Licencia: fuera del alcance.** La revisión detectó un posible conflicto: bareiron es GPL-3.0 sin excepciones, mientras que el binario incluye RFAL de ST (SLA0051) y kode-sdk es privado. Manuel decide no abordarlo en este proyecto: no bloquea ni el desarrollo ni la publicación, y no se planifica trabajo para ello.

Referencia visual: `.superpowers/brainstorm/*/content/pantalla-principal-v2.html` (maquetas aprobadas).

## Restricciones de la plataforma

Todas verificadas en kode-sdk v00.00.27 (commit 54481d4).

- **Qué es una app:** una app del Kode Dot es un firmware ESP32-P4 completo con su propia copia del SDK (QP/C Active Objects sobre FreeRTOS). Mientras corre, kodeOS no corre. Se instala por el buzón `app_pending` (0x1C00000, máx. ~4 MB).
- **Salir:** se vuelve a kodeOS con `kode_exit_to_os()` o con el gesto de sistema. Mantener BOT ~600 ms es "home", así que la app no debe usar BOT largo para nada.
- **Compilación:** ESP-IDF v6.1 en local (instalado en el Mac de Manuel). La plantilla de partida es `kodediy/kode-app-template`, que fija kode-sdk v00.00.25.
  - Hay que subirla a v00.00.27, porque la v00.00.25 necesita un arreglo de kui que la v00.00.27 ya trae.
  - No se tocan las líneas bloqueadas: la tabla de particiones y `CONFIG_KODEOS_BOOT_ROLE_APP=y`.
- **Radio:** la plantilla trae la radio apagada (`CONFIG_KODEOS_RADIO=n`, `CONFIG_ESP_HOSTED_ENABLED=n`). Hay que activarla como en `examples/network/sdkconfig.defaults`.
  - Apagar lo que no se usa: `CONFIG_KODEOS_CHANNEL=n`, `CONFIG_KODEOS_BLE=n` y `CONFIG_BT_ENABLED=n`.
- **WiFi:** solo modo estación. Las redes guardadas en kodeOS viven en el "vault" del C5 y la app se une sola (`KODEOS_WIFI_AUTOJOIN`). La app no lleva SSID ni contraseña.
  - La IP llega por `WIFI_STATE_SIG` (`WifiStateEvt.ip4`) o por `net_info_request()`.
  - No se hace `bind` hasta tener IP.
- **Sockets:** sockets BSD de lwIP normales en el P4. El C5 solo transporta tramas por SDIO 4-bit a 40 MHz.
  - El SDK no ajusta `CONFIG_LWIP_*`; los ajusta la app en su `sdkconfig.defaults`.
  - El modo WiFi rápido (`wifi_fast_request(true)`) hay que pedirlo de nuevo tras cada reconexión.
- **Memoria:**
  - 640 KB de SRAM interna, de los que el SDK usa ~350 KB, y 32 MB de PSRAM. `malloc` ya va a PSRAM.
  - Las variables globales y estáticas y las pilas de tareas van a SRAM interna: nada grande ahí.
  - Hay que vigilar `int_low` (≥ 64 KB libres).
- **CPU:** RISC-V de doble núcleo a 400 MHz. No hay núcleos reservados.
- **Pantalla:** 410×502 en vertical, con LVGL 9.6 a través de `kscreen`/`kui`.
  - `lv_*` solo desde el hilo dueño (`kscreen.h:11` manda sobre lo que diga kode-api.md).
  - Reglas de kui: esquinas redondeadas de 114 px sin contenido en las esquinas, y filas entre y=53 y y=448 con márgenes de 18 px.
- **Botones:** `BUTTON_EVT_SIG` (UP, DOWN, LEFT, RIGHT, TOP, BOT) y táctil.
- **Almacenamiento:**
  - Tarjeta SD en `/sdcard`. Solo AO_Storage puede tocar la VFS de la tarjeta, y AO_Radio la desmonta y la vuelve a montar mientras arranca la radio: no se abre nada hasta `KRADIO_UP`.
  - La SD y la radio comparten el controlador SDMMC, así que escribir mucho compite con la red.
  - La FAT interna (`/storage`, 2 MB) sirve como alternativa pequeña.
- **Licencia:** GPL-3.0 (heredada de bareiron). El repo es público.

## Arquitectura (decisión de Claude salvo donde se indica)

### Repositorios

- `mquerostudio/bareiron`: fork de p2r3/bareiron.
  - Rama `kode-dot` con los cambios de KodeCraft al núcleo: arreglos, red, rendimiento y ganchos.
  - Ramas de un solo tema para proponer arriba cada mejora, después de abrir un issue.
  - El núcleo sigue compilando en PC (build POSIX), que es la forma rápida de probar.
- `mquerostudio/kodecraft`: la app (proyecto ESP-IDF a partir de kode-app-template).
  - Incluye bareiron como submódulo git en `components/bareiron` (rama `kode-dot`).
  - Contiene la capa de plataforma Kode Dot, la interfaz, los recursos gráficos, la documentación y este diseño.
- Los registros de Minecraft (`registries.c/.h`) se generan en local con el script de bareiron (Java 21 + servidor vanilla 1.21.8) y no se suben al repo, igual que en el original. El README del repo explica cómo generarlos.

### Hilos y núcleos

- **Tarea del servidor** (FreeRTOS normal, no un AO, porque bloquea en sockets):
  - Fijada al núcleo 1.
  - Pila de 16-32 KB, porque la de 4 KB de bareiron no basta.
  - Se crea al pulsar "Iniciar servidor" y termina limpiamente al pulsar "Parar servidor", sin `exit()`.
- **AO de la app + kscreen** (interfaz LVGL): en el núcleo 0, como en la plantilla.
- **Tarea del mapa:** baja prioridad, núcleo 0. Pinta el minimapa en un búfer de PSRAM (ver "Mapa en vivo").
- **Comunicación servidor → interfaz:**
  - Una instantánea de estado protegida por mutex: estado, IP, puerto, jugadores (nombre, color, posición, hora de entrada), número de jugadores, hora del mundo y tiempo activo.
  - Eventos puntuales al AO de la app: jugador entra, jugador sale, servidor listo y error.
  - La interfaz nunca toca las variables globales de bareiron.

### Cambios al núcleo de bareiron

1. **Plataforma:**
   - Quitar `app_main`/`wifi_init` del núcleo. La app da la red y arranca el servidor con una API pequeña: `bareiron_start(config)`, `bareiron_stop()` y los ganchos de eventos.
   - Arreglar el arranque duplicado en cada `IP_EVENT_STA_GOT_IP`.
   - Sustituir los `exit()` por retornos de error.
   - Que el estado global se pueda reiniciar, para arrancar y parar varias veces sin que cambie la semilla del mundo.
2. **Bugs:**
   - Doble `close()` y doble decremento de `client_count` en `send_all`/`recv_all`: se pasa una copia del fd y el hueco de `clients[]` no se limpia.
   - El hueco de jugador recordado nunca se libera.
3. **Red:**
   - Búfer de envío por cliente en PSRAM, que se vacía en bloques, en lugar de un `send()` por campo. `TCP_NODELAY`.
   - Ningún `send`/`recv` puede bloquear el bucle: un cliente que no consume se desconecta cuando su búfer supera un límite, en vez de esperar 15 s.
   - **Compresión del protocolo** (paquete Set Compression con zlib/deflate). Es la mejora de más impacto, porque los chunks van sin comprimir: ~135-150 KB cada uno y ~3,5 MB por cada entrada al servidor.
4. **Límites y estructuras:**
   - `block_changes` en PSRAM, con un índice (hash) para `getBlockChange` y para la superposición por sección en lugar de búsquedas lineales.
   - Subir `MAX_BLOCK_CHANGES` a cientos de miles.
   - Subir los jugadores recordados (`MAX_PLAYERS` hoy cuenta UUIDs distintos, no conexiones) a 32-64.
   - Limitar las conexiones simultáneas a 8.
   - Los arrays de registros generados pasan a `const`, para que se queden en flash y no ocupen DRAM.
5. **Ganchos para la interfaz:** llamadas al entrar o salir un jugador (`handlePlayerJoin`/`handlePlayerDisconnect`) y una forma segura de copiar la lista de jugadores y sus posiciones.
6. **Juego:** modo **supervivencia** por defecto (decisión de Claude; Manuel puede cambiarlo). MOTD "KodeCraft". Semilla fija por mundo, guardada con el mundo.

### Persistencia

- El mundo se guarda en la SD, en `/sdcard/kodecraft/world.bin`. Hoy es un volcado de `block_changes` más `player_data`; con los límites nuevos ocupará unos pocos MB.
- Se guarda por lotes, no bloque a bloque como hace bareiron:
  - cada 60 s si hay cambios;
  - al pulsar "Parar servidor";
  - al salir de la app por la vía normal.
- La escritura no debe congelar el servidor: se copia la instantánea y se escribe fuera del bucle.
- Para escribir archivos grandes desde la app hace falta algo que el SDK no da bien hoy (ver "PRs previstos al SDK"). La solución va al SDK.
- **Sin tarjeta SD:** el servidor funciona sin guardar y la pantalla de inicio lo avisa.

### Mapa en vivo

- Vista cenital centrada en los jugadores, que se ajusta para que quepan todos. Cada píxel es un bloque (o 2×2), con colores por tipo de bloque superior: agua, arena, hierba, roca, nieve y hojas de árbol.
- El color sale del generador de terreno de bareiron (determinista) más los cambios de bloques. No necesita que los chunks se hayan enviado.
- Las teselas se calculan en la tarea del mapa y se guardan en caché en PSRAM. Se actualiza a 1-2 Hz.
- Marcadores de jugador con su color y su nombre. Aviso "X ha entrado al servidor".
- Si generar el mapa le quita rendimiento al servidor, gana el servidor: el mapa baja de frecuencia.

### Interfaz

Pantallas, todas con el fondo de paisaje pixel-art desenfocado y paneles de cristal:

1. **Inicio:**
   - título KodeCraft y botón grande "Iniciar servidor";
   - debajo, la red WiFi actual;
   - avisos si no hay red guardada ("conéctate a una red desde kodeOS") o si no hay SD ("el mundo no se guardará").
2. **Arrancando:** "Conectando al WiFi…" y "Preparando el mundo…".
3. **Panel (página 1):**
   - "Servidor en marcha", la IP en grande, el puerto y la versión;
   - la lista de jugadores con color y tiempo conectado;
   - pie con jugadores/máximo y tiempo activo;
   - botón "Parar servidor" (guarda el mundo y vuelve a Inicio).
4. **Mapa en vivo (página 2):** el minimapa, el aviso de entrada y un pie con jugadores y la hora del día del mundo.
5. **Error:** mensaje claro y botón de volver.

Detalles:
- Se cambia de página con LEFT/RIGHT o deslizando.
- Color de jugador: sale de un hash del nombre. No hay skins reales, porque requieren Internet y la API de Mojang.
- Fondo: ilustración pixel-art propia, en estilo de bloques pero sin texturas ni capturas de Mojang. Se guarda como imagen en flash, ya desenfocada para no gastar CPU.
- Icono de la app: propio, cuadrado, con el mismo estilo.

## Rendimiento: cómo sabremos que va bien

Escenario de prueba: 6 clientes vanilla 1.21.8 en el WiFi de la oficina, supervivencia, 1 hora.

- Entrar al servidor: el mundo se ve en menos de 5 s.
- Ticks del servidor sin retrasos visibles. El tiempo del bucle se mide y se muestra en el log.
- Ninguna desconexión ni congelación. Si un cliente se cae, los demás siguen.
- `int_low` siempre por encima de 64 KB.
- Primero se mide sin compresión (línea base) y después con ella, para el vídeo y para el PR.

## PRs previstos al SDK (kodediy/kode-sdk)

Se abren cuando el problema aparezca de verdad al construir, uno por tema:

1. Una forma sencilla de saber la IP y esperar a estar en línea desde una tarea normal.
2. Escribir y leer archivos grandes desde una tarea, sin el límite de 4 KB de `kode_fs`, respetando que solo AO_Storage toque la tarjeta.
3. Un preset o documentación de `CONFIG_LWIP_*` para apps que hacen de servidor.
4. Que el deseo de "WiFi rápido" se mantenga tras cada reconexión.
5. Errores de documentación detectados:
   - comentario de `kode_net_lookup` separado de su función en `kode.h`;
   - `examples/network` dice que las contraseñas van a NVS;
   - kode-api.md contradice a `kscreen.h` sobre el bloqueo de LVGL.
6. Subir la versión fija en kode-app-template a v00.00.27.

## Publicación

- apps.kode.diy: slug `kodecraft`, categoría `game`, icono cuadrado propio.
- La descripción dice "compatible con Minecraft Java 1.21.8", explica la limitación de modo offline y red local, y enlaza al repo (GPL).
- Primero como borrador; se envía a revisión cuando Manuel lo apruebe.

## Quién hace qué (decisión de Claude)

- **Repos:** Claude crea `mquerostudio/kodecraft` y el fork `mquerostudio/bareiron` con `gh`, en la cuenta de Manuel, como él pidió.
- **PRs al SDK:** la cuenta de Manuel tiene permisos de escritura en `kodediy/kode-sdk`. Claude sube ramas de un tema allí y abre los PRs; Jorge revisa y fusiona.
- **Mientras un PR del SDK está abierto (decidido por Manuel):** `idf_component.yml` de la app apunta a la rama de ese PR. Cuando Jorge lo fusiona y publica versión, vuelve a la versión etiquetada. Así no se parchea el SDK dentro de la app ni se espera.
- **Orden de trabajo (sin fecha; Manuel: «no te preocupes de la fecha del vídeo»):**
  1. núcleo arreglado con red rápida y compresión, probado en PC;
  2. app en el Kode Dot con Inicio y Panel;
  3. guardar el mundo;
  4. mapa en vivo y pulido visual;
  5. prueba de una hora con 6 jugadores;
  6. borrador en apps.kode.diy.
- **Arte:** Claude dibuja el fondo pixel-art y el icono con el estilo de la maqueta aprobada. Manuel puede sustituirlos por los suyos cuando quiera.
- **apps.kode.diy:** Claude sube la app como borrador con la clave de agente de este Mac (`~/.config/kode/kode_pat`). El envío a revisión, solo cuando Manuel lo apruebe.

## Riesgos que se comprueban el primer día

- **Aislamiento de clientes en el WiFi de la oficina:** si la red no deja que los dispositivos se hablen entre sí, nada funciona. Se prueba con un servidor de prueba antes de nada.
- **Guardado al salir con el gesto de inicio:** el SDK da como mucho 3 s (`KODE_HOME_DEFER_MAX_MS`). El guardado cada 60 s limita lo que se puede perder si no da tiempo.

## Lo que se deja al constructor

- Qué implementación de deflate usar (miniz en ROM si existe en el P4, el componente zlib del registro u otra) y el umbral de compresión.
- Tamaños exactos de búferes, pilas, índice y límites, mientras se cumplan los objetivos y la memoria interna.
- El formato exacto del archivo del mundo (puede seguir siendo el volcado de bareiron con cabecera de versión).
- Diseño de detalle de las pantallas dentro del estilo aprobado.
- El orden de trabajo, siempre que el núcleo se pruebe primero en PC y después en el Kode Dot.
