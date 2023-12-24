#include <stdio.h>
#include<string.h>
#include <stdlib.h>

#include "common.h"
#include "memory.h"
#include "debug.h"
#include "chunk.h"
#include "vm.h"

/*
	NOTAS:
	NURN = Not Understood Right Now
	IPA = Interesting Pointer Arithmetic

	Declarações: são mts vezes tipo Chunk chunk, e daí usa &chunk
	Forward declaration
*/

#define MAX_INPUT_LENGTH 2000

static char* readFile(const char* path) {
	FILE* file;
	errno_t err = fopen_s(&file, path, "rb");
	if (err != 0 || file == NULL) {
		fprintf(stderr, "Error: Could not open file '%s' for reading.\n", path);
		return NULL;
	}

	fseek(file, 0L, SEEK_END);
	size_t fileSize = ftell(file);
	rewind(file);

	char* buffer = (char*)malloc(fileSize + 1);
	size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
	buffer[bytesRead] = '\0';

	fclose(file);
	return buffer;
}


int main(int argc, const char* argv[]) {
	if (argc == 2) {
		initVM();
		char* buffer = readFile(argv[1]);
		interpret(buffer);
	}
	else
	{
		char buffer[MAX_INPUT_LENGTH];

		printf("Enter a string: ");

		// Read input from the console
		if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
			// Remove the newline character at the end
			size_t len = strlen(buffer);
			if (len > 0 && buffer[len - 1] == '\n') {
				buffer[len - 1] = '\0';
			}
			for (int i = len - 1;i >= 0;i--) {
				if (buffer[i] == '$')
					buffer[i] = '\n';
			}

			// Print the entered string
			printf("%s\n", buffer);
		}
		else {
			// Handle error if fgets fails
			fprintf(stderr, "Error reading input\n");
			return 1;
		}
		initVM();
		interpret(buffer);
	}

	freeVM();

	//initVM();

	//Chunk chunk;
	//initChunk(&chunk);

	//writeChunk(&chunk, OP_CONSTANT, 123);
	//int constant = addConstant(&chunk, 1.2);
	//writeChunk(&chunk, constant, 123);

	//writeChunk(&chunk, OP_CONSTANT, 123);
	//constant = addConstant(&chunk, 3.1415);
	//writeChunk(&chunk, constant, 123);

	//writeChunk(&chunk, OP_ADD, 124);

	//writeChunk(&chunk, OP_CONSTANT, 123);
	//constant = addConstant(&chunk, 2.5);
	//writeChunk(&chunk, constant, 129);

	//writeChunk(&chunk, OP_MULTIPLY, 444);


	//writeChunk(&chunk, OP_CONSTANT, 123);
	//constant = addConstant(&chunk, 6);
	//writeChunk(&chunk, constant, 129);

	//writeChunk(&chunk, OP_CONSTANT, 123);
	//constant = addConstant(&chunk, 3);
	//writeChunk(&chunk, constant, 129);

	//writeChunk(&chunk, OP_MOD, 456);

	//writeChunk(&chunk, OP_RETURN, 123);

	//disassembleChunk(&chunk, "test chunk");

	////interpret(&chunk);

	//freeVM();
	//freeChunk(&chunk);

	return 0;
}