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
		size_t instruction = frame->ip - frame->function->chunk.code - 1;
		int line = frame->function->chunk.lines[instruction];
		fprintf(stderr, "[line %d] in ", line);
		if (frame->function->name == NULL) {
			fprintf(stderr, "script\n");
		}
		else {
			fprintf(stderr, "%s()\n", frame->function->name->chars);
		}
	}

	resetStack();
}

void initVM()
{
	resetStack();
	vm.objects = NULL;
	initTable(&vm.internStrings);
	initTable(&vm.globals);
}


void freeVM()
{
	freeObjects();
	freeTable(&vm.internStrings);
	freeTable(&vm.globals);
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
	ObjString* b = AS_STRING(pop());
	ObjString* a = AS_STRING(pop());

	int length = a->length + b->length;
	char* chars = ALLOCATE(char, length + 1);
	memcpy(chars, a->chars, a->length);
	memcpy(chars + a->length, b->chars, b->length);
	chars[length] = '\0';

	ObjString* result = takeString(chars, length);
	push(OBJ_VAL(result));
}

static bool call(ObjFunction* function, int argCount) {
	if (argCount != function->arity) {
		runtimeError("Expected %d arguments but got %d.", function->arity, argCount);
		return false;
	}
	CallFrame* frame = &vm.frames[vm.frameCount++];
	if (vm.frameCount > FRAMES_MAX) {
		runtimeError("Stack overflow");
		return false;
	}

	frame->function = function;
	frame->ip = function->chunk.code;
	frame->frameSlots = vm.stackPtr - argCount - 1;
	return true;
}

static bool callValue(Value callee, int argCount) {
	if (IS_OBJ(callee)) {
		switch (OBJ_TYPE(callee))
		{
		case OBJ_FUNCTION: {
			return call(AS_FUNCTION(callee), argCount);
		}
		default: break;
		}
	}
	runtimeError("Can only call functions and classes.");
	return false;
}

InterpretResult interpret(char* source)
{
	ObjFunction* function = compile(source);
	if (function == NULL) return INTERPRET_COMPILER_ERROR;

	push(OBJ_VAL(function));
	call(function, 0);
	/*CallFrame* frame = &vm.frames[vm.frameCount++];
	frame->function = function;
	frame->ip = function->chunk.code;
	frame->frameSlots = vm.stack;*/

	InterpretResult result = run();

	return result;
}

InterpretResult run()
{
	CallFrame* currentFrame = &vm.frames[vm.frameCount - 1];

#define READ_BYTE() (*currentFrame->ip++)
#define READ_CONSTANT() (currentFrame->function->chunk.constants.values[READ_BYTE()])
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
		dissassembleInstruction(&currentFrame->function->chunk, (int)(currentFrame->ip - currentFrame->function->chunk.code));
#endif // DEBUG_TRACE_EXECUTION

		uint8_t instruction;
		switch (instruction = READ_BYTE())
		{
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

		case OP_PRINT:
			printValue(pop());
			printf("\n");
			break;
		case OP_POP: pop(); break;

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


		case OP_CALL: {
			int argCount = READ_BYTE();
			if (!callValue(peek(argCount) ,argCount)) {
				return INTERPRET_RUNTIME_ERROR;
			}
			currentFrame = &vm.frames[vm.frameCount - 1];
			break;
		}

		case OP_RETURN: {
			Value result = pop();
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

		default:
			break;
		}
	}
	

#undef BINARY_OP
#undef READ_SHORT
#undef READ_STRING
#undef READ_CONSTANT
#undef READ_BYTE
}
