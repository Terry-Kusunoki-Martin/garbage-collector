
#include "gc.h"
#include "cutest-1.5/CuTest.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void CuAssertArrayEquals_LineMsg(CuTest* tc, const char* file, int line, const char* message, 
	int* expected, int* actual, int length);

#define CuAssertArrayEquals(tc,expect,actual,len)           CuAssertArrayEquals_LineMsg((tc),__FILE__,__LINE__,NULL,(expect),(actual),(len))

void CuAssertArrayEquals_LineMsg(CuTest* tc, const char* file, int line, const char* message, 
  int* expected, int* actual, int length)
{
  char buf[STRING_MAX];
  int i = 0;
  int found_mismatch = 0;

  int actv;
  int expv;

  for(i = 0; i < length; i += 1) {
    expv = *(expected + i);
    actv = *(actual + i);
    if(expv != actv) {
      sprintf(buf, "values at index %d differ: expected %#010x but was %#010x", i, expv, actv);
      found_mismatch = 1;
    }
  }
  if(found_mismatch) {
    CuFail_Line(tc, file, line, message, buf);
  }
}


void TestMark(CuTest* tc) {
  int* heap = calloc(16, sizeof (int));

  heap[0] = 0x00000001; // a pair (that will be collected)
  heap[1] = 0x00000000;
  heap[2] = ((int)(heap + 4)) | 0x00000001; // another pair on the heap
  heap[3] = 0x00000030; // the number 24

  heap[4] = 0x00000001; // a pair
  heap[5] = 0x00000000; // empty gc word
  heap[6] = ((int)(heap + 12)) | 0x00000001; // another pair on the heap
  heap[7] = 0x00000008; // the number 4

  heap[8] = 0x00000001;  // another pair that will be collected
  heap[9] = 0x00000000;
  heap[10] = 0x00000004; // the number 2
  heap[11] = 0x00000006; // the number 3

  heap[12]  = 0x00000001;
  heap[13]  = 0x00000000;
  heap[14] = 0x0000000a; // the number 5
  heap[15] = 0x0000000c; // the number 6
  
  int stack[5] = {
    ((int)(heap + 4)) + 1, // a reference to the second pair on the heap
    0x0000000e, // the number 7
    0x00000000, // to be filled in with mock ebp
    0x00000000, // to be filled in with mock return ptr
    0xffffffff
  };

  stack[2] = (int)(stack + 4); // some address further "down"
  stack[3] = 0x0adadad0; // mock return ptr/data to skip

  int* expectHeap = calloc(16, sizeof (int));
  memcpy(expectHeap, heap, 16 * (sizeof (int)));
  expectHeap[5] = 0x00000001;
  expectHeap[13] = 0x00000001;

  int* max_address = mark(stack, (stack + 2), (stack + 4), heap);

  printf("(MARK) EXPECTED HEAP AFTER MARK\n");
  print_heap(expectHeap, 16);
  printf("\n(MARK)HEAP AFTER MARK\n");
  print_heap(heap, 16);

  CuAssertArrayEquals(tc, expectHeap, heap, 16);

  //printf("heap: %p, max address: %p\n", heap, max_address);

  int* expectHeap2 = calloc(16, sizeof (int));
  memcpy(expectHeap2, heap, 16 * (sizeof (int)));
  
  expectHeap2[5] = ((int)heap) + 1;
  expectHeap2[13] = ((int)(heap + 4)) + 1;
  
  forward(stack, (stack + 2), stack + 4, heap, max_address);

  printf("(MARK) EXPECTED HEAP AFTER FORWARD\n");
  print_heap(expectHeap2, 16);
  printf("\n(MARK)HEAP AFTER FORWARD\n");
  print_heap(heap, 16);
  
  CuAssertArrayEquals(tc, expectHeap2, heap, 16);


  int* expectHeap3 = calloc(16, sizeof (int));

  compact(heap, max_address, heap + 16);

  expectHeap3[0] = 0x00000001; // a pair
  expectHeap3[1] = 0x00000000; // empty gc word
  expectHeap3[2] = ((int)(heap + 4)) | 0x00000001; // another pair on the heap
  expectHeap3[3] = 0x00000008; // the number 4

  expectHeap3[4] = 0x00000001;  // another pair
  expectHeap3[5] = 0x00000000;
  expectHeap3[6] = 0x0000000a;  // the number 5
  expectHeap3[7] = 0x0000000c;  // the number 6

  expectHeap3[8]  = 0x0cab005e;
  expectHeap3[9]  = 0x0cab005e;
  expectHeap3[10] = 0x0cab005e;
  expectHeap3[11] = 0x0cab005e;

  expectHeap3[12] = 0x0cab005e;
  expectHeap3[13] = 0x0cab005e;
  expectHeap3[14] = 0x0cab005e;
  expectHeap3[15] = 0x0cab005e;

  // printf("\nEXPECTED HEAP AFTER COMPACT\n");
  // print_heap(expectHeap3, 16);
  // printf("\nHEAP AFTER COMPACT\n");
  print_heap(heap, 16);

  CuAssertArrayEquals(tc, expectHeap3, heap, 16);

  free(heap);
  free(expectHeap);
  free(expectHeap2);
  free(expectHeap3);
}

void TestClosure(CuTest* tc) {
  int* heap = calloc(16, sizeof (int));

  heap[0] = 0x00000001; // a pair (that will be collected)
  heap[1] = 0x00000000;
  heap[2] = 0x00000004; // another pair on the heap
  heap[3] = 0x00000030; // the number 24

  heap[4] = 0x00000001; // a pair
  heap[5] = 0x00000000; // empty gc word
  heap[6] = 0x00000002; // another pair on the heap
  heap[7] = 0x00000008; // the number 4

  heap[8] = 0x00000005;  // closure
  heap[9] = 0x00000000;
  heap[10] = 0x00000002; // 2 as the varcount
  heap[11] = 0x00000001; // arity

  heap[12]  = 0x0cac3333;//arbitrary code ptr
  heap[13]  = 0x0000001a;// first freevar: 13
  heap[14] = ((int)(heap + 4)) | 0x00000001; // second freevar: heap ptr to a pair
  heap[15] = 0x00000000; // padding
  
  int stack[10] = {
    ((int)(heap + 4)) + 1, // (loaded freevar) a reference to the second pair on the heap
    0x00000000, // address of freevar with value 13
    0x00000000, // to be filled in with mock ebp
    0x00000000, // to be filled in with mock return ptr
    ((int)(heap + 8)) + 5, // pushed reference to closure
    0x00000006, // pushed arg for function call
    ((int)(heap + 8)) + 5, // reference to closure
    ((int)(heap + 4)) + 1, // reference to 2nd pair on heap\
    0x0000001a, //freevar = 3
    0xffffffff
  };

  stack[1] = (int)(stack + 8); // ptr to freevar = 3
  stack[2] = (int)(stack + 9); // some address further "down"
  stack[3] = 0x0adadad0; // mock return ptr/data to skip

  int* expectHeap = calloc(16, sizeof (int));
  memcpy(expectHeap, heap, 16 * (sizeof (int)));
  expectHeap[5] = 0x00000001;
  expectHeap[9] = 0x00000001;

  int* max_address = mark(stack, (stack + 2), (stack + 9), heap);

  printf("(CLOSURE) EXPECTED HEAP AFTER MARK:\n");
  print_heap(expectHeap, 16);
  printf("(CLOSURE) HEAP AFTER MARK:\n");
  print_heap(heap, 16);
  CuAssertArrayEquals(tc, expectHeap, heap, 16);

  //printf("heap: %p, max address: %p\n", heap, max_address);
  int* expectHeap2 = calloc(16, sizeof (int));
  memcpy(expectHeap2, heap, 16 * (sizeof (int)));
  
  expectHeap2[5] = ((int)heap) + 1;
  expectHeap2[9] = ((int)(heap + 4)) + 5;
  expectHeap2[14] = ((int)heap) + 1;
  
  forward(stack, (stack + 2), stack + 9, heap, max_address);

  printf("(CLOSURE) EXPECTED HEAP AFTER FORWARD\n");
  print_heap(expectHeap2, 16);
  printf("\n(CLOSURE) HEAP AFTER FORWARD\n");
  print_heap(heap, 16);
  
  CuAssertArrayEquals(tc, expectHeap2, heap, 16);

  int* expectHeap3 = calloc(16, sizeof (int));

  compact(heap, max_address, heap + 16);

  expectHeap3[0] = 0x00000001; // a pair
  expectHeap3[1] = 0x00000000; // empty gc word
  expectHeap3[2] = 0x00000002; // the number 1
  expectHeap3[3] = 0x00000008; // the number 4

  expectHeap3[4] = 0x00000005;  // closure
  expectHeap3[5] = 0x00000000;
  expectHeap3[6] = 0x00000002;  
  expectHeap3[7] = 0x00000001;  

  expectHeap3[8]  = 0x0cac3333;
  expectHeap3[9]  = 0x0000001a;
  expectHeap3[10] = ((int)heap) + 1;
  expectHeap3[11] = 0x00000000;

  expectHeap3[12] = 0x0cab005e;
  expectHeap3[13] = 0x0cab005e;
  expectHeap3[14] = 0x0cab005e;
  expectHeap3[15] = 0x0cab005e;

  printf("\n(CLOSURE) EXPECTED HEAP AFTER COMPACT\n");
  print_heap(expectHeap3, 16);
  printf("\n(CLOSURE) HEAP AFTER COMPACT\n");
  print_heap(heap, 16);

  CuAssertArrayEquals(tc, expectHeap3, heap, 16);

  free(heap);
  free(expectHeap);
  free(expectHeap2);
  free(expectHeap3);
}

CuSuite* CuGetSuite(void)
{
  CuSuite* suite = CuSuiteNew();

  SUITE_ADD_TEST(suite, TestMark);
  SUITE_ADD_TEST(suite, TestClosure);

  return suite;
}