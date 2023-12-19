#ifndef compiler_h
#define compiler_h

#include "common.h"
#include "chunk.h"
#include "object.h"

ObjFunction* compile(const char* source);

#endif // !compiler_h
