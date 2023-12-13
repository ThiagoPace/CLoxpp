#ifndef vm_h
#define vm_h

#include "value.h"
#include "chunk.h"

#define STACK_MAX 256

typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILER_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
	Chunk* chunk;
	uint8_t* ip;

	Value stack[STACK_MAX];
	Value* stackPtr;

	Obj* objects;
} VM;

//A bit NURN
extern VM vm;

void initVM();
void freeVM();

InterpretResult interpret(char* source);
InterpretResult run();

void push(Value value);
Value pop();

#endif vm_h