#include<stdio.h>
#include<string.h>
#include "common.h"
#include "memory.h"
#include "table.h"
#include "vm.h"
#include "object.h"


//Macro used to no make unnecessary void* cast
#define ALLOCATE_OBJ(type, objectType) \
			(type*)allocateObj(sizeof(type), objectType)


static Obj* allocateObj(size_t size, ObjType type) {
	Obj* object = reallocate(NULL, 0, size);
	object->type = type;

	object->next = vm.objects;
	vm.objects = object;
	return object;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->chars = chars;
	string->length = length;
	string->hash = hash;

	tableSet(&vm.internStrings, string, NIL_VAL);
	return string;
}

static uint32_t hashString(char* key, int length) {
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++) {
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}
	return hash;
}

ObjString* takeString(char* chars, int length)
{
	uint32_t hash = hashString(chars, length);
	ObjString* intern = findTableString(&vm.internStrings, chars, length, hash);
	if (intern != NULL) {
		FREE_ARRAY(char, chars, length);
		return intern;
	}
	return allocateString(chars, length, hash);
}

ObjString* copyString(const char* chars, int length)
{
	uint32_t hash = hashString(chars, length);
	ObjString* intern = findTableString(&vm.internStrings, chars, length, hash);
	if (intern != NULL) return intern;

	char* heapString = ALLOCATE(char, length + 1);
	memcpy(heapString, chars, length);
	heapString[length] = '\0';

	return allocateString(heapString, length, hash);
}

ObjFunction* newFunction()
{
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->defaults = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}


void printObj(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_STRING:
		printf("%s", AS_CSTRING(value));
		break;

	case OBJ_FUNCTION:
		if (AS_FUNCTION(value)->name == NULL)
			printf("<script>");
		else
			printf("<func %s>", AS_FUNCTION(value)->name->chars);
		break;

	default:
		break;
	}
}
