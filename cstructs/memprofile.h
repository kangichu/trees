#pragma once

void *memop(char *file, int line, void *ptr, long numBytes, int isRealloc);
void printmeminfo();

#if 1

#define malloc(numBytes) memop(__FILE__, __LINE__, NULL, numBytes, 0)
#define realloc(oldPtr, numBytes) memop(__FILE__, __LINE__, oldPtr, numBytes, 1)
#define free(ptr) memop(__FILE__, __LINE__, ptr, -1, 0)

#endif
