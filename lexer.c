#include <stdio.h>
#include<string.h>
#include "debug.h"
#include "common.h"
#include "lexer.h"

typedef struct {
	char* start;
	char* current;
	int line;
} Lexer;

Lexer lexer;

static bool isAtEnd() {
	return *lexer.current == '\0';
}
static char advance() {
	lexer.current++;
	return lexer.current[-1];
}
static char peek() {
	return *lexer.current;
}
static char peekPrevious() {
	return lexer.current[-1];
}
static char peekNext() {
	if (isAtEnd())	return '\0';
	return lexer.current[1];
}
static bool match(char c) {
	if (isAtEnd()) return false;
	if (*lexer.current != c) return false;
	lexer.current++;
	return true;
}

static Token makeToken(TokenType type) {
	Token token;
	token.type = type;
	token.lexemeStart = lexer.start;
	token.length = (int)(lexer.current - lexer.start);
	token.line = lexer.line;
	return token;
}
static Token errorToken(const char* message) {
	Token token;
	token.type = TOKEN_ERROR;
	token.lexemeStart = message;
	token.length = (int)strlen(message);
	token.line = lexer.line;
	return token;
}

static void skipWhitespace() {
	for (;;) {
		char c = peek();
		switch (c)
		{
		case ' ':
		case '\r':
		case '\t':
			advance();
			break;

		case '\n':
			lexer.line++;
			advance();
			break;

		case '/':
			if (peekNext() == '/') {
				while (peek() != '\n' && !isAtEnd()) {
					advance();
				}
			}
			else if (peekNext() == '*') {
				advance();
				advance();
				while (peek() != '*' && !isAtEnd()) {
					if (peek() == '\n')	lexer.line++;
					advance();
				}
				advance();
				if (isAtEnd) {
					//ERROR
				}
				if (peek() == '/') {
					advance();
				}
				else {
					//ERROR
				}
			}
			else {
				return;
			}
			break;

		default:
			return;
		}
	}
}
static Token string() {
	while (peek() != '"')
	{
		if (isAtEnd()) {
			return errorToken("Unterminated string.");
		}
		if (peek() == '\n')
			lexer.line++;

		advance();
	}

	//Consume closing "
	advance();
	return makeToken(TOKEN_STRING);
}

static bool isDigit(char c) {
	return c >= '0' && c <= '9';
}
static bool isAlpha(char c) {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == "_";
}

static Token number() {
	while (isDigit(peek())){
		advance();
	}
	if (peek() == '.' && isDigit(peekNext())) {
		advance();
		while (isDigit(peek())) {
			advance();
		}
	}
	return makeToken(TOKEN_NUMBER);
}

static TokenType checkKeyword(int start, int length, char* rest, TokenType type) {
	//IPA
	//ERRO: true
	if (lexer.current - lexer.start == start + length
		&& memcmp(lexer.start + start, rest, length) == 0) {
		return type;
	}
	return TOKEN_IDENTIFIER;
}
static TokenType identifierType() {
	switch (lexer.start[0])
	{
	case 'a': return checkKeyword(1, 2, "nd", TOKEN_AND);
	case 'c': return checkKeyword(1, 4, "lass", TOKEN_CLASS);
	case 'e': return checkKeyword(1, 3, "lse", TOKEN_ELSE);
	case 'i': return checkKeyword(1, 1, "f", TOKEN_IF);
	case 'n': return checkKeyword(1, 2, "il", TOKEN_NIL);
	case 'o': return checkKeyword(1, 1, "r", TOKEN_OR);
	case 'p': return checkKeyword(1, 4, "rint", TOKEN_PRINT);
	case 'r': return checkKeyword(1, 5, "eturn", TOKEN_RETURN);
	case 's': return checkKeyword(1, 4, "uper", TOKEN_SUPER);
	case 'v': return checkKeyword(1, 2, "ar", TOKEN_VAR);
	case 'w': return checkKeyword(1, 4, "hile", TOKEN_WHILE);

	case 'f':
		if (lexer.current - lexer.start > 1) {
			switch (lexer.start[1])
			{
			case 'a': return checkKeyword(2, 3, "lse", TOKEN_FALSE);
			case 'o': return checkKeyword(2, 1, "r", TOKEN_FOR);
			case 'u': return checkKeyword(2, 1, "n", TOKEN_FUN);
			}
		}
		break;

	case 't':
		if (lexer.current - lexer.start > 1) {
			switch (lexer.start[1])
			{
			case 'h': return checkKeyword(2, 2, "is", TOKEN_THIS);
			case 'r': return checkKeyword(2, 2, "ue", TOKEN_TRUE);
			}
		}
		break;

	default:
		break;
	}

	return TOKEN_IDENTIFIER;
}
static Token identifier() {
	while (isAlpha(peek()) || isDigit(peek()))
	{
		advance();
	}
	return makeToken(identifierType());
}

void initLexer(char* source)
{
	lexer.start = source;
	lexer.current = source;
	lexer.line = 1;
}

Token lexToken() {
	skipWhitespace();
	lexer.start = lexer.current;
	if (isAtEnd()) {
		log("END");
		return makeToken(TOKEN_EOF);
	}

	char c = advance();

	if (isDigit(c)) {
		return number();
	}
	if (isAlpha(c)) {
		return identifier();
	}
	switch (c)
	{
	case '(': return makeToken(TOKEN_LEFT_PAREN);
	case ')': return makeToken(TOKEN_RIGHT_PAREN);
	case '{': return makeToken(TOKEN_LEFT_BRACE);
	case '}': return makeToken(TOKEN_RIGHT_BRACE);
	case ';': return makeToken(TOKEN_SEMICOLON);
	case ',': return makeToken(TOKEN_COMMA);
	case '.': return makeToken(TOKEN_DOT);
	case '+':
		if (match('='))
			return makeToken(TOKEN_PLUS_EQUAL);
		else
			return makeToken(match('+') ? TOKEN_PLUS_PLUS : TOKEN_PLUS);
	case '-':
		if (match('='))
			return makeToken(TOKEN_MINUS_EQUAL);
		else
			return makeToken(match('-') ? TOKEN_MINUS_MINUS : TOKEN_MINUS);
		
	case '*':
		return makeToken(match('=') ? TOKEN_STAR_EQUAL : TOKEN_STAR);

	case '%': return makeToken(match('=') ? TOKEN_PERCENT_EQUAL : TOKEN_PERCENT);

	//Dependant binaries
	case '!':
		return makeToken(
			match('=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
	case '=':
		return makeToken(
			match('=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
	case '<':
		return makeToken(
			match('=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
	case '>':
		return makeToken(
			match('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);


	//Long binaries
	case '/':
		return makeToken(match('=') ? TOKEN_SLASH_EQUAL : TOKEN_SLASH);
		break;

	//Strings
	case '"': return string();

	//Special characters

	default:
		printf(c);
		return errorToken("Unexpected character.");
	}
}