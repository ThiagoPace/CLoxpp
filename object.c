#include<stdio.h>
#include<string.h>
#include "common.h"
#include "memory.h"
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

static ObjString* allocateString(char* chars, int length) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->chars = chars;
	string->length = length;
	return string;
}

ObjString* takeString(char* chars, int length)
{
	return allocateString(chars, length);
}

ObjString* copyString(const char* chars, int length)
{
	char* heapString = ALLOCATE(char, length + 1);
	memcpy(heapString, chars, length);
	heapString[length] = '\0';
	return allocateString(heapString, length);
}

void printObj(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_STRING:
		printf("%s", AS_CSTRING(value));
		break;

	default:
		break;
	}
}
