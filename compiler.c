#include <stdlib.h>
#include <stdio.h>
#include "compiler.h"
#include "lexer.h"

typedef struct{
	Token current;
	Token previous;
	bool hadError;
	//Panic flag has yet to be cleared and synchronized
	bool panicMode;
} Parser;

typedef enum {
	PREC_NONE,
	PREC_ASSIGNMENT, // =
	PREC_OR, // or
	PREC_AND, // and
	PREC_EQUALITY, // == !=
	PREC_COMPARISON, // < > <= >=
	PREC_TERM, // + -
	PREC_FACTOR, // * /
	PREC_UNARY, // ! -
	PREC_CALL, // . ()
	PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)();
typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

Parser parser;

Chunk* compillingChunk;
static Chunk* currentChunk() {
	return compillingChunk;
}


static void errorAt(Token* token, char* message) {
	if (parser.panicMode) return;

	fprintf(stderr, "[Line %d] Error ", token->line);
	if (token->type == TOKEN_ERROR) {
		//Nothing
	}
	else if (token->type == TOKEN_EOF) {
		fprintf(stderr, "at end");
	}
	else{
		fprintf(stderr, "at %.*s", token->length, token->lexemeStart);
	}
	fprintf(stderr, ": %s\n", message);

	parser.panicMode = true;
	parser.hadError = true;
}

static void errorAtCurrent(char* message) {
	errorAt(&parser.current, message);
}

static void error(char* message) {
	errorAt(&parser.previous, message);
}

static void advance() {
	parser.previous = parser.current;
	for (;;) {
		printf("qqqq");
		parser.current = lexToken();

		if (parser.current.type != TOKEN_ERROR)	break;
		//NURN
		errorAtCurrent(parser.current.lexemeStart);
	}
}

static void consume(TokenType type, char* message) {
	if (parser.current.type == type) {
		advance();
		return;
	}
	else{
		errorAtCurrent(message);
	}
}

static void emitByte(uint8_t byte) {
	writeChunk(currentChunk(), byte, parser.previous.line);
}
static void emitBytes(uint8_t byte1, uint8_t byte2) {
	emitByte(byte1);
	emitByte(byte2);
}
static void emitReturn() {
	emitByte(OP_RETURN);
}

static void endCompile() {
	emitReturn();
}

static uint8_t makeConstant(Value value) {
	int constant = addConstant(currentChunk(), value);
	if (constant >= UINT8_MAX) {
		error("Too many constants in one chunk.");
		return 0;
	}
	return (uint8_t)constant;
}

static void expression();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}
static void number() {
	double value = strtod(parser.previous.lexemeStart, NULL);
	emitBytes(OP_CONSTANT, makeConstant(value));
}
static void grouping() {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}
static void unary() {
	TokenType operatorType = parser.previous.type;
	parsePrecedence(PREC_UNARY);

	switch (operatorType)
	{
	case TOKEN_MINUS:
		emitByte(OP_NEGATE);
		break;

	}

}

static void binary() {
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType)
	{
	case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
	case TOKEN_PLUS: emitByte(OP_ADD); break;
	case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
	case TOKEN_PERCENT: emitByte(OP_MOD); break;

	}
}
ParseRule rules[] = {
[TOKEN_LEFT_PAREN] = {grouping, NULL, PREC_NONE},
[TOKEN_RIGHT_PAREN] = {NULL, NULL, PREC_NONE},
[TOKEN_LEFT_BRACE] = {NULL, NULL, PREC_NONE},
[TOKEN_RIGHT_BRACE] = {NULL, NULL, PREC_NONE},
[TOKEN_COMMA] = {NULL, NULL, PREC_NONE},
[TOKEN_DOT] = {NULL, NULL, PREC_NONE},
[TOKEN_MINUS] = {unary, binary, PREC_TERM},
[TOKEN_PLUS] = {NULL, binary, PREC_TERM},
[TOKEN_SEMICOLON] = {NULL, NULL, PREC_NONE},
[TOKEN_SLASH] = {NULL, binary, PREC_FACTOR},
[TOKEN_STAR] = {NULL, binary, PREC_FACTOR},
[TOKEN_BANG] = {NULL, NULL, PREC_NONE},
[TOKEN_BANG_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_EQUAL_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_GREATER] = {NULL, NULL, PREC_NONE},
[TOKEN_GREATER_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_LESS] = {NULL, NULL, PREC_NONE},
[TOKEN_LESS_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_IDENTIFIER] = {NULL, NULL, PREC_NONE},
[TOKEN_STRING] = {NULL, NULL, PREC_NONE},
[TOKEN_NUMBER] = {number, NULL, PREC_NONE},
[TOKEN_AND] = {NULL, NULL, PREC_NONE},
[TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
[TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
[TOKEN_FALSE] = {NULL, NULL, PREC_NONE},
[TOKEN_FOR] = {NULL, NULL, PREC_NONE},
[TOKEN_FUN] = {NULL, NULL, PREC_NONE},
[TOKEN_IF] = {NULL, NULL, PREC_NONE},
[TOKEN_NIL] = {NULL, NULL, PREC_NONE},
[TOKEN_OR] = {NULL, NULL, PREC_NONE},
[TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
[TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
[TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
[TOKEN_THIS] = {NULL, NULL, PREC_NONE},
[TOKEN_TRUE] = {NULL, NULL, PREC_NONE},
[TOKEN_VAR] = {NULL, NULL, PREC_NONE},
[TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
[TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
[TOKEN_EOF] = {NULL, NULL, PREC_NONE},

[TOKEN_PERCENT] = {NULL, binary, PREC_FACTOR}
};

static ParseRule* getRule(TokenType type) {
	return &rules[type];
}

static void parsePrecedence(Precedence precedence) {
	advance();

	ParseFn prefix = getRule(parser.previous.type)->prefix;
	if (prefix == NULL) {
		error("Expect expression.");
		return;
	}

	prefix();

	while (precedence <= getRule(parser.current.type)->precedence)
	{
		advance();
		ParseFn infix = getRule(parser.previous.type)->infix;
		infix();
	}
}

bool compile(const char* source, Chunk* chunk)
{
	//Initializations
	initLexer(source);
	parser.hadError = false;
	parser.panicMode = false;
	compillingChunk = chunk;

	advance();

	expression();

	consume(TOKEN_EOF, "Expect end of expression.");

	endCompile();
	return !parser.hadError;
}
