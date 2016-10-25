#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

void print_heap(int* heap, int size) {
  for(int i = 0; i < size; i += 1) {
    printf("  %d/%p: %p (%d)\n", i, (heap + i), (int*)(*(heap + i)), *(heap + i));
  }
}

// Note: precondition is that start_addr is untagged by caller
int* traverse_heap(int* start_addr, int type) {
  if (*(start_addr + 1) == 1) { // break out of cycles
    return start_addr;
  }
  //*(start_addr + 4) = 1;//NOTE: PROBABLY DOESNT WORK
  uint32_t one = 1;
  memcpy(start_addr + 1, &one, 4);
  if (type == 5) {
    
    int* local_max = start_addr;
    for (uint32_t i = 0; i < (uint32_t)*(start_addr + 2); i++){
      //recursive calls possibly
      uint32_t to_mask = (uint32_t)*(start_addr + i + 5); //skip 2 tag words, varct, arity, and code ptr
      
      uint32_t masked = (to_mask & 7);

      if (masked == 5 || masked == 1){
        int* ret = traverse_heap((int*)(to_mask & 0xFFFFFFF8), masked);
        if (ret > local_max) {
          local_max = ret;
        }
      }
    }
    return local_max;

  } else { // pair
    int* left_ret_val = start_addr;
    int* right_ret_val = start_addr;
    
    uint32_t left_addr = (uint32_t)*(start_addr + 2);
    uint32_t masked = (left_addr & 7);
    if (masked == 5 || masked == 1) {
      left_ret_val = traverse_heap((int*)(left_addr & 0xFFFFFFF8), masked);
    }

    uint32_t right_addr = (uint32_t)*(start_addr + 3);
    masked = (right_addr & 7);
    if (masked == 5 || masked == 1) {
      right_ret_val = traverse_heap((int*)(right_addr & 0xFFFFFFF8), masked);
    }
    return (right_ret_val > left_ret_val) ? right_ret_val : left_ret_val;
  }
}

int* mark(int* stack_top, int* first_frame, int* stack_bottom, int* max_addr) {
  /*
  ON THE STACK
  1. Traverse from top of stack_top to first_frame
  2. Check each stack frame. If it contains references into the heap, go into the heap. If not, keep traversing
  3. Once we reach first_frame, access the contents of first_frame, and recursively call mark with stack_top =
  the frame after the return pointer, i.e. the frame after the frame after first_frame, and first_frame being the 
  contents of the current first_frame

  (MAKE A HELPER FUNCTION FOR TRAVERSING HEAP)  

  IN THE HEAP
  1. Mark the GC word as 1
  2. Check the first word, to see what type of data you are currently examining.
  If there are other references in the heap, recursively call this helper function
  on those references. (closures/pairs)
  3. keep track of current heap address. return max of current heap address and
  recursive calls to this helper function
  */

  int * max_addr_ret = max_addr;
  while(stack_top < first_frame) {
    uint32_t current_frame = (uint32_t)*(stack_top);
    int masked = (current_frame & 7);
    if (masked == 5 || masked == 1) {
      int* heap_ret = traverse_heap((int*)(current_frame & 0xFFFFFFF8), masked);
      if (heap_ret > max_addr_ret) {
        max_addr_ret = heap_ret;
      }
    }
    stack_top = stack_top + 1;
  }

  return (first_frame == stack_bottom) ? 
    max_addr_ret : 
    mark(first_frame + 2, (int*)*first_frame, stack_bottom, max_addr_ret);
  
}

// iterates through stack from stack_top to stack_bottom and updates heap references
void update_stack_pointers(int * stack_top, int* first_frame, int* stack_bottom) {
  while(stack_top < first_frame) {
    uint32_t current_frame = (uint32_t)*stack_top;
    int masked = (current_frame & 7);
    if (masked == 5 || masked == 1) {
      memcpy(stack_top, (int*)((current_frame & 0xFFFFFFF8) + 1), 4); 
    }
    stack_top = stack_top + 1;
  }

  if (first_frame != stack_bottom) {
    update_stack_pointers(first_frame + 2, (int*)*first_frame, stack_bottom);
  }
}

// iterates through heap from heap_start to max_address and updates heap references
void update_heap_pointers(int* heap_start, int* max_address) {
  int* curr_addr = heap_start;
  int step_size;
  while (curr_addr <= max_address) {
    uint32_t curr_type = (uint32_t)*curr_addr;
    if (curr_type == 5) {
      for (int i = 0; i < (int)*(curr_addr + 2); i++) {
        uint32_t to_mask = (uint32_t)*(curr_addr + i + 5); //skip 2 tag words, varct, arity, and code ptr
        int masked = (to_mask & 7);
        if (masked == 5 || masked == 1) {
          memcpy((curr_addr + i + 5), (int*)((to_mask & 0xFFFFFFF8) + 4), 4);
        }
      }
      uint32_t varct = (uint32_t)*(curr_addr + 2);
      int padding = ((varct % 2) == 0) ? 1 : 0;
      step_size = 5 + varct + padding;

    } else { //pair
      uint32_t left_addr =  (uint32_t)*(curr_addr + 2);
      int masked = (left_addr & 7);
      if (masked == 5 || masked == 1) {
        memcpy((int*)left_addr, (int*)((left_addr & 0xFFFFFFF8) + 1), 4);
      }

      uint32_t right_addr = (uint32_t)*(curr_addr + 3);
      masked = (right_addr & 7);
      if (masked == 5 || masked == 1) {
        memcpy((int*)right_addr, (int*)((right_addr & 0xFFFFFFF8) + 1), 4);
      }
      step_size = 4;
    }
    curr_addr = curr_addr + step_size;
  }
}

void forward(int* stack_top, int* first_frame, int* stack_bottom, int* heap_start, int* max_address) {
  /*
  To set up the forwarding of values, we traverse the heap starting from the beginning (heap_start). We keep 
  track of two pointers, one to the next space to use for the eventual location of compacted data, and one to 
  the currently-inspected value.

  For each value, we check if it's live, and if it is, set its forwarding address to the current compacted data
  pointer and increase the compacted pointer by the size of the value. If it is not live, we simply continue onto 
  the next value – we can use the tag and other metadata to compute the size of each value to determine which address 
  to inspect next. The traversal stops when we reach the max_address stored above (so we don't accidentally treat the 
  undefined data in spaces 72-80 as real data).

  Then we traverse all of the stack and heap values again to update any internal pointers to use the new addresses.

  In this case, the closure is scheduled to move from its current location of 64 to a new location of 16. So its 
  forwarding pointer is set, and both references to it on the stack are also updated. The first tuple is already 
  in its final position (starting at 0), so while its forwarding pointer is set, references to it do not change.*/

  /*
  1. keep track of 2 pointers.  1 for next available loc for compacted data, and 1 for loc being inspected.  Both
     begin at HEAP_START
  2. Traverse heap using inspection ptr and if data being inspected is live, set forwarding address and increment 
     compacted data loc ptr.  Stop traversal at MAX_ADDRESS.  Step size for traversal determined using metadata.
  3. Traverse heap AND STACK values a second time to update any internal pointers (follow the pointers into the heap,
  grab the forwarding addresses, and overwrite the pointer you followed with the value of the grabbed forwarding address)
  */
  int* forwarding_addr = heap_start;
  int* curr_addr = heap_start;
  int step_size; // # of words this data block takes up in the heap

  while (curr_addr <= max_address) {
    uint32_t tag = (uint32_t)*curr_addr;
    if (tag == 1) {
      step_size = 4;
    }
    else if (tag == 5){
      uint32_t varct = (uint32_t)*(curr_addr + 2);
      int padding = ((varct % 2) == 0) ? 1 : 0;
      step_size = 5 + varct + padding;
    }

    uint32_t gc_word = (uint32_t)*(curr_addr + 1);
    if (gc_word == 1) { // alive
      memcpy(curr_addr + 1, &forwarding_addr, 4);
      *(curr_addr + 1) = *(curr_addr + 1) + tag; // note that forwarding addresses are tagged
      forwarding_addr = forwarding_addr + step_size;
    } else { // dead
      // do nothing
    }
    curr_addr = curr_addr + step_size;
  }

  update_stack_pointers(stack_top, first_frame, stack_bottom);
  update_heap_pointers(heap_start, max_address);

  return;
}

int* compact(int* heap_start, int* max_address, int* heap_end) {

  /*
  Finally, we traverse the heap, starting from the beginning, and copy the values into their forwarding positions. 
  Since all the internal pointers and stack pointers have been updated already, once the values are copied, the 
  heap becomes consistent again. We track the last compacted address so that we can return the first free address—in 
  this case 40—which will be returned and used as the new start of allocation. While doing so, we also zero out all 
  of the GC words, so that the next time we mark the heap we have a fresh start.

  I also highly recommend that you walk the rest of the heap and set the words to some special value. The given tests 
  suggest overwriting each word with the value 0x0cab005e – the “caboose” of the heap. This will make it much easier 
  when debugging to tell where the heap ends, and also stop a runaway algorithm from interpreting leftover heap data 
  as live data accidentally.
  */

  /*
  1. Traverse heap and copy values into their forwarding addresses.  Reset all GC tags to 0.
  2. (optional) mark the rest of the heap with a garbage word like 0x0cab005e
  3. return first free address on new heap (why is heap_start the default return value???)
  */

  int * curr_addr = heap_start;
  int num_words = 0;
  int total_words = 0;
  while (curr_addr <= max_address) {
    uint32_t tag = (uint32_t)*curr_addr;

    if (tag == 5) {
      int num_vars = (uint32_t)*(curr_addr + 2); 
      int padding = (num_vars % 2 == 0) ? 1 : 0;
      num_words = num_vars + 5 + padding;
    } else { // pair
      num_words = 4;
    }

    if (*(curr_addr + 1) != 0) {//i.e. there is a tagged forwarding address
      int* new_addr = (int*)(*(curr_addr + 1) & 0xFFFFFFF8);//get untagged forwarding address
      // printf("MEMCPYING FROM %#010x INTO %#010x \n", curr_addr, new_addr);
      // for (int i = 0; i < num_words; i++) {
      //   printf("%d\n", *(curr_addr + i));
      // }
      // printf("\n");
      if (tag == 5) {//closure
        for (int i = 0; i < (int)*(curr_addr + 2); i++) {
          uint32_t to_mask = (uint32_t)*(curr_addr + i + 5); //skip 2 tag words, varct, arity, and code ptr
          int masked = (to_mask & 7);
          if (masked == 5 || masked == 1) {
            memcpy(curr_addr + i + 5, (int*)((to_mask & 0xFFFFFFF8) + 1), 4);
          }
          else{//nothing
          }
        }
        memcpy(new_addr, curr_addr, num_words * 4);

      } else { // pair
        memcpy(new_addr, curr_addr, 8); // copy over tag and gc word
        uint32_t to_mask = (uint32_t)*(curr_addr + 2); // this is the value of 1st elt
        int masked = (to_mask & 7); // check if 1st elt is pair or closure
        
        if (masked == 5 || masked == 1) {
          // copy over the forwarded addr of the pair or closure
          memcpy(new_addr + 2, (int*)((to_mask & 0xFFFFFFF8) + 4), 4);
        } else {
          memcpy(new_addr + 2, curr_addr + 2, 4);
        }

        to_mask = (uint32_t)*(curr_addr + 3); // this is the value of 2nd elt
        masked = (to_mask & 7); // check if 2nd elt is pair or closure
        
        if (masked == 5 || masked == 1) {
          // copy over the forwarded addr of the pair or closure
          memcpy(new_addr + 3, (int*)((to_mask & 0xFFFFFFF8) + 4), 4);
        } else {
          memcpy(new_addr + 3, curr_addr + 3, 4);
        }
      }

      //*(new_addr + 4) = 0;//NOTE: Make sure we aren't setting more than 4 bytes here
      uint32_t zero = 0;
      memcpy(new_addr + 1, &zero, 4);
      total_words = total_words + num_words; //keep track of size of compacted heap
    } else {
      // do nothing
    }
    curr_addr = curr_addr + num_words;
  }

  //set all words between end of compacted heap and end of old heap to 0x0cab005e
  curr_addr = heap_start + total_words;

  uint32_t caboose = 0x0cab005e;
  while (curr_addr < heap_end) {
    memcpy(curr_addr, &caboose, 4);
    curr_addr = curr_addr + 1;
  }

  return heap_start + total_words;
}

int* gc(int* stack_bottom, int* first_frame, int* stack_top, int* heap_start, int* heap_end) {
  int* max_address = mark(stack_top, first_frame, stack_bottom, heap_start);
  forward(stack_top, first_frame, stack_bottom, heap_start, max_address);
  int* answer = compact(heap_start, max_address, heap_end);
  return answer;
}