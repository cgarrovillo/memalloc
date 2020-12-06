#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <string.h>

// Make header struct have a memory address aligned to 16 bytes by 
// Wrapping header struct in a union;
// Since a union is the larger size of it's members.
typedef char ALIGN[16];
union header{
    struct {
        size_t size;
        unsigned is_free;
        struct header_t *next;
    } s;
    ALIGN stub;
};
typedef union header header_t;

header_t *head, *tail;

// To prevent two+ threads from concurrent memory access
pthread_mutex_t global_malloc_lock;

// Traverse the linked list of allocated memory blocks
header_t *get_free_block(size_t size)
{
	header_t *curr = head;
    // First-fit approach
	while(curr) {
		if (curr->s.is_free && curr->s.size >= size)
			return curr;
		curr = curr->s.next;
	}
	return NULL;
}


void *malloc(size_t size) {

    size_t total_size;
    void *block;
    header_t *header;

    // if requested size is 0, return null
    if(!size)
        return NULL;

    // acquire the lock for a valid size
    pthread_mutex_lock(&global_malloc_lock);
    header = get_free_block(size);

    // If we found a block thats sufficiently large: mark block as not free, 
    // release global lock, and return pointer to that block
    if (header) {
        header->s.is_free = 0;
        pthread_mutex_unlock(&global_malloc_lock);

        // Remember that the existence of the header (containing info about the memory block itself; metadata)
        // has to be hidden from an outside party.
        // When (header + 1) is done, it points to the byte right after the end of the header; 
        // Incidentally the first byte of the actual memory block,  which is what the caller wants.
        return (void*)(header + 1);
    }

    // If we have NOT found a sufficiently large block, we have to extend the heap by calling sbrk()
    // We first have to compute the total size that includes the requested size + our metadata header
    total_size = sizeof(header_t) + size;
    block = sbrk(total_size);
    if (block == (void*) - 1) {
        pthread_mutex_unlock(&global_malloc_lock);
        return NULL;
    }

    // The block is safely promoted as there is no need to cast a void* to any other pointer type
    header = block;

    // Fill this header with the requested size (not total),
    // and mark as not free.
    // Update the next pointer, head & tail to reflect the new state of the linked list
    header->s.size = size;
	header->s.is_free = 0;
	header->s.next = NULL;
	if (!head)
		head = header;
	if (tail)
		tail->s.next = header;
	tail = header;

    // Release global lock
	pthread_mutex_unlock(&global_malloc_lock);
	return (void*)(header + 1);


    block = sbrk(size);
    if (block == (void*) -1)
        return NULL;
    return block;
};

// Frees a memory block.
void free(void *block)
{
	header_t *header, *tmp;
	void *programbreak;

	if (!block)
		return;
	pthread_mutex_lock(&global_malloc_lock);

    // Get a pointer that is behind the block by a distance EQUAL to the size of the header.
    // (-1 unit to get behind the block)
	header = (header_t*)block - 1;

    // Get current value of program break (Remember: Program breaks are pointers to 
    // the end of the data segment-- data segment being the Data, BSS, & Heap sections.)
	programbreak = sbrk(0);

    // Find end of current block, and check if it is == programbreak  (thus meaning it is the
    // end of the current block)
	if ((char*)block + header->s.size == programbreak) {
        // Reset the head and tail pointers to reflect loss of last block.
		if (head == tail) {
			head = tail = NULL;
		} else {
			tmp = head;
			while (tmp) {
				if(tmp->s.next == tail) {
					tmp->s.next = NULL;
					tail = tmp;
				}
				tmp = tmp->s.next;
			}
		}
        // Calculate amount of memory to be released, and release the memory 
        // by calling sbrk() with a negative amount of this value.
		sbrk(0 - sizeof(header_t) - header->s.size);
		pthread_mutex_unlock(&global_malloc_lock);
		return;
	}

    // If current block is not the last of linked list, set is_free field of the header
	header->s.is_free = 1;
	pthread_mutex_unlock(&global_malloc_lock);
}

// Allocates memory for an array of num elements of nsize bytes each and returns a pointer to the
// newly allocated memory. 
void *calloc(size_t num, size_t nsize)
{
	size_t size;
	void *block;
	if (!num || !nsize)
		return NULL;
	size = num * nsize;

	/* check mul overflow */
	if (nsize != size / num)
		return NULL;
	block = malloc(size);
	if (!block)
		return NULL;

    // Clear all allocated memory to zeroes
	memset(block, 0, size);
	return block;
}

// Change the size of the given memory block
void *realloc(void *block, size_t size)
{
	header_t *header;
	void *ret;
	if (!block || !size)
		return malloc(size);
    
    // Get the block's header and check if block has size to accomodate requested size
	header = (header_t*)block - 1;
	if (header->s.size >= size)
		return block;

    // If it does not, call malloc() to get block of requested size
	ret = malloc(size);

    // Relocate contents to new bigger block using memcpy(),
    // then free the old block of memory
	if (ret) {
		memcpy(ret, block, header->s.size);
		free(block);
	}
	return ret;
}