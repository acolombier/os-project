#ifndef 	_MEM_ALLOC_H_
#define 	_MEM_ALLOC_H_

#define BEST_FIT

#include "mem_alloc_types.h"

#define IS_BLOCK_FREE(b) ((b->flag & 0x1) == FREE_BLOCK)
#define FIRST_FIT

/* Allocator functions, to be implemented in mem_alloc.c */
void mem_init();
char *memory_alloc(int size);
void memory_free(char *p);

/*  */

void memory_display_state(void);

mem_block* check_memory(void); // Return the first corrupted block, NULL otherwise
void compute_checksum(mem_block*);
char check_block(mem_block*);


#endif  	/* !_MEM_ALLOC_H_ */
