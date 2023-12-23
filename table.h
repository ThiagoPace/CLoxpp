#ifndef table_h
#define table_h

#include "common.h"
#include "value.h"

typedef struct {
	ObjString* key;
	Value value;
} Entry;

typedef struct {
	int count;
	int capacity;

	Entry* entries;
} Table;


void initTable(Table* table);
void freeTable(Table* table);
ObjString* findTableString(Table* table, char* chars, int length, uint32_t hash);
bool tableSet(Table* table, ObjString* key, Value value);
bool tableDelete(Table* table, ObjString* key);
void tableAddAll(Table* from, Table* to);
bool tableGet(Table* table, ObjString* key, Value* outValue);

//GC
void markTable(Table* table);
void tableRemoveWhite(Table* table);

#endif