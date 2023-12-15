#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "object.h"
#include "compiler.h"
#include "lexer.h"
#include "debug.h"

#define UINT8_COUNT (UINT8_MAX + 1)

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

typedef void (*ParseFn)(bool canAssign);
typedef struct {
	ParseFn prefix;
	ParseFn infix;
	Precedence precedence;
} ParseRule;

Parser parser;

typedef struct {
	Token name;
	int depth;
} Local;

typedef struct {
	Local locals[UINT8_COUNT];
	int localCount;
	int scopeDepth;
} Compiler;

Compiler* current = NULL;

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
		log("q");
		parser.current = lexToken();

		if (parser.current.type != TOKEN_ERROR)	break;
		//NURN
		errorAtCurrent(parser.current.lexemeStart);
	}
}
static bool check(TokenType type) {
	return parser.current.type == type;
}
static bool match(TokenType type) {
	if (!check(type))	return false;
	advance();
	return true;
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
static void emitConstant() {

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

static void initCompiler(Compiler* compiler) {
	compiler->localCount = 0;
	compiler->scopeDepth = 0;
	current = compiler;
}

static void expression();
static void statement();
static void declaration();
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Precedence precedence);

static void beginScope() {
	current->scopeDepth++;
}
static void endScope() {
	current->scopeDepth--;

	while (current->localCount > 0 && current->locals[current->localCount - 1].depth > current->scopeDepth)
	{
		emitByte(OP_POP);
		current->localCount--;
	}
}

static void expression() {
	parsePrecedence(PREC_ASSIGNMENT);
}

static void block() {
	while (!check(TOKEN_RIGHT_BRACE) && !check(TOKEN_EOF))
	{
		declaration();
	}
	consume(TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static uint8_t identifierConstant(Token* name) {
	return makeConstant(OBJ_VAL( copyString(name->lexemeStart, name->length)   ));
}

static bool identifiersEqual(Token* a, Token* b) {
	if (a->length != b->length)	return false;
	return memcmp(a->lexemeStart, b->lexemeStart, a->length) == 0;
}

static int resolveLocal(Compiler* compiler, Token* name) {
	for (int i = compiler->localCount - 1; i >= 0 ;i--) {
		if (identifiersEqual(name, &(compiler->locals[i].name))) {
			return i;
		}
	}
	return -1;
}

static void addLocal(Token* name) {
	Local* local = &current->locals[current->localCount++];
	local->name = *name;
	local->depth = current->scopeDepth;
}

//For locals only
static void declareVariable() {
	if (current->scopeDepth == 0)	return;

	Token* name = &parser.previous;
	addLocal(name);
}

static uint8_t parseVariable(char* message) {
	consume(TOKEN_IDENTIFIER, message);

	declareVariable();
	if (current->scopeDepth > 0)	return;

	return identifierConstant(&parser.previous);
}
static void defineVariable(uint8_t global) {
	if (current->scopeDepth > 0)	return;
	emitBytes(OP_DEFINE_GLOBAL, global);
}

static void varDeclaration() {
	uint8_t global = parseVariable("Expect variable name.");
	
	if (match(TOKEN_EQUAL))
		expression();
	else
	{
		emitByte(OP_CONSTANT, VAL_NIL);
	}
	consume(TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
	defineVariable(global);
}

static void printStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after print statement.");
	emitByte(OP_PRINT);
}
static void expressionStatement() {
	expression();
	consume(TOKEN_SEMICOLON, "Expect ';' after expression statement.");
	emitByte(OP_POP);
}

static void statement() {
	if (match(TOKEN_PRINT))
		printStatement();	
	else if (match(TOKEN_LEFT_BRACE)) {
		beginScope();
		block();
		endScope();
	}
	else
		expressionStatement();
}
static void declaration() {
	if (match(TOKEN_VAR))
		varDeclaration();
	else
		statement();
}

static void number(bool canAssign) {
	double value = strtod(parser.previous.lexemeStart, NULL);
	emitBytes(OP_CONSTANT, makeConstant(NUMBER_VAL(value)));
}
static void string(bool canAssign) {
	emitBytes(OP_CONSTANT, makeConstant(OBJ_VAL(copyString(parser.previous.lexemeStart + 1, parser.previous.length - 2))));
}
static void namedVariable(Token* name, bool canAssign) {
	uint8_t getOp, setOp;
	int arg = resolveLocal(current, name);

	if (arg == -1) {
		getOp = OP_GET_GLOBAL;
		setOp = OP_SET_GLOBAL;
		arg = identifierConstant(name);
	}
	else{
		getOp = OP_GET_LOCAL;
		setOp = OP_SET_LOCAL;
	}

	//Order below matters
	if (canAssign && match(TOKEN_EQUAL)) {
		expression();
		emitBytes(setOp, arg);
	} 
	else{
		emitBytes(getOp, arg);
	}
}
static void variable(bool canAssign) {
	namedVariable(&parser.previous, canAssign);
}
static void grouping(bool canAssign) {
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after expression.");
}
static void unary(bool canAssign) {
	TokenType operatorType = parser.previous.type;
	parsePrecedence(PREC_UNARY);

	switch (operatorType)
	{
	case TOKEN_MINUS:
		emitByte(OP_NEGATE);
		break;
	case TOKEN_BANG:
		emitByte(OP_NOT);
		break;

	}

}

static void binary(bool canAssign) {
	TokenType operatorType = parser.previous.type;
	ParseRule* rule = getRule(operatorType);
	parsePrecedence((Precedence)(rule->precedence + 1));

	switch (operatorType)
	{
	case TOKEN_EQUAL_EQUAL: emitByte(OP_EQUAL); break;
	case TOKEN_BANG_EQUAL:	emitBytes(OP_EQUAL, OP_NOT); break;
	case TOKEN_GREATER:		emitByte(OP_GREATER); break;
	case TOKEN_GREATER_EQUAL: emitBytes(OP_LESS, OP_NOT); break;
	case TOKEN_LESS:		emitByte(OP_LESS); break;
	case TOKEN_LESS_EQUAL:	emitBytes(OP_GREATER, OP_NOT); break;

	case TOKEN_MINUS: emitByte(OP_SUBTRACT); break;
	case TOKEN_PLUS: emitByte(OP_ADD); break;
	case TOKEN_STAR: emitByte(OP_MULTIPLY); break;
	case TOKEN_SLASH: emitByte(OP_DIVIDE); break;
	case TOKEN_PERCENT: emitByte(OP_MOD); break;

	}
}

static void literal(bool canAssign) {
	switch (parser.previous.type)
	{
	case TOKEN_NIL:		emitByte(OP_NIL); break;
	case TOKEN_TRUE:	emitByte(OP_TRUE); break;
	case TOKEN_FALSE:	emitByte(OP_FALSE); break;

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


[TOKEN_BANG] = {unary, NULL, PREC_NONE},
[TOKEN_BANG_EQUAL] = {NULL, binary, PREC_EQUALITY},
[TOKEN_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
[TOKEN_GREATER] = {NULL, binary, PREC_COMPARISON},
[TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
[TOKEN_LESS] = {NULL, binary, PREC_COMPARISON},
[TOKEN_LESS_EQUAL] = {NULL, binary, PREC_COMPARISON},


[TOKEN_IDENTIFIER] = {variable, NULL, PREC_NONE},
[TOKEN_STRING] = {string, NULL, PREC_NONE},
[TOKEN_NUMBER] = {number, NULL, PREC_NONE},


[TOKEN_AND] = {NULL, NULL, PREC_NONE},
[TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
[TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
[TOKEN_FALSE] = {literal, NULL, PREC_NONE},
[TOKEN_FOR] = {NULL, NULL, PREC_NONE},
[TOKEN_FUN] = {NULL, NULL, PREC_NONE},
[TOKEN_IF] = {NULL, NULL, PREC_NONE},
[TOKEN_NIL] = {literal, NULL, PREC_NONE},
[TOKEN_OR] = {NULL, NULL, PREC_NONE},


[TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
[TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
[TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
[TOKEN_THIS] = {NULL, NULL, PREC_NONE},
[TOKEN_TRUE] = {literal, NULL, PREC_NONE},
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
		error("Expect expression (null prefix).");
		return;
	}


	bool canAssign = precedence <= PREC_ASSIGNMENT;
	prefix(canAssign);

	while (precedence <= getRule(parser.current.type)->precedence)
	{
		advance();
		ParseFn infix = getRule(parser.previous.type)->infix;
		infix(canAssign);
	}

	if (!canAssign && match(TOKEN_EQUAL)) {
		error("Invalid assignment target.");
	}
}

bool compile(const char* source, Chunk* chunk)
{
	//Initializations
	initLexer(source);
	parser.hadError = false;
	parser.panicMode = false;
	compillingChunk = chunk;

	Compiler compiler;
	initCompiler(&compiler);

	advance();

	while (!match(TOKEN_EOF))
	{
		declaration();
	}

	consume(TOKEN_EOF, "Expect end of expression.");

	endCompile();
	return !parser.hadError;
}
