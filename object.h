#ifndef object_h
#define object_h

#include "common.h"
#include "chunk.h"
#include "value.h"
#include "table.h"


#define OBJ_TYPE(value) (AS_OBJ(value)->type)

#define IS_STRING(value) isObjType(value, OBJ_STRING)
#define AS_STRING(value) ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)

#define IS_UPVALUE(value) isObjType(value, OBJ_UPVALUE)
#define AS_UPVALUE(value) ((ObjUpvalue*)AS_OBJ(value))

#define IS_FUNCTION(value) isObjType(value, OBJ_FUNCTION)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))

#define IS_CLOSURE(value) isObjType(value, OBJ_CLOSURE)
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))

#define IS_CLASS(value) isObjType(value, OBJ_CLASS)
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))

#define IS_INSTANCE(value) isObjType(value, OBJ_INSTANCE)
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))

#define IS_BOUND_METHOD(value) isObjType(value, OBJ_BOUND_METHOD)
#define AS_BOUND_METHOD(value) ((ObjBoundMethod*)AS_OBJ(value))


typedef enum {
	OBJ_STRING,
	OBJ_UPVALUE,
	OBJ_FUNCTION,
	OBJ_CLOSURE,
	OBJ_CLASS,
	OBJ_INSTANCE,
	OBJ_BOUND_METHOD,
} ObjType;

char* objTypeString(ObjType type);

struct Obj
{
	ObjType type;

	bool gcMarked;
	//NURN sintaxe
	struct Obj* next;
};

struct ObjString
{
	Obj obj;
	int length;
	char* chars;

	uint32_t hash;
};

typedef struct {
	Obj obj;
	Value* location;
	Value closed;

	struct ObjUpvalue* next;
} ObjUpvalue;

typedef struct{
	Obj obj;
	int arity;
	int defaults;

	int upvalueCount;

	Chunk chunk;
	ObjString* name;
} ObjFunction;

typedef struct {
	Obj obj;
	ObjFunction* function;

	ObjUpvalue** upvalues;
	int upvalueCount;
} ObjClosure;


typedef struct {
	Obj obj;
	ObjString* name;
	int arity;

	Table methods;
} ObjClass;

typedef struct {
	Obj obj;
	ObjClass* klass;
	Table fields;
} ObjInstance;

typedef struct {
	Obj obj;
	Value receiver;
	ObjClosure* method;
} ObjBoundMethod;

static inline bool isObjType(Value value, ObjType type) {
	return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

ObjString* takeString(char* chars, int length);
ObjString* copyString(const char* chars, int length);

ObjUpvalue* newUpvalue(Value* slot);

ObjFunction* newFunction();

ObjClosure* newClosure(ObjFunction* function);

ObjClass* newClass(ObjString* name);

ObjInstance* newInstance(ObjClass* klass);

ObjBoundMethod* newBoundMethod(Value receiver, ObjClosure* closure);

void printObj(Value value);

#endif // !object_h
