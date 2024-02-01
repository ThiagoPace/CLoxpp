#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "compiler.h"
#include "debug.h"
#include "memory.h"
#include "vm.h"

VM vm;

static void resetStack() {
	vm.stackPtr = vm.stack;
	vm.frameCount = 0;
}

static void runtimeError(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	//NURN: pq size_t no livro e n int?
	for (int i = vm.frameCount - 1;i >= 0;i--) {
		CallFrame* frame = &vm.frames[i];
		ObjFunction* function = frame->closure->function;
		size_t instruction = frame->ip - function->chunk.code - 1;
		int line = function->chunk.lines[instruction];
		fprintf(stderr, "[line %d] in ", line);
		if (function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", function->name->chars);
		}
	}

	resetStack();
}

void initVM()
{
	//GC
	vm.grayCount = 0;
	vm.grayCapacity = 0;
	vm.grayStack = NULL;
	vm.bytesAllocated = 0;
	vm.nextGC = 1024 * 1024;

	resetStack();
	vm.objects = NULL;
	vm.openUpvalues = NULL;


	initTable(&vm.internStrings);
	initTable(&vm.globals);

	//Initialize as NULL to avoid GC problems
	vm.initString = NULL;
	vm.initString = copyString("init", 4);
}


void freeVM()
{
	freeObjects();
	free(vm.grayStack);
	freeTable(&vm.internStrings);
	freeTable(&vm.globals);
	vm.initString = NULL;
}

void push(Value value)
{
	*vm.stackPtr = value;
	vm.stackPtr++;
}

static Value peek(int distance) {
	return vm.stackPtr[-1 - distance];
}

Value pop()
{
	vm.stackPtr--;
	return *vm.stackPtr;
}

static bool isFalse(Value value) {
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}
static void concatenate() {
	ObjString* b = AS_STRING(peek(0));
	ObjString* a = AS_STRING(peek(1));

	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);

	pop();
	pop();
	push(OBJ_VAL(result));
}

#pragma region Calls and methods
static bool call(ObjClosure* closure, int argCount) {
	ObjFunction* function = closure->function;
	int defaultsRequired = 0;
	if (argCount != function->arity) {
		if (argCount < function->arity - function->defaults || argCount > function->arity) {
			//Error message can be improved
			runtimeError("Expected %d arguments but got %d.", function->arity, argCount);
			return false;
		}

		defaultsRequired = function->arity - argCount;
		for (int i = 0; i < defaultsRequired;i++) {
			push(NIL_VAL);
		}
	}

	CallFrame* frame = &vm.frames[vm.frameCount++];
	if (vm.frameCount > FRAMES_MAX) {
		runtimeError("Stack overflow");
		return false;
	}
	frame->closure = closure;
	frame->ip = function->chunk.code;
	frame->frameSlots = vm.stackPtr - (argCount + defaultsRequired) - 1;
	frame->defaultsStart = vm.stackPtr - function->defaults;
	frame->defaultsRequired = defaultsRequired;
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee))
		{
		case OBJ_BOUND_METHOD: {
			ObjBoundMethod* boundMethod = AS_BOUND_METHOD(callee);
			vm.stackPtr[-argCount - 1] = boundMethod->receiver;
			return call(boundMethod->method, argCount);
		}
		case OBJ_CLASS: {
			ObjClass* klass = AS_CLASS(callee);
			vm.stackPtr[-argCount - 1] = OBJ_VAL(newInstance(klass));

			Value initializer;
			if (tableGet(&klass->methods, vm.initString, &initializer)) {
				return call(AS_CLOSURE(initializer), argCount);
			}
			else if (argCount != 0) {
				runtimeError("Expected 0 arguments but got %d.", argCount);
				return false;
			}
			return true;
		}
		case OBJ_CLOSURE: {
			return call(AS_CLOSURE(callee), argCount);
		}
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
}

static bool bindMethod(ObjClass* klass, ObjString* name) {
	Value method;
	if (!tableGet(&klass->methods, name, &method)) {
		runtimeError("Undefined property '%s'.", name->chars);
		return false;
	}

	ObjBoundMethod* boundMethod = newBoundMethod(peek(0), AS_CLOSURE(method));

	pop();
	push(OBJ_VAL(boundMethod));
	return true;
}

static void defineMethod(ObjString* name) {
	Value method = peek(0);
	ObjClass* klass = AS_CLASS(peek(1));
	//Table set isn't actually setting
	tableSet(&klass->methods, name, method);
	//pop();
}
#pragma endregion

#pragma region Upvalues
static ObjUpvalue* captureUpvalue(Value* value) {
	ObjUpvalue* previousUpvalue = NULL;
	ObjUpvalue* upvalue = vm.openUpvalues;

	while (upvalue != NULL && upvalue->location > value) {
		previousUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	if (upvalue != NULL && upvalue->location == value) {
		return upvalue;
	}

	ObjUpvalue* createdUpvalue = newUpvalue(value);
	createdUpvalue->next = upvalue;

	if (previousUpvalue == NULL) {
		vm.openUpvalues = createdUpvalue;
	}
	else{
		previousUpvalue->next = createdUpvalue;
	}

	return createdUpvalue;
}

static void closeUpvariable(Value* last) {
	while (vm.openUpvalues != NULL && vm.openUpvalues->location >= last)
	{
		ObjUpvalue* upvalue = vm.openUpvalues;
		upvalue->closed = *upvalue->location;
		upvalue->location = &upvalue->closed;
		vm.openUpvalues = upvalue->next;
	}
}
#pragma endregion

InterpretResult interpret(char* source)
{
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILER_ERROR;

	push(OBJ_VAL(function));
	ObjClosure* closure = newClosure(function);
	pop();
	push(OBJ_VAL(closure));
	call(closure, 0);

	InterpretResult result = run();

	return result;
}

InterpretResult run()
{
	CallFrame* currentFrame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*currentFrame->ip++)
#define READ_CONSTANT() (currentFrame->closure->function->chunk.constants.values[READ_BYTE()])
#define READ_STRING() AS_STRING(READ_CONSTANT())
#define READ_SHORT() (currentFrame->ip +=2, (uint16_t)((currentFrame->ip[-2] << 8) | currentFrame->ip[-1]))
#define BINARY_OP(valueType, op) \
		do { \
			if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) { \
				runtimeError("Operands must be numbers."); \
				return INTERPRET_RUNTIME_ERROR; \
			} \
			double b = AS_NUMBER(pop()); \
			double a = AS_NUMBER(pop()); \
			push(valueType(a op b)); \
		} while (false)


	for (;;) {
#ifdef DEBUG_TRACE_EXECUTION
		printf(" ");
		for (Value* slot = vm.stack; slot < vm.stackPtr; slot++) {
			printf("[ ");
			printValue(*slot);
			printf(" ]");
		}
		printf("\n");
		dissassembleInstruction(&currentFrame->closure->function->chunk, (int)(currentFrame->ip - currentFrame->closure->function->chunk.code));
#endif // DEBUG_TRACE_EXECUTION

		uint8_t instruction;
		switch (instruction = READ_BYTE())
		{
		case OP_PRINT:
			printValue(pop());
			printf("\n");
			break;
		case OP_POP: pop(); break;

#pragma region Values
		case OP_CONSTANT:
			push(READ_CONSTANT());
			break;
		case OP_NIL:
			push(NIL_VAL);
			break;
		case OP_TRUE:
			push(BOOL_VAL(true));
			break;
		case OP_FALSE:
			push(BOOL_VAL(false));
			break;
#pragma endregion

#pragma region Arithmetic
		case OP_EQUAL: {
			Value b = pop();
			Value a = pop();
			push(BOOL_VAL(valuesEqual(a, b)));
			break;
		}

		case OP_GREATER:
			BINARY_OP(BOOL_VAL, >);
			break;

		case OP_LESS:
			BINARY_OP(BOOL_VAL, < );
			break;

		case OP_NOT:
			push(BOOL_VAL(isFalse(pop())));
			break;

		case OP_NEGATE: 
			if (!IS_NUMBER(peek(0))) {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			push(NUMBER_VAL(-AS_NUMBER(pop()))); break;
		case OP_ADD: {
			if (IS_NUMBER(peek(0)) && IS_NUMBER(peek(1))) {
				BINARY_OP(NUMBER_VAL, +);
			}
			else if (IS_STRING(peek(0)) && IS_STRING(peek(1))) {
				concatenate();
			}
			else {
				runtimeError("Operands must be two numbers or two strings.");
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_SUBTRACT: BINARY_OP(NUMBER_VAL, -); break;
		case OP_MULTIPLY: BINARY_OP(NUMBER_VAL, *); break;
		case OP_DIVIDE: BINARY_OP(NUMBER_VAL , /); break;
		case OP_MOD: {
			if (!IS_NUMBER(peek(0)) || !IS_NUMBER(peek(1))) {
				runtimeError("Operand must be a number.");
				return INTERPRET_RUNTIME_ERROR;
			}
			int b = (int)AS_NUMBER(pop());
			int a = (int)AS_NUMBER(pop());
			push(NUMBER_VAL(a % b));
			break;
		}
#pragma endregion

#pragma region Variables
		case OP_DEFINE_GLOBAL: {
			ObjString* name = READ_STRING();
			tableSet(&vm.globals, name, peek(0));
			pop();
			break;
		}
		case OP_GET_GLOBAL: {
			ObjString* name = READ_STRING();
			Value value;
			if (!tableGet(&vm.globals, name, &value)) {
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			push(value);
			break;
		}
		case OP_SET_GLOBAL: {
			ObjString* name = READ_STRING();
			if (tableSet(&vm.globals, name, peek(0))) {
				tableDelete(&vm.globals, name);
				runtimeError("Undefined variable '%s'.", name->chars);
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}

		case OP_GET_LOCAL: {
			uint8_t slot = READ_BYTE();
			push(currentFrame->frameSlots[slot]);
			break;
		}

		case OP_SET_LOCAL: {
			uint8_t slot = READ_BYTE();
			currentFrame->frameSlots[slot] = peek(0);
			break;
		}
#pragma endregion

#pragma region Control Flow
		case OP_JUMP_IF_FALSE: {
			uint16_t jumpOffset = READ_SHORT();
			if (isFalse(peek(0)))
				currentFrame->ip += jumpOffset;
			break;
		}
		case OP_JUMP: {
			uint16_t jumpOffset = READ_SHORT();
			currentFrame->ip += jumpOffset;
			break;
		}
		case OP_LOOP: {
			uint16_t loopOffset = READ_SHORT();
			currentFrame->ip -= loopOffset;
			break;
		}
#pragma endregion

#pragma region Closures
		case OP_CLOSURE: {
			ObjFunction* function = AS_FUNCTION(READ_CONSTANT());
			ObjClosure* closure = newClosure(function);

			for (int i = 0;i < closure->upvalueCount;i++) {
				uint8_t isLocal = READ_BYTE();
				uint8_t index = READ_BYTE();

				if (isLocal) {
					closure->upvalues[i] = captureUpvalue(currentFrame->frameSlots + index);
				}
				else{
					closure->upvalues[i] = currentFrame->closure->upvalues[index];
				}
			}
			push(OBJ_VAL(closure));

			break;
		}
		case OP_GET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			push(*currentFrame->closure->upvalues[slot]->location);
			break;
		}
		case OP_SET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			*currentFrame->closure->upvalues[slot]->location = peek(0);
			break;
		}
		case OP_CLOSE_UPVALUE: {
			closeUpvariable(vm.stackPtr - 1);
			pop();
			break;
		}
#pragma endregion

#pragma region Functions
		case OP_SET_DEFAULT: {
			int defCount = READ_BYTE();
			if (defCount < currentFrame->closure->function->defaults - currentFrame->defaultsRequired) {
				pop();
				break;
			}
			Value def = pop();
			currentFrame->defaultsStart[defCount] = def;
			break;
		}

		case OP_CALL: {
			int argCount = READ_BYTE();
			if (!callValue(peek(argCount), argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}

			currentFrame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_RETURN: {
			Value result = pop();
			closeUpvariable(currentFrame->frameSlots);
			vm.frameCount--;
			if (vm.frameCount == 0) {
				pop();
				return INTERPRET_OK;
			}
			vm.stackPtr = currentFrame->frameSlots;
			push(result);
			currentFrame = &vm.frames[vm.frameCount - 1];
			break;
		}
#pragma endregion

#pragma region Classes
		case OP_CLASS: {
			push(OBJ_VAL(newClass(READ_STRING())));
			break;
		}
		case OP_GET_PROPERTY: {
			if (!IS_INSTANCE(peek(0))) {
				runtimeError("Only instances have properties.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(0));
			ObjString* name = READ_STRING();

			Value value;
			if (tableGet(&instance->fields, name, &value)) {
				pop();
				push(value);
				break;
			}

			if (!bindMethod(instance->klass, name)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			break;
		}
		case OP_SET_PROPERTY: {
			if (!IS_INSTANCE(peek(1))) {
				runtimeError("Only instances have fields.");
				return INTERPRET_RUNTIME_ERROR;
			}

			ObjInstance* instance = AS_INSTANCE(peek(1));
			tableSet(&instance->fields, READ_STRING(), peek(0));
			
			Value popped = pop();
			pop();
			push(popped);
			break;
		}
		case OP_METHOD:
			defineMethod(READ_STRING());
			break;
#pragma endregion
		}
	}
	

#undef BINARY_OP
#undef READ_SHORT
#undef READ_STRING
#undef READ_CONSTANT
#undef READ_BYTE
}
