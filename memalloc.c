#include <stdio.h>
#include <pthread.h>

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
    header->s.size = size;
	header->s.is_free = 0;
	header->s.next = NULL;
	if (!head)
		head = header;
	if (tail)
		tail->s.next = header;
	tail = header;
	pthread_mutex_unlock(&global_malloc_lock);
	return (void*)(header + 1);


    block = sbrk(size);
    if (block == (void*) -1)
        return NULL;
    return block;
};