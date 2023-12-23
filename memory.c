#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#include "object.h"
#endif

#include "memory.h"
#include "compiler.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

void* reallocate(void* pointer, size_t oldSize, size_t newSize)
{
	vm.bytesAllocated += newSize - oldSize;
	if (newSize > oldSize) {
#ifdef DEBUG_STRESS_GC 
		collectGarbage();
#endif 
	}

	if (newSize == 0) {
		free(pointer);
		return NULL;
	}
	void* result = realloc(pointer, newSize);
	if (result == NULL)	exit(1);

	if (vm.bytesAllocated >= vm.nextGC) {
		vm.nextGC = vm.bytesAllocated * GC_HEAP_GROW_FACTOR;
		collectGarbage();
	}

	return result;
}

static void freeObj(Obj* object) {
#ifdef DEBUG_LOG_GC
	char* typeName = objTypeString(object->type);
	printf("%p free type %s\n", (void*)object, typeName);
#endif

	switch (object->type)
	{
	case OBJ_STRING: {
		ObjString* string = (ObjString*)object;
		FREE_ARRAY(char, string->chars, string->length + 1);
		FREE(ObjString, object);
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)object;
		freeChunk(&function->chunk);
		FREE(ObjFunction, function);
		break;
	}
	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)object;
		FREE_ARRAY(ObjUpvalue*, closure->upvalues, closure->upvalueCount);
		FREE(ObjClosure, closure);
		break;
	}
	case OBJ_UPVALUE: {
		FREE(ObjUpvalue, object);
		break;
	}
	case OBJ_CLASS: {
		ObjClass* klass = (ObjClass*)object;
		freeTable(&klass->methods);
		FREE(ObjClass, object);
		break;
	}
	case OBJ_INSTANCE: {
		ObjInstance* instance = (ObjInstance*)object;
		freeTable(&instance->fields);
		FREE(ObjInstance, instance);
		break;
	}
	case OBJ_BOUND_METHOD: {
		FREE(ObjBoundMethod, object);
		break;
	}
	}

}

void freeObjects() {
	Obj* object = vm.objects;
	while (object != NULL)
	{
		Obj* next = object->next;
		freeObj(object);
		object = next;
	}
}

void markValue(Value value)
{
	if (IS_OBJ(value))	markObj(AS_OBJ(value));
}

void markObj(Obj* obj)
{
	if (obj == NULL)	return;
	if (obj->gcMarked) return;

#ifdef DEBUG_LOG_GC
	printf("%p mark ", (void*)obj);
	printValue(OBJ_VAL(obj));
	printf("\n");
#endif

	obj->gcMarked = true;


	if (vm.grayCapacity <= vm.grayCount) {
		vm.grayCapacity = GROW_CAPACITY(vm.grayCapacity);
		vm.grayStack = (Obj**)realloc(vm.grayStack, sizeof(Obj*) * vm.grayCapacity);
	}
	if (vm.grayStack == NULL) {
		exit(1);
	}

	vm.grayStack[vm.grayCount++] = obj;
}


static void markRoots() {
	for (Value* val = vm.stack; val < vm.stackPtr; val++) {
		markValue(*val);
	}

	for (int i = 0;i < vm.frameCount;i++) {
		markObj((Obj*)vm.frames[i].closure);
	}

	for (ObjUpvalue* upval = vm.openUpvalues; upval != NULL; upval = upval->next) {
		markObj((Obj*)upval);
	}

	markTable(&vm.globals);
	markObj((Obj*)vm.initString);
	markCompilerRoots();
}

static void markArray(ValueArray* array) {
	for (int i = 0;i < array->count;i++) {
		markValue(array->values[i]);
	}
}
static void blackenObj(Obj* obj) {
#ifdef DEBUG_LOG_GC
	printf("%p blacken ", (void*)obj);
	printValue(OBJ_VAL(obj));
	printf("\n");
#endif

	switch (obj->type)
	{
	case OBJ_STRING:
		break;
	case OBJ_UPVALUE:
		markValue(((ObjUpvalue*)obj)->closed);
		break;
	case OBJ_CLOSURE: {
		ObjClosure* closure = (ObjClosure*)obj;
		markObj((Obj*)closure->function);
		for (int i = 0;i < closure->upvalueCount;i++) {
			markObj(closure->upvalues[i]);
		}
		break;
	}
	case OBJ_FUNCTION: {
		ObjFunction* function = (ObjFunction*)obj;
		markObj((Obj*)function->name);
		markArray(&function->chunk.constants);
		break;
	}
	case OBJ_CLASS: {
		ObjClass* klass = (ObjClass*)obj;
		markObj(klass->name);
		markTable(&klass->methods);
		break;
	 }
	case OBJ_INSTANCE: {
		ObjInstance* instance = (ObjInstance*)obj;
		markObj(instance->klass);
		markTable(&instance->fields);
		break;
	}
	case OBJ_BOUND_METHOD: {
		ObjBoundMethod* boundMethod = (ObjBoundMethod*)obj;
		markObj(boundMethod->method);
		markValue(boundMethod->receiver);
		break;
	}
	}
}

static void traceReferences() {
	while (vm.grayCount > 0)
	{
		Obj* obj = vm.grayStack[--vm.grayCount];
		blackenObj(obj);
	}
}

static void sweep() {
	Obj* previous = NULL;
	Obj* object = vm.objects;
	while (object != NULL)
	{
		if (object->gcMarked) {
			object->gcMarked = false;
			previous = object;
			object = object->next;
		}
		else {
			Obj* unreachable = object;
			object = object->next;
			if (previous != NULL) {
				previous->next = object;
			}
			else {
				vm.objects = object;
			}
			if (unreachable->type == OBJ_STRING) {
				ObjString* string = (ObjString*)unreachable;
			}
			freeObj(unreachable);
		}
	}
}

void collectGarbage()
{
#ifdef DEBUG_LOG_GC
	printf("-- gc begin\n");
	size_t before = vm.bytesAllocated;
#endif

	markRoots();
	traceReferences();
	tableRemoveWhite(&vm.internStrings);
	sweep();

#ifdef DEBUG_LOG_GC
	printf("-- gc end\n");
	printf(" collected %zu bytes (from %zu to %zu) next at %zu\n",
		before - vm.bytesAllocated, before, vm.bytesAllocated,
		vm.nextGC);
#endif
}
