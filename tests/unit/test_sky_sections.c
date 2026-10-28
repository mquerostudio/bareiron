// Sections above everything in a chunk go out as one value; the test that
// this is exact: whatever is called sky must really be all air
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "globals.h"
#include "registries.h"
#include "blockindex.h"
#include "procedures.h"
#include "worldgen.h"
#include "net.h"
#include "packets.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>

static void setup (void) {
  if (block_changes) free(block_changes);
  block_changes_cap = 1000;
  block_changes = malloc(block_changes_cap * sizeof(BlockChange));
  for (uint32_t i = 0; i < block_changes_cap; i ++) block_changes[i].block = 0xFF;
  block_changes_count = 0;
  bc_index_free();
  CHECK(bc_index_init(block_changes_cap) == 0);
}

static int all_air (void) {
  for (int i = 0; i < 4096; i ++) if (chunk_section[i] != B_air) return 0;
  return 1;
}

static void test_sky_predicate_is_exact (void) {
  setup();
  int sky = 0, checked = 0;
  for (uint32_t s = 1; s <= 5; s ++) {
    world_seed = 0x9E3779B9u * s;
    for (int c = 0; c < 400; c ++) {
      short cx = (short)((c % 20) - 10 + s * 40), cz = (short)((c / 20) - 10);
      uint8_t top = chunkTopY(cx, cz);
      for (int sec = 0; sec < 20; sec ++) {
        int y = sec * 16;
        if (y <= top) continue;
        buildChunkSection(cx * 16, y, cz * 16);
        checked ++;
        if (all_air()) sky ++;
        else CHECK(!"a section above chunkTopY has blocks");
      }
    }
  }
  printf("  %d sections above the top, all of them air\n", sky);
  CHECK(checked > 20000);
}

static void test_sky_section_with_changes_is_built (void) {
  setup();
  world_seed = 0x9E3779B9u;
  uint8_t before = chunkTopY(2, 3);
  CHECK(before < 200);
  CHECK(makeBlockChange(2 * 16 + 5, 200, 3 * 16 + 7, B_stone) == 0);
  CHECK(chunkTopY(2, 3) >= 200);
}

static int varint (const uint8_t *b, int *pos) {
  int v = 0, shift = 0;
  while (1) { uint8_t c = b[(*pos) ++]; v |= (c & 0x7F) << shift; if (!(c & 0x80)) break; shift += 7; }
  return v;
}

// The chunk packet as the client parses it: the declared data size must be
// exactly the 32 sections that follow, and sky sections must be one value
static void test_chunk_packet_format (void) {
  setup();
  world_seed = 0x432B506Fu;
  int sv[2];
  CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
  fcntl(sv[0], F_SETFL, fcntl(sv[0], F_GETFL, 0) | O_NONBLOCK);   // as the server's sockets
  CHECK(net_open(sv[0]) == 0);
  sc_chunkDataAndUpdateLight(sv[0], 1, 1);
  // Flush and read in turns: the socket holds less than one chunk
  static uint8_t b[400000];
  int got = 0, n;
  for (int tries = 0; tries < 1000 && (net_pending(sv[0]) > 0 || tries == 0); tries ++) {
    net_flush(sv[0]);
    while ((n = (int)recv(sv[1], b + got, sizeof(b) - got, MSG_DONTWAIT)) > 0) got += n;
  }
  CHECK(net_pending(sv[0]) == 0);
  if (got < 16) return;
  int pos = 0;
  int length = varint(b, &pos);
  CHECK(length == got - pos);
  CHECK(varint(b, &pos) == 0x27);
  pos += 8;                         // chunk x, z
  CHECK(varint(b, &pos) == 0);      // no heightmaps
  int data_size = varint(b, &pos);
  int start = pos, sky = 0;
  for (int sec = 0; sec < 32 && pos < got; sec ++) {
    pos += 2;                       // block count
    int bits = b[pos ++];
    if (bits == 0) {
      varint(b, &pos);              // the one block
      if (sec >= 4 && sec < 24) sky ++;
    } else {
      CHECK(bits == 8);
      int palette = varint(b, &pos);
      for (int i = 0; i < palette; i ++) varint(b, &pos);
      pos += 4096;
    }
    CHECK(b[pos ++] == 0);          // biome bits
    varint(b, &pos);                // biome
  }
  CHECK(pos - start == data_size);
  printf("  data %d bytes, %d of 20 generated sections sent as sky\n", data_size, sky);
  CHECK(sky >= 8);
  net_close(sv[0]);
  close(sv[0]);
  close(sv[1]);
}

int main (void) {
  platform = bareiron_posix_platform();
  RUN(test_sky_predicate_is_exact);
  RUN(test_sky_section_with_changes_is_built);
  RUN(test_chunk_packet_format);
  return CHECK_DONE();
}
