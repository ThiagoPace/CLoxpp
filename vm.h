#ifndef vm_h
#define vm_h

#include "value.h"
#include "chunk.h"
#include "table.h"
#include "object.h"

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * UINT8_MAX)


typedef struct {
	ObjFunction* function;
	uint8_t* ip;
	Value* frameSlots;
	Value* defaultsStart;
	//Defaults required (i.e., non instantiated at call)
	int defaultsRequired;
} CallFrame;


typedef enum {
	INTERPRET_OK,
	INTERPRET_COMPILER_ERROR,
	INTERPRET_RUNTIME_ERROR
} InterpretResult;

typedef struct {
	CallFrame frames[FRAMES_MAX];
	int frameCount;

	//Value stack
	Value stack[STACK_MAX];
	Value* stackPtr;
	//Array of objects to be freed
	Obj* objects;
	//Interning strings
	Table internStrings;
	//Variables
	Table globals;
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