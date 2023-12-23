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

	object->gcMarked = false;

	object->next = vm.objects;
	vm.objects = object;

#ifdef DEBUG_LOG_GC
	char* typeName = objTypeString(type);
	printf("%p allocate %zu for %s\n", (void*)object, size, typeName);
#endif

	return object;
}

static ObjString* allocateString(char* chars, int length, uint32_t hash) {
	ObjString* string = ALLOCATE_OBJ(ObjString, OBJ_STRING);
	string->chars = chars;
	string->length = length;
	string->hash = hash;

	push(OBJ_VAL(string));
	tableSet(&vm.internStrings, string, NIL_VAL);
	pop();
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

char* objTypeString(ObjType type)
{
	switch (type)
	{
	case OBJ_CLASS: return "OBJ_CLASS";
	case OBJ_BOUND_METHOD: return "OBJ_BOUND_METHOD";
	case OBJ_CLOSURE: return "OBJ_CLOSURE";
	case OBJ_FUNCTION: return "OBJ_FUNCTION";
	case OBJ_INSTANCE: return "OBJ_INSTANCE";
	case OBJ_STRING: return "OBJ_STRING";
	case OBJ_UPVALUE: return "OBJ_UPVALUE";
	}
	return "UNKOWN_OBJ_TYPE";
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

ObjUpvalue* newUpvalue(Value* slot)
{
	ObjUpvalue* upvalue = ALLOCATE_OBJ(ObjUpvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	upvalue->next = NULL;
	upvalue->closed = NIL_VAL;
	return upvalue;
}

ObjFunction* newFunction()
{
	ObjFunction* function = ALLOCATE_OBJ(ObjFunction, OBJ_FUNCTION);
	function->arity = 0;
	function->defaults = 0;
	function->upvalueCount = 0;
	function->name = NULL;
	initChunk(&function->chunk);
	return function;
}

ObjClosure* newClosure(ObjFunction* function)
{
	ObjClosure* closure = ALLOCATE_OBJ(ObjClosure, OBJ_CLOSURE);
	closure->function = function;

	ObjUpvalue** upvalues = ALLOCATE(ObjUpvalue*, function->upvalueCount);
	for (int i = 0;i < function->upvalueCount;i++) {
		upvalues[i] = NULL;
	}
	closure->upvalues = upvalues;
	closure->upvalueCount = function->upvalueCount;
	return closure;
}

ObjClass* newClass(ObjString* name)
{
	ObjClass* klass = ALLOCATE_OBJ(ObjClass, OBJ_CLASS);
	klass->name = name;
	klass->arity = 0;
	initTable(&klass->methods);
	return klass;
}

ObjInstance* newInstance(ObjClass* klass)
{
	ObjInstance* instance = ALLOCATE_OBJ(ObjInstance, OBJ_INSTANCE);
	instance->klass = klass;
	initTable(&instance->fields);
	return instance;
}

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* closure)
{
	ObjBoundMethod* boundMethod = ALLOCATE_OBJ(ObjBoundMethod, OBJ_BOUND_METHOD);
	boundMethod->method = closure;
	boundMethod->receiver = receiver;
	return boundMethod;
}

static void printFunction(ObjFunction* function) {
	if (function->name == NULL)
		printf("<script>");
	else
		printf("<func %s>", function->name->chars);
}

void printObj(Value value)
{
	switch (OBJ_TYPE(value))
	{
	case OBJ_STRING:
		printf("%s", AS_CSTRING(value));
		break;

	case OBJ_FUNCTION:
		printFunction(AS_FUNCTION(value));
		break;
	case OBJ_CLOSURE:
		printFunction(AS_CLOSURE(value)->function);
		break;
	case OBJ_UPVALUE: {
		printf("upvalue");
		break;
	}
	case OBJ_CLASS: {
		printf("%s", AS_CLASS(value)->name->chars);
		break;
	}
	case OBJ_INSTANCE: {
		printf("%s instance", AS_INSTANCE(value)->klass->name->chars);
		break;
	}
	case OBJ_BOUND_METHOD: {
		printFunction(AS_BOUND_METHOD(value)->method);
		break;
	}
	default:
		break;
	}
}
