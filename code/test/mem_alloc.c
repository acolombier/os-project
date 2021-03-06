#include "syscall.h"
#include "userlib.h"
#include "mem_alloc.h"

#define FIRST_FIT

static char *memory = NULL;
static unsigned int memory_size;

static void cleanup(){
	mem_block *current_block = (mem_block*)memory, *previous_block = NULL;

	if(divRoundDown(memory_size, PageSize) == 1)
	    return;
        
#ifdef VERBOSE_MALLOC                
	PutString("Cleaning unused tail pages\n");
#endif
	while (memory + memory_size > (char*)current_block){
		previous_block = current_block;
		current_block = (mem_block*)((char*)current_block + current_block->size + 2 * BLOCK_SIZE_WITH_PADDING);
	}
	if (previous_block && IS_BLOCK_FREE(previous_block) && previous_block->size + 2 * BLOCK_SIZE_WITH_PADDING >= PageSize){
		int deallocated_pages = divRoundDown(previous_block->size, PageSize);
        previous_block->size -= deallocated_pages * PageSize;
        DUMP_HEADER_TO_FOOTER(previous_block);
		Sbrk(-deallocated_pages);
		memory_size -= deallocated_pages * PageSize;
	}
}

void memory_init() {	
	memory = Sbrk(0);
	if (!Sbrk(1))
		return;
	memory_size = PageSize;

	mem_block *first_block = (mem_block*)memory;
	first_block->flag = FREE_BLOCK;
	first_block->size = memory_size - 2 * BLOCK_SIZE_WITH_PADDING;

	DUMP_HEADER_TO_FOOTER(first_block);
	
}

char *memory_alloc(int size){
	char *mem = NULL;

	if (size <= 0)
		return NULL;
		
	while (mem == NULL){

		mem_block *temporary_block = NULL, *current_block = (mem_block *)memory;

		int effective_size = size;

		if (effective_size % MEM_ALIGNMENT)
			effective_size = (effective_size / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT;

#if defined(BEST_FIT) || defined(WORSE_FIT)

		mem_block *elected_block = NULL;
		int lost_bytes = memory_size;

#endif

		while (memory + memory_size > (char*)current_block){

	/* code specific to first fit strategy can be inserted here */
			if (!check_block(current_block)){
				PutString("Memory corrupted: Block at ");
                PutInt((int)current_block);
                PutString(" doesn't match with the cheksum (current: ");
                PutInt(current_block->flag);
                PutString(")\n");
#ifdef MALLOC_EXIT_ON_FAILURE
				Exit(EXIT_FAILURE);
#else
                return NULL;
#endif
			}

	#if defined(FIRST_FIT)
			if (IS_BLOCK_FREE(current_block) && current_block->size >= effective_size){
				if (current_block->size - effective_size >  2 * BLOCK_SIZE_WITH_PADDING){
				/* If there is enought room remaining for writing a free block struct, we do it */

					temporary_block = (mem_block*)((char*)current_block + effective_size + 2 * BLOCK_SIZE_WITH_PADDING);
					
					// Padding the new block on the same alignment
					if ((unsigned int)temporary_block % MEM_ALIGNMENT){
						effective_size += MEM_ALIGNMENT - (unsigned int)temporary_block % MEM_ALIGNMENT;
						temporary_block = (mem_block*)(((unsigned int)temporary_block / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT);
					}

					memset(temporary_block, 0, BLOCK_SIZE_WITH_PADDING);
					// We update the size of the block from where we took the memory
					temporary_block->size = current_block->size - effective_size - 2 * BLOCK_SIZE_WITH_PADDING;
				} else
					effective_size = current_block->size;

				current_block->size = effective_size;
				current_block->flag |= USED_BLOCK;
				mem = (char*)current_block + BLOCK_SIZE_WITH_PADDING;
				break;
			} else
				// We put our value to the next theoretical block
				current_block = (mem_block*)((char*)current_block + current_block->size + 2 * BLOCK_SIZE_WITH_PADDING);

	#elif defined(BEST_FIT) || defined(WORST_FIT)
		if (IS_BLOCK_FREE(current_block) && current_block->size >= effective_size &&
	#if defined(BEST_FIT)
		current_block->size - effective_size > lost_bytes){
	#else
		current_block->size - effective_size < lost_bytes){
	#endif
			
			lost_bytes = current_block->size - effective_size;
			elected_block = current_block;
		}
		current_block = (mem_block*)((char*)current_block + current_block->size + 2 * BLOCK_SIZE_WITH_PADDING);

	#endif
		}

	#if defined(BEST_FIT) || defined(WORST_FIT)
	/* code specific to best fit strategy can be inserted here */
		if (elected_block){
			if (lost_bytes >  2 * BLOCK_SIZE_WITH_PADDING){
			/* If there is enought room remaining for writing a free block struct, we do it */

				temporary_block = (mem_block*)((char*)elected_block + effective_size + 2 * BLOCK_SIZE_WITH_PADDING);

				// Padding the new block on the same alignment
				if ((unsigned int)temporary_block % MEM_ALIGNMENT){
					effective_size += MEM_ALIGNMENT - (unsigned int)temporary_block % MEM_ALIGNMENT;
					temporary_block = (mem_block*)(((unsigned int)temporary_block / MEM_ALIGNMENT + 1) * MEM_ALIGNMENT);
				}
				memset(temporary_block, 0, BLOCK_SIZE_WITH_PADDING);
				// We update the size of the block from where we took the memory
				temporary_block->size = lost_bytes - 2 * BLOCK_SIZE_WITH_PADDING;
				DUMP_HEADER_TO_FOOTER(temporary_block);
			} else
				effective_size = elected_block->size;


			((mem_block*)elected_block)->size = effective_size;
			elected_block->flag |= USED_BLOCK;
			mem = (char*)elected_block + BLOCK_SIZE_WITH_PADDING;
		}
	#endif

		if (mem){
			if (temporary_block){
				DUMP_HEADER_TO_FOOTER(temporary_block);
			}
			DUMP_HEADER_TO_FOOTER(((mem_block*)(mem - BLOCK_SIZE_WITH_PADDING)));
		} else {
			int additionnal_pages = divRoundUp(size, PageSize);
			void* new_area = Sbrk(additionnal_pages);			
#ifdef VERBOSE_MALLOC                
				PutString("Heap has been extented\n");
#endif
			if (!new_area){
				cleanup();
				return NULL;
			}
            
			mem_block* last_block = PREV_FOOTER_BLOCK(new_area);
			if (IS_BLOCK_FREE(last_block)){
#ifdef VERBOSE_MALLOC                
				PutString("Extending previous over\n");
#endif
				HEADER_FROM_FOOTER(last_block)->size += additionnal_pages * PageSize;
				DUMP_HEADER_TO_FOOTER(HEADER_FROM_FOOTER(last_block));
			} else {
#ifdef VERBOSE_MALLOC                
				PutString("Creating a new free\n");
#endif
				((mem_block*)new_area)->flag = FREE_BLOCK;
				((mem_block*)new_area)->size = additionnal_pages * PageSize - 2 * BLOCK_SIZE_WITH_PADDING;
				DUMP_HEADER_TO_FOOTER(((mem_block*)new_area));				
			}
			memory_size += additionnal_pages * PageSize;
		}
	}
	cleanup();

    return mem; /* to be modified */
}

void memory_free(char *p){
	if (!p)
		return;

    mem_block* meta_block_to_free = (mem_block*)(p - BLOCK_SIZE_WITH_PADDING), *meta_previous_block = PREV_FOOTER_BLOCK(meta_block_to_free), *meta_next_block = NEXT_HEADER_BLOCK(meta_block_to_free);

	if (!check_block(meta_block_to_free)){
        PutString("Memory corrupted: Block at ");
        PutInt((int)meta_block_to_free);
        PutString(" doesn't match with the cheksum (current: ");
        PutInt(meta_block_to_free->flag);
        PutString(")\n");
#ifdef MALLOC_EXIT_ON_FAILURE
		Exit(EXIT_FAILURE);
#else
                return;
#endif
	}

    if ((char*)meta_previous_block >= memory && IS_BLOCK_FREE(meta_previous_block)){ // The previous block is free
		meta_previous_block = HEADER_FROM_FOOTER(meta_previous_block);
		meta_previous_block->size += meta_block_to_free->size + 2 * BLOCK_SIZE_WITH_PADDING;
		DUMP_HEADER_TO_FOOTER(meta_previous_block);
		meta_block_to_free = meta_previous_block;
	}

	if ((char*)meta_next_block + BLOCK_SIZE_WITH_PADDING < memory + memory_size && IS_BLOCK_FREE(meta_next_block)) // The next block is free
		meta_block_to_free->size += meta_next_block->size + 2 * BLOCK_SIZE_WITH_PADDING;

	meta_block_to_free->flag &= 0xFE;
	DUMP_HEADER_TO_FOOTER(meta_block_to_free);
	
	cleanup();
}

 // Return the first corrupted block, NULL otherwise
mem_block* check_memory(){
	return NULL;
}

void compute_checksum(mem_block* b){
	
	mem_block* sibbling_meta[2] = {PREV_FOOTER_BLOCK(b), NEXT_HEADER_BLOCK(b)};

	// HEADER: 4 bits for own checksum, 3 for sibbling (left) checksum
	if ((char*)sibbling_meta[LEFT_SIBBLING] > memory){
		b->flag = sibbling_meta[LEFT_SIBBLING]->size % 7 << 1 | (b->flag & 1);
		if (!IS_BLOCK_FREE(sibbling_meta[LEFT_SIBBLING]))
			b->flag =  ((~b->flag) & 0xE) | (b->flag & 0x1);
		sibbling_meta[LEFT_SIBBLING]->flag &= 0xF1;
		sibbling_meta[LEFT_SIBBLING]->flag |= b->size % 7 << 1;
	}
	b->flag = b->size % 15 << 4 | (b->flag & 0xF);

	// FOOTER: 4 bits for own checksum, 3 for sibbling (right) checksum
	if ((char*)sibbling_meta[RIGHT_SIBBLING] < memory + memory_size){
		FOOTER_FROM_HEADER(b)->flag = sibbling_meta[RIGHT_SIBBLING]->size % 7 << 1 | (FOOTER_FROM_HEADER(b)->flag & 1);
		if (!IS_BLOCK_FREE(sibbling_meta[RIGHT_SIBBLING]))
			FOOTER_FROM_HEADER(b)->flag =  ((~FOOTER_FROM_HEADER(b)->flag) & 0xE) | (FOOTER_FROM_HEADER(b)->flag & 0x1);
		sibbling_meta[RIGHT_SIBBLING]->flag &= 0xF1;
		sibbling_meta[RIGHT_SIBBLING]->flag |= b->size % 7 << 1;
	}
	FOOTER_FROM_HEADER(b)->flag = FOOTER_FROM_HEADER(b)->size % 15 << 4 | (FOOTER_FROM_HEADER(b)->flag & 0xF);

	// Inverse sum if allocated
	if (!IS_BLOCK_FREE(b)){
		if ((char*)sibbling_meta[RIGHT_SIBBLING] < memory + memory_size)
			sibbling_meta[RIGHT_SIBBLING]->flag = ((~sibbling_meta[RIGHT_SIBBLING]->flag) & 0xE) | (sibbling_meta[RIGHT_SIBBLING]->flag & 0xF1);
		if ((char*)sibbling_meta[LEFT_SIBBLING] > memory)
			sibbling_meta[LEFT_SIBBLING]->flag = ((~sibbling_meta[LEFT_SIBBLING]->flag) & 0xE) | (sibbling_meta[LEFT_SIBBLING]->flag & 0xF1);

		b->flag = ((~b->flag) & 0xF0) | (b->flag & 0xF);
		FOOTER_FROM_HEADER(b)->flag = ((~FOOTER_FROM_HEADER(b)->flag) & 0xF0) | (FOOTER_FROM_HEADER(b)->flag & 0xF);
	}
}

char check_block(mem_block* b){
	mem_block* sibbling_meta[2] = {PREV_FOOTER_BLOCK(b), NEXT_HEADER_BLOCK(b)};
	unsigned char small_hash_from_left = 0,  small_hash_from_right = 0;
	unsigned char extended_hash;
	unsigned char computed_sibbling_sum = b->size % 7,  computed_extended_hash = b->size % 15;

	if ((char*)sibbling_meta[LEFT_SIBBLING] > memory)
		small_hash_from_left = sibbling_meta[LEFT_SIBBLING]->flag >> 1 & 0x7;

	if ((char*)sibbling_meta[RIGHT_SIBBLING] < memory + memory_size)
		small_hash_from_right = sibbling_meta[RIGHT_SIBBLING]->flag >> 1 & 0x7;

	extended_hash = b->flag >> 4;

	if (!IS_BLOCK_FREE(b)){
		computed_extended_hash = (~computed_extended_hash) & 0xF;
		computed_sibbling_sum = (~computed_sibbling_sum) & 0x7;
	}

	return (computed_extended_hash == extended_hash)
		&& ((char*)sibbling_meta[LEFT_SIBBLING] < memory || computed_sibbling_sum == small_hash_from_left)
		&& ((char*)sibbling_meta[RIGHT_SIBBLING] >= memory + memory_size || computed_sibbling_sum == small_hash_from_right);
}

void nextColor(int color){
    switch (color % 3){
    case 0 : PutString("\x1b[35m"); break;
    case 1 : PutString("\x1b[92m"); break;
    case 2 : PutString("\x1b[36m"); break;
    }
}

void memory_display_state(void){

    mem_block* current_block = (mem_block*)memory;
    unsigned int c = (unsigned int)memory;
    unsigned int increment = memory_size / MEMORY_DISPLAY_SIZE;
    int color_index = 0;
    
    PutString("Number of page used: ");
    PutInt(memory_size / PageSize);
    PutString("\nStaring: ");
    PutInt((unsigned int)memory);
    PutString("\nFinishing: ");
    PutInt((unsigned int)memory + memory_size);
    PutString("\nSize: ");
    PutInt((unsigned int)memory_size);
   
    PutString("\nMem. usage: [");
    while (memory + memory_size > (char*)current_block){
	if(!IS_BLOCK_FREE(current_block))
	    nextColor(color_index++);
	for (; c < (unsigned int)((char*)current_block + current_block->size + 2 * BLOCK_SIZE_WITH_PADDING); c+= increment)
	    PutChar(IS_BLOCK_FREE(current_block) ? '.' : 'U');
	current_block = (mem_block*)((char*)current_block + current_block->size + 2 * BLOCK_SIZE_WITH_PADDING);
	PutString("\x1b[0m");
    }
    PutString("]\n");
	
}
