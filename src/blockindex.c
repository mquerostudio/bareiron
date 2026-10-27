#include <string.h>

#include "globals.h"
#include "registries.h"
#include "blockindex.h"

// One chunk column and the block_changes entries that fall inside it
typedef struct {
  uint32_t key;
  int used;
  int count;
  int cap;
  int *items;
} BcColumn;

// Open addressing hash table of columns, always a power of two in size
static BcColumn *columns = NULL;
static uint32_t columns_size = 0;
static uint32_t columns_used = 0;

// Lowest index that might be free; nothing below it is
static int free_hint = 0;

static uint32_t columnKey (short cx, short cz) {
  return ((uint32_t)(uint16_t)cx << 16) | (uint16_t)cz;
}

static uint32_t hashKey (uint32_t key) {
  key ^= key >> 16;
  key *= 0x7feb352d;
  key ^= key >> 15;
  return key;
}

// Finds the slot of a column, or the empty slot where it would go
static BcColumn *findSlot (BcColumn *table, uint32_t size, uint32_t key) {
  uint32_t i = hashKey(key) & (size - 1);
  while (table[i].used && table[i].key != key) i = (i + 1) & (size - 1);
  return &table[i];
}

static void freeColumns (void) {
  if (!columns) return;
  for (uint32_t i = 0; i < columns_size; i ++) {
    if (columns[i].items) platform->free_large(columns[i].items);
  }
  platform->free_large(columns);
  columns = NULL;
  columns_size = columns_used = 0;
}

static int allocColumns (uint32_t size) {
  columns = platform->alloc_large(size * sizeof(BcColumn));
  if (!columns) return -1;
  memset(columns, 0, size * sizeof(BcColumn));
  columns_size = size;
  columns_used = 0;
  return 0;
}

// Doubles the table once it's half full
static int growColumns (void) {
  BcColumn *old = columns;
  uint32_t old_size = columns_size;
  if (allocColumns(old_size * 2)) {
    columns = old;
    columns_size = old_size;
    return -1;
  }
  for (uint32_t i = 0; i < old_size; i ++) {
    if (!old[i].used) continue;
    *findSlot(columns, columns_size, old[i].key) = old[i];
    columns_used ++;
  }
  platform->free_large(old);
  return 0;
}

int bc_index_init (uint32_t capacity) {
  (void)capacity;
  freeColumns();
  free_hint = 0;
  return allocColumns(1024);
}

void bc_index_free (void) {
  freeColumns();
}

static BcColumn *columnOf (int index, int create) {
  short x = block_changes[index].x, z = block_changes[index].z;
  uint32_t key = columnKey(x >> 4, z >> 4);
  BcColumn *column = findSlot(columns, columns_size, key);
  if (column->used || !create) return column->used ? column : NULL;
  if ((columns_used + 1) * 2 > columns_size) {
    if (growColumns()) return NULL;
    column = findSlot(columns, columns_size, key);
  }
  column->used = 1;
  column->key = key;
  columns_used ++;
  return column;
}

void bc_on_set (int index) {
  BcColumn *column = columnOf(index, true);
  if (!column) return;
  for (int i = 0; i < column->count; i ++) {
    if (column->items[i] == index) return;
  }
  if (column->count == column->cap) {
    int cap = column->cap ? column->cap * 2 : 8;
    int *items = platform->alloc_large(cap * sizeof(int));
    if (!items) return;
    if (column->count) memcpy(items, column->items, column->count * sizeof(int));
    if (column->items) platform->free_large(column->items);
    column->items = items;
    column->cap = cap;
  }
  column->items[column->count ++] = index;
}

void bc_on_clear (int index) {
  if (index < free_hint) free_hint = index;
  BcColumn *column = columnOf(index, false);
  if (!column) return;
  for (int i = 0; i < column->count; i ++) {
    if (column->items[i] != index) continue;
    column->items[i] = column->items[-- column->count];
    return;
  }
}

void bc_index_rebuild (void) {
  for (uint32_t i = 0; i < columns_size; i ++) columns[i].count = 0;
  free_hint = 0;
  for (int i = 0; i < block_changes_count; i ++) {
    if (block_changes[i].block == 0xFF) continue;
    bc_on_set(i);
    #ifdef ALLOW_CHESTS
      // Skip chest contents
      if (block_changes[i].block == B_chest) i += 14;
    #endif
  }
}

int bc_find (short x, uint8_t y, short z) {
  const int *indices;
  int count = bc_column(x >> 4, z >> 4, &indices);
  for (int i = 0; i < count; i ++) {
    BlockChange *change = &block_changes[indices[i]];
    if (change->block == 0xFF) continue;
    if (change->x == x && change->y == y && change->z == z) return indices[i];
  }
  return -1;
}

int bc_column (short cx, short cz, const int **indices) {
  BcColumn *column = findSlot(columns, columns_size, columnKey(cx, cz));
  if (!column->used) return 0;
  *indices = column->items;
  return column->count;
}

int bc_first_free (int run_length) {
  int run_start = -1;
  int first_free = -1;
  for (int i = free_hint; i < (int)block_changes_cap; i ++) {
    if (block_changes[i].block != 0xFF) {
      run_start = -1;
      #ifdef ALLOW_CHESTS
        // A chest's item data is never free, whatever its bytes say
        if (block_changes[i].block == B_chest) i += 14;
      #endif
      continue;
    }
    if (first_free == -1) first_free = i;
    if (run_start == -1) run_start = i;
    if (i - run_start + 1 == run_length) {
      free_hint = first_free;
      return run_start;
    }
  }
  if (first_free != -1) free_hint = first_free;
  return -1;
}
