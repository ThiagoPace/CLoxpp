#include "vm.h"
#include "compiler.h"
#include "debug.h"

VM vm;

static void resetStack() {
	vm.stackPtr = vm.stack;
}
void initVM()
{
	resetStack();
}


void freeVM()
{
}

InterpretResult interpret(char* source)
{
	Chunk chunk;
	initChunk(&chunk);

	if (!compile(source, &chunk)) {
		freeChunk(&chunk);
		return INTERPRET_COMPILER_ERROR;
	}

	vm.chunk = &chunk;
	vm.ip = vm.chunk->code;
	InterpretResult result = run();

	freeChunk(&chunk);

	return result;
}

InterpretResult run()
{
#define READ_BYTE() (*vm.ip++)
#define READ_CONSTANT() (vm.chunk->constants.values[READ_BYTE()])
#define BINARY_OP(op) \
		do { \
			double b = pop(); \
			double a = pop(); \
			push(a op b); \
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
		dissassembleInstruction(vm.chunk, (int)(vm.ip - vm.chunk->code));
#endif // DEBUG_TRACE_EXECUTION

		uint8_t instruction;
		switch (instruction = READ_BYTE())
		{

		case OP_NEGATE: push(-pop()); break;
		case OP_ADD: BINARY_OP(+); break;
		case OP_SUBTRACT: BINARY_OP(-); break;
		case OP_MULTIPLY: BINARY_OP(*); break;
		case OP_DIVIDE: BINARY_OP(/); break;
		case OP_MOD: {
			int b = (int)pop();
			int a = (int)pop();
			push(a % b);
			break;
		}
			
		case OP_CONSTANT:
			push(READ_CONSTANT());
			break;

		case OP_RETURN: 
			printValue(pop());
			printf("\n");
			return INTERPRET_OK;

		default:
			break;
		}
	}
	

#undef BINARY_OP
#undef READ_CONSTANT()
#undef READ_BYTE
}

void push(Value value)
{
	*vm.stackPtr = value;
	vm.stackPtr++;
}

Value pop()
{
	vm.stackPtr--;
	return *vm.stackPtr;
}
