#ifndef H_BLOCKINDEX
#define H_BLOCKINDEX

#include <stdint.h>

// Index over block_changes by chunk column (x >> 4, z >> 4), so looking up
// a block or building a chunk section only visits the changes nearby
// instead of the whole array. The 14 entries of item data that follow a
// chest are never indexed.

int  bc_index_init (uint32_t capacity);          // 0 ok, -1 no memory (alloc_large)
void bc_index_free (void);
void bc_index_rebuild (void);                    // after loading a world
int  bc_find (short x, uint8_t y, short z);      // index into block_changes, or -1
void bc_on_set (int index);                      // entry index now holds a block (chest entries: only the chest itself)
void bc_on_clear (int index);                    // call before the entry's coordinates are overwritten
int  bc_first_free (int run_length);             // lowest index with run_length consecutive 0xFF entries, or -1
int  bc_column (short cx, short cz, const int **indices); // count of entries in chunk column (x>>4, z>>4)

#endif
