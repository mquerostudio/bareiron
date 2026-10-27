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
