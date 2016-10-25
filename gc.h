#include <stddef.h>

/*
  Mark, sweep, and compact memory, starting from the root set on the stack.

  Arguments:
  
    - stack_bottom: A pointer to the bottom of the stack (highest address).  In
      our runtime, this is set by getting the address of a variable from main
    - first_frame: A pointer to the topmost (lowest address) base pointer, for
      starting stack traversal
    - stack_top: A pointer to the top of the stack (lowest address).  During
      execution, this will be the current value of ESP, so it points to the
      most recent frame's variables
    - heap_start: A pointer to the start of the heap; typically the global
      value HEAP, but provided as a parameter for testing
  
  Returns:

    The address immediately following the compacted data, to use as the new
    allocation index stored in ESI.

*/
int* gc(int* stack_bottom, int* first_frame, int* stack_top, int* heap_start, int* heap_end);

int traverse_heap(int* start_addr, int type);

/*

  mark()

  Traverse the heap, starting from the data on the stack, and mark all
  reachable data as live, by setting the second word to 0x00000001

  Arguments:

    - stack_top, first_frame, stack_bottom: as in gc()
    - heap_start: as in compact()

  Returns:

    The starting address of the largest value marked.  By starting address,
    this means the word holding the tag bits.

*/
int* mark(int* stack_top, int* first_frame, int* stack_bottom, int* max_addr);

void update_stack_pointers(int * stack_top, int* first_frame, int* stack_bottom);

void update_heap_pointers(int* heap_ptr, int type);

/*

  forward()

  Set up forwarding pointers to marked data on the heap:
  
    1.  Set the second word of each live value on the heap to its new,
        compacted address (with a 1 at the end, to track liveness)
    2.  Set each address value on the stack and on the heap to point at the
        new corresponding forwarded value.

  Arguments:
    
    - stack_top, first_frame, stack_bottom: As in gc()
    - heap_start, max_address: as in compact()

  Returns:

    nothing

*/
void forward(int* stack_top, int* first_frame, int* stack_bottom, int* heap_start, int* max_address);

/*

  compact()

  Compact memory, assuming that the data has been marked and set up for
  forwarding.  Copy the memory for each value onto the heap into its new
  location.

  Arguments:
  
    - heap_start: A pointer to the start of the heap; typically the global
      value HEAP, but provided as a parameter for testing
    - max_address: A pointer to the _start_ of the latest value marked (e.g.
      the return value of the mark call), for knowing how far in the heap to 
      traverse

  Returns:

    The address immediately following the compacted data, to use as the new
    starting allocation index for ESI

*/
int* compact(int* heap_start, int* max_address, int* heap_end);

int* print_heap(int* heap, int size);
