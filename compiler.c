#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

typedef enum {
	TYPE_SCRIPT,
	TYPE_FUNCTION
} FunctionType;

typedef struct {
	struct Compiler* enclosing;

	ObjFunction* function;
	FunctionType type;

	Local locals[UINT8_COUNT];
	int localCount;
	int scopeDepth;
} Compiler;

Compiler* current = NULL;

Chunk* compillingChunk;
static Chunk* currentChunk() {
	return &current->function->chunk;
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
		//log("q");
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
	emitByte(OP_NIL);
	emitByte(OP_RETURN);
}

static ObjFunction* endCompile() {
	emitReturn();
	ObjFunction* function = current->function;
#ifdef DEBUG_TRACE_EXECUTION
	if (!parser.hadError) {
		disassembleChunk(currentChunk(),
			function->name != NULL ? function->name->chars: "<script>");
	}
#endif // DEBUG_TRACE_EXECUTION

	current = current->enclosing;
	return function;
}

static uint8_t makeConstant(Value value) {
	int constant = addConstant(currentChunk(), value);
	if (constant >= UINT8_MAX) {
		error("Too many constants in one chunk.");
		return 0;
	}
	return (uint8_t)constant;
}

static void initCompiler(Compiler* compiler, FunctionType type) {
	compiler->enclosing = current;
	compiler->type = type;
	compiler->function = NULL;

	compiler->localCount = 0;
	compiler->scopeDepth = 0;

	compiler->function = newFunction();

	if (type != TYPE_SCRIPT) {
		compiler->function->name = copyString(parser.previous.lexemeStart, parser.previous.length);
	}

	current = compiler;

	Local* local = &current->locals[current->localCount++];
	local->depth = 0;
	local->name.lexemeStart = "";
	local->name.length = 0;
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
			if (compiler->locals[i].depth == -1) {
				error("Can't read local variable in its own initializer.");
			}
			return i;
		}
	}
	return -1;
}

static void addLocal(Token* name) {
	if (current->localCount == UINT8_COUNT) {
		error("Too many local variables in function.");
		return;
	}
	Local* local = &current->locals[current->localCount++];
	local->name = *name;
	local->depth = -1;
	//local->depth = current->scopeDepth;
}

//For locals only
static void declareVariable() {
	if (current->scopeDepth == 0)	return;

	Token* name = &parser.previous;
	for (int i = current->localCount - 1;i >= 0;i--) {
		Local* local = &current->locals[i];
		if (local->depth != -1 && local->depth < current->scopeDepth) {
			break;
		}

		if (identifiersEqual(name, &local->name)) {
			error("Already a variable with this name in this scope.");
		}
	}
	addLocal(name);
}

static uint8_t parseVariable(char* message) {
	consume(TOKEN_IDENTIFIER, message);

	declareVariable();
	if (current->scopeDepth > 0)	return;

	return identifierConstant(&parser.previous);
}

static void markInitialized() {
	if (current->scopeDepth == 0)	return;
	current->locals[current->localCount - 1].depth = current->scopeDepth;
}

static void defineVariable(uint8_t global) {
	if (current->scopeDepth > 0) {
		markInitialized();
		return;
	}
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

static void function(FunctionType type) {
	Compiler compiler;
	initCompiler(&compiler, type);
	beginScope();

	consume(TOKEN_LEFT_PAREN, "Expect '(' after function name.");

	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			current->function->arity++;
			if (current->function->arity > 255) {
				errorAtCurrent("Can't have more than 255 parameters.");
			}
			uint8_t parameter = parseVariable("Expect parameter name.");
			defineVariable(parameter);

			if (match(TOKEN_EQUAL)) {
				expression();
				emitBytes(OP_SET_DEFAULT, current->function->defaults);

				current->function->defaults++;
				break;
			}

		} while (match(TOKEN_COMMA));

		//Define (but do not assign) defaults
		if (match(TOKEN_COMMA)) {
			do {
				current->function->arity++;
				if (current->function->arity > 255) {
					errorAtCurrent("Can't have more than 255 parameters.");
				}
				uint8_t parameter = parseVariable("Expect parameter name.");
				defineVariable(parameter);

				consume(TOKEN_EQUAL, "Default parameters must be at the end.");

				expression();
				emitBytes(OP_SET_DEFAULT, current->function->defaults);

				current->function->defaults++;
			} while (match(TOKEN_COMMA));
		}
	}

	consume(TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
	consume(TOKEN_LEFT_BRACE, "Expect '{' before function body.");

	block();

	ObjFunction* function = endCompile();
	emitBytes(OP_CONSTANT, makeConstant(OBJ_VAL(function)));
}

static void functionDeclaration() {
	uint8_t global = parseVariable("Expect function name.");
	markInitialized();
	function(TYPE_FUNCTION);
	defineVariable(global);
}

static uint8_t argumentList() {
	uint8_t argCount = 0;

	if (!check(TOKEN_RIGHT_PAREN)) {
		do {
			argCount++;
			if (argCount == 255) {
				error("Can't have more than 255 arguments.");
			}
			expression();
		} while (match(TOKEN_COMMA));
	}
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
	return argCount;
}

static void call(bool canAssign) {
	uint8_t args = argumentList();
	emitBytes(OP_CALL, args);
}

static void returnStatement() {
	if (current->type == TYPE_SCRIPT) {
		error("Can't return from top-level code.");
	}
	if (match(TOKEN_SEMICOLON)) {
		emitReturn();
	}
	else{
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after return value.");
		emitByte(OP_RETURN);
	}
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

//Returns position of the offset
static int emitJump(uint8_t instruction) {
	emitByte(instruction);
	emitByte(0xff);
	emitByte(0xff);
	return currentChunk()->count - 2;
}

static void patchJump(int offset) {
	//Jump amount
	int jump = currentChunk()->count - 2 - offset;

	if (jump > UINT16_MAX) {
		error("Too much code to jump over.");
	}

	currentChunk()->code[offset] = (jump >> 8) & 0xff;
	currentChunk()->code[offset + 1] = jump & 0xff;
}

static void ifStatement() {
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after if condition.");

	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	declaration();
	
	int elseJump = emitJump(OP_JUMP);
	patchJump(thenJump);
	emitByte(OP_POP);

	if (match(TOKEN_ELSE))	declaration();

	patchJump(elseJump);
}

static void emitLoop(int loopStart) {
	emitByte(OP_LOOP);

	int offset = currentChunk()->count + 2 - loopStart;
	if (offset > UINT16_MAX) {
		error("Loop body too large.");
	}

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

static void whileStatement() {
	int loopStart = currentChunk()->count;
	consume(TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
	expression();
	consume(TOKEN_RIGHT_PAREN, "Expect ')' after while condition.");

	int thenJump = emitJump(OP_JUMP_IF_FALSE);
	emitByte(OP_POP);
	declaration();

	emitLoop(loopStart);
	patchJump(thenJump);
	emitByte(OP_POP);
}

static void forStatement() {
	beginScope();
	consume(TOKEN_LEFT_PAREN, "Expect '(' after for.");

	if (match(TOKEN_SEMICOLON)) {
		//No initializer
	}
	else if (match(TOKEN_VAR)) {
		varDeclaration();
	}
	else {
		expressionStatement();
	}

	int loopStart = currentChunk()->count;
	int exitJump = -1;
	if (!match(TOKEN_SEMICOLON)) {
		expression();
		consume(TOKEN_SEMICOLON, "Expect ';' after loop condition.");

		exitJump = emitJump(OP_JUMP_IF_FALSE);
		emitByte(OP_POP);
	}

	if (!match(TOKEN_RIGHT_PAREN)) {
		int bodyJump = emitJump(OP_JUMP);
		int incrementStart = currentChunk()->count;

		expression();
		emitByte(OP_POP);
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");

		emitLoop(loopStart);
		loopStart = incrementStart;
		patchJump(bodyJump);
	}
	else
	{
		consume(TOKEN_RIGHT_PAREN, "Expect ')' after for clauses.");
	}

	statement();

	emitLoop(loopStart);
	if (exitJump != -1) {
		patchJump(exitJump);
		emitByte(OP_POP);
	}

	endScope();
}

static void statement() {
	if (match(TOKEN_PRINT))
		printStatement();
	else if (match(TOKEN_IF))
		ifStatement();
	else if (match(TOKEN_WHILE))
		whileStatement();
	else if (match(TOKEN_FOR))
		forStatement();
	else if (match(TOKEN_RETURN))
		returnStatement();
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
	else if (match(TOKEN_FUN))
		functionDeclaration();
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
	else if (canAssign && 
		(match(TOKEN_PLUS_EQUAL) || match(TOKEN_MINUS_MINUS) || match(TOKEN_STAR_EQUAL) || match(TOKEN_SLASH_EQUAL)
			|| match(TOKEN_PERCENT_EQUAL))) {

		uint8_t op;
		switch (parser.previous.type)
		{
		case TOKEN_PLUS_EQUAL: op = OP_ADD; break;
		case TOKEN_MINUS_EQUAL: op = OP_SUBTRACT; break;
		case TOKEN_STAR_EQUAL: op = OP_MULTIPLY; break;
		case TOKEN_SLASH_EQUAL: op = OP_DIVIDE; break;
		case TOKEN_PERCENT_EQUAL: op = OP_MOD; break;

		default: op = 0; break; //Unreachable
		}
		emitBytes(getOp, arg);
		expression();

		emitByte(op);
		emitBytes(setOp, arg);
	}
	else if (canAssign && (match(TOKEN_PLUS_PLUS) || match(TOKEN_MINUS_MINUS)) ) {
		emitBytes(getOp, arg);
		int sign = parser.previous.type == TOKEN_PLUS_PLUS ? 1 : -1;
		emitBytes(OP_CONSTANT, makeConstant(NUMBER_VAL(sign)));
		emitByte(OP_ADD);
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

static void or_(bool canAssign) {
	int elseJump = emitJump(OP_JUMP_IF_FALSE);
	int endJump = emitJump(OP_JUMP);

	patchJump(elseJump);
	emitByte(OP_POP);

	parsePrecedence(PREC_OR);

	patchJump(endJump);
}
static void and_(bool canAssign) {
	int endJump = emitJump(OP_JUMP_IF_FALSE);

	emitByte(OP_POP);
	parsePrecedence(PREC_AND);

	patchJump(endJump);
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
[TOKEN_LEFT_PAREN] = {grouping, call, PREC_CALL},
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


[TOKEN_AND] = {NULL, and_, PREC_AND},
[TOKEN_CLASS] = {NULL, NULL, PREC_NONE},
[TOKEN_ELSE] = {NULL, NULL, PREC_NONE},
[TOKEN_FALSE] = {literal, NULL, PREC_NONE},
[TOKEN_FOR] = {NULL, NULL, PREC_NONE},
[TOKEN_FUN] = {NULL, NULL, PREC_NONE},
[TOKEN_IF] = {NULL, NULL, PREC_NONE},
[TOKEN_NIL] = {literal, NULL, PREC_NONE},
[TOKEN_OR] = {NULL, or_, PREC_OR},


[TOKEN_PRINT] = {NULL, NULL, PREC_NONE},
[TOKEN_RETURN] = {NULL, NULL, PREC_NONE},
[TOKEN_SUPER] = {NULL, NULL, PREC_NONE},
[TOKEN_THIS] = {NULL, NULL, PREC_NONE},
[TOKEN_TRUE] = {literal, NULL, PREC_NONE},
[TOKEN_VAR] = {NULL, NULL, PREC_NONE},
[TOKEN_WHILE] = {NULL, NULL, PREC_NONE},
[TOKEN_ERROR] = {NULL, NULL, PREC_NONE},
[TOKEN_EOF] = {NULL, NULL, PREC_NONE},

[TOKEN_PERCENT] = {NULL, binary, PREC_FACTOR},
[TOKEN_PLUS_PLUS] = {NULL, NULL, PREC_NONE},
[TOKEN_MINUS_MINUS] = {NULL, NULL, PREC_NONE},
[TOKEN_PLUS_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_MINUS_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_STAR_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_SLASH_EQUAL] = {NULL, NULL, PREC_NONE},
[TOKEN_PERCENT_EQUAL] = {NULL, NULL, PREC_NONE},
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

ObjFunction* compile(const char* source)
{
	//Initializations
	initLexer(source);
	parser.hadError = false;
	parser.panicMode = false;

	Compiler compiler;
	initCompiler(&compiler, TYPE_SCRIPT);

	advance();

	while (!match(TOKEN_EOF))
	{
		declaration();
	}

	consume(TOKEN_EOF, "Expect end of expression.");

	ObjFunction* function = endCompile();
	return parser.hadError ? NULL : function;
}
