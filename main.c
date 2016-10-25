#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/queue.h>
#include "gc.h"

extern int our_code_starts_here() asm("our_code_starts_here");
extern void error() asm("error");
extern int print(int val) asm("print");
extern int equal(int val1, int val2) asm("equal");
extern int* try_gc(int* alloc_ptr, int amount_needed, int* first_frame, int* stack_top) asm("try_gc");
extern int* HEAP_END asm("HEAP_END");
extern int* STACK_BOTTOM asm("STACK_BOTTOM");

const int TRUE = 0xFFFFFFFF;
const int FALSE = 0x7FFFFFFF;
size_t HEAP_SIZE;
int* STACK_BOTTOM;
int* HEAP;
int* HEAP_END;

int equal(int val1, int val2) {
  if(val1 == val2) { return TRUE; }
  else { return FALSE; }
}

void print_rec(int val) {
  if(val & 0x00000001 ^ 0x00000001) {
    printf("%d", val >> 1);
  }
  else if((val & 0x00000007) == 5) {
    printf("<function>");
  }
  else if(val == 0xFFFFFFFF) {
    printf("true");
  }
  else if(val == 0x7FFFFFFF) {
    printf("false");
  }
  else if((val & 0x00000007) == 1) {
    int* valp = (int*) (val - 1);
    if(*valp & 0x00000008) {
      printf("<cyclic tuple>");
      return;
    }
    *(valp) += 8;
    printf("(");
    print_rec(*(valp + 2));
    printf(", ");
    print_rec(*(valp + 3));
    printf(")");
    *(valp) -= 8;
    fflush(stdout);
  }
  else {
    printf("Unknown value: %#010x", val);
  }
}

int print(int val) {
  print_rec(val);
  printf("\n");
  return val;
}

void error(int i) {
  if (i == 0) {
    fprintf(stderr, "Error: comparison operator got non-number");
  }
  else if (i == 1) {
    fprintf(stderr, "Error: arithmetic operator got non-number");
  }
  else if (i == 2) {
    fprintf(stderr, "Error: if condition got non-boolean");
  }
  else if (i == 3) {
    fprintf(stderr, "Error: Integer overflow");
  }
  else if (i == 4) {
    fprintf(stderr, "Error: not a pair");
  }
  else if (i == 5) {
    fprintf(stderr, "Error: index too small");
  }
  else if (i == 6) {
    fprintf(stderr, "Error: index too large");
  }
  else if (i == 7) {
    fprintf(stderr, "Error: arity mismatch");
  }
  else if (i == 8) {
    fprintf(stderr, "Error: application got non-function");
  }
  else {
    fprintf(stderr, "Error: Unknown error code: %d\n", i);
  }
  exit(i);
}

/*
  Try to clean up space in memory by calling gc.

  You do not need to edit this function.

  Arguments:

    - alloc_ptr: The current value of ESI (where the next value would be
      allocated without GC)
    - bytes_needed: The number of bytes that the runtime is trying to allocate
    - first_frame: The current value of EBP (for tracking stack information)
    - stack_top: The current value of ESP (for tracking stack information)

  Returns:

    The new value for ESI, for the runtime to start using as the allocation
    point.  Must be set to a location that provides enough room to fit
    bytes_allocated more bytes in the given heap space

*/
int* try_gc(int* alloc_ptr, int bytes_needed, int* first_frame, int* stack_top) {
  if(HEAP == alloc_ptr) {
    fprintf(stderr, "Allocation of %d words too large for %d-word heap\n", bytes_needed / 4, (int)HEAP_SIZE);
    free(HEAP);
    exit(10);
  }
  // When you're confident in your collector, use the next line to trigger your GC
  // int* new_esi = gc(STACK_BOTTOM, first_frame, stack_top, HEAP, HEAP_END);

  // This line you'll change; it just keeps ESI where it is.
  int* new_esi = alloc_ptr;
  if((new_esi + (bytes_needed / 4)) > HEAP_END) {
    fprintf(stderr, "Out of memory: needed %d words, but only %d remain after collection", bytes_needed / 4, (HEAP_END - new_esi));
    free(HEAP);
    exit(9);
  }
  else {
    return new_esi;
  }
}

int main(int argc, char** argv) {
  if(argc > 1) {
    HEAP_SIZE = atoi(argv[1]);
  }
  else {
    HEAP_SIZE = 100000;
  }
  HEAP = calloc(HEAP_SIZE, sizeof (int));
  HEAP_END = HEAP + HEAP_SIZE;

  int result = our_code_starts_here(HEAP);

  print(result);
  return 0;
}

