#ifndef compiler_h
#define compiler_h

#include "common.h"
#include "chunk.h"
#include "object.h"

ObjFunction* compile(const char* source);

void markCompilerRoots();

#endif // !compiler_h
