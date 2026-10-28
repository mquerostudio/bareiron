# Rendimiento medido (KodeCraft / bareiron kode-dot)

Medido en un MacBook (macOS, clang -O2), servidor y bots en localhost.

| Medida | Línea base (upstream 02733be) | kode-dot |
|---|---:|---:|
| Bytes que recibe un jugador al entrar (5 s, 25 chunks) | 3 630 016 | 55 534 |
| Peor vuelta del bucle con el servidor vacío | — | ~25 µs |
| Peor vuelta del bucle con 6 bots caminando 60 s | — | 154 ms |
| Peor vuelta del bucle en la entrada de un jugador | — | ~160 ms |

La peor vuelta coincide con la entrada de un jugador: los 25 chunks se generan y comprimen de una vez, en una sola vuelta del bucle. En el Mac no se nota. En el ESP32-P4 (RISC-V a 400 MHz) puede ser varias veces más lenta y congelar a todos durante la entrada; hay que medirlo en el Plan 2 antes de decidir si se reparten los chunks entre vueltas.

## Memoria que se reserva con `alloc_large` (PSRAM en el Kode Dot)

| Qué | Tamaño |
|---|---:|
| `block_changes` | 6 B × capacidad (20 000 en PC = 120 KB; 500 000 = 3 MB) |
| Compresor deflate (`tdefl_compressor`) | 319 352 B, uno compartido |
| Descompresor inflate (`tinfl_decompressor`) | 8 376 B, uno compartido |
| Índice de columnas | 1024 columnas × 24 B al empezar, crece ×2 |
| Búferes de red por cliente | 4 KB al empezar; pico medido de 512 KB durante la entrada de un jugador (los paquetes se comprimen a medida que se escriben); se liberan al vaciarse |
| Instantánea del mundo al guardar | cabecera + 6 B × cambios + 64 × 221 B |

`player_data` (64 × 221 B = 14 KB) sigue siendo estático.

## ESP32-P4 (ESP-IDF 6.1, `tests/esp`, -Og)

| Componente | RAM interna (.bss + .data) | Flash (.text + .rodata) |
|---|---:|---:|
| `libbareiron.a` (núcleo + miniz) | 23 141 B (19 957 + 3 184) | 47 874 B |

Las tablas de registros ya caben de sobra en RAM interna, así que no hace falta marcarlas `const`.

## Sin pausas dentro de un chunk (Plan 4, Task 1)

`sc_chunkDataAndUpdateLight` llamaba a `task_yield()` 22 veces por chunk, y `spawnPlayer` otras 3. Venía de cuando el servidor compartía núcleo con todo lo demás. En el ESP32-P4 cada una es un `vTaskDelay(1)`, hasta 1 ms. El bucle principal ya cede el núcleo una vez por vuelta, y con un chunk por vuelta no hace falta más.

`tests/bench/chunk_bench.c` (Mac, -O2): un chunk completo, generado, enmarcado y comprimido, cuesta 0,43 ms. La generación de las 20 secciones son 0,21 ms (solo las 8 de abajo, 0,09 ms) y la compresión otro tanto.

| Mac, e2e | Antes | Ahora |
|---|---:|---:|
| Peor vuelta del bucle en una entrada | ~6 ms | 0,55 ms |
| Peor vuelta con 6 bots | ~48 ms | 4,0 ms |
| 25 chunks recibidos tras entrar | 0,21 s | 0,10 s |

## El cielo en una sección de un solo valor (Plan 4, Task 2)

`chunkTopY` da la Y más alta con algo en un chunk: el terreno más 8 de margen para árboles, el agua hasta 64 y cualquier cambio de bloque. Las secciones por encima salen como "todo aire" (6 bytes) en vez de 4096 bloques generados. `test_sky_predicate_is_exact` comprueba en 5 semillas que nunca se toma por cielo una sección con algo; y `test_chunk_packet_format` decodifica el paquete como el cliente.

| Mac | Tras la Task 1 | Ahora |
|---|---:|---:|
| Datos de un chunk sin comprimir | 91 792 B | 23 092 B (15 de 20 secciones son cielo) |
| Un chunk completo, comprimido | 0,43 ms | 0,19 ms |
| Peor vuelta en una entrada (e2e) | 0,55 ms | 0,21 ms |
| Peor vuelta con 6 bots (e2e) | 4,0 ms | 1,4 ms |
| Bytes recibidos al entrar | 55 534 | 50 648 |

Las pruebas e2e admiten `BAREIRON_TEST_PORT` para no chocar con otro servidor en 25565.
