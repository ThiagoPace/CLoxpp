#include <string.h>
#include "table.h"
#include "memory.h"


#define TABLE_MAX_LOAD 0.75

void initTable(Table* table)
{
	table->capacity = 0;
	table->count = 0;
	table->entries = NULL;
}

void freeTable(Table* table)
{
	FREE_ARRAY(Entry, table->entries, table->capacity);
	initTable(table);
}

//Method used for internalizing all strings. Used for copy and taking methods in object.c
ObjString* findTableString(Table* table, char* chars, int length, uint32_t hash)
{
	if (table->count == 0)	return NULL;

	uint32_t index = hash % table->capacity;
	for (;;) {
		Entry* entry = &table->entries[index];
		if (entry->key == NULL) {
			if (IS_NIL(entry->value))	return NULL;
		}
		else if (entry->key->length == length && entry->key->hash == hash
			&& memcmp(chars, entry->key->chars, length) == 0) {
			return entry->key;
		}
		index = (index + 1) % table->capacity;
	}
}

static Entry* findEntry(Entry* entries, int capacity, ObjString* key) {
	uint32_t index = key->hash % capacity;
	Entry* tombstone = NULL;
	for (;;) {
		Entry* entry = &entries[index];
		if (entry->key == NULL) {
			if (IS_NIL(entry->value)) {
				return tombstone != NULL ? tombstone : entry;
			}
			else
			{
				if (tombstone == NULL)	tombstone = entry;
			}
		}
		else if (entry->key == key) {
			return entry;
		}
		index = (index + 1) % capacity;
	}
}

static void adjustCapacity(Table* table, int capacity) {
	Entry* entries = ALLOCATE(Entry, capacity);
	for (int i = 0;i < capacity;i++) {
		entries[i].key = NULL;
		entries[i].value = NIL_VAL;
	}


	table->count = 0;
	for (int i = 0;i < table->capacity;i++) {
		Entry* entry = &table->entries[i];
		//Ignore tombstones as well
		if (entry->key == NULL)	continue;

		Entry* dest = findEntry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	FREE_ARRAY(Entry, table->entries, table->capacity);
	table->capacity = capacity;
	table->entries = entries;
}

//IPA: pointers para encontrar elementos num array
bool tableSet(Table* table, ObjString* key, Value value) {
	//If neccessary, allocation. For efficiency, we don't let the table be too loaded.
	if (table->count >= table->capacity * TABLE_MAX_LOAD) {
		int capacity = GROW_CAPACITY(table->capacity);
		adjustCapacity(table, capacity);
	}

	//Finding bucket and setting kv pair
	Entry* entry = findEntry(table->entries, table->capacity, key);
	bool isNew = entry->key == NULL;
	if (isNew && IS_NIL(entry->value))	table->count++;

	entry->key = key;
	entry->value = value;
	return isNew;
}

bool tableDelete(Table* table, ObjString* key)
{
	if (table->count == 0)	return false;

	Entry* entry = findEntry(table->entries, table->capacity, key);
	if (entry->key == NULL)	return false;

	entry->key = NULL;
	entry->value = BOOL_VAL(true);

	return true;
}

void tableAddAll(Table* from, Table* to) {
	for (int i = 0;i < from->capacity;i++) {
		Entry* entry = &from->entries[i];
		if (entry->key != NULL)
			tableSet(to, entry->key, entry->value);
	}
}

bool tableGet(Table* table, ObjString* key, Value* outValue)
{
	if (table->count == 0)	return false;
	Entry* entry = findEntry(table->entries, table->capacity, key);
	//Return false if not contained in table
	if (entry->key == NULL)	return false;
	*outValue = entry->value;
	return true;
}

void markTable(Table* table)
{
	for (Entry* entry = table->entries; entry < table->count; entry++) {
		markObj(entry->key);
		markValue(entry->value);
	}
}

void tableRemoveWhite(Table* table)
{
	for (int i = 0;i < table->capacity;i++) {
		Entry* entry = &table->entries[i];
		if (entry->key != NULL && !entry->key->obj.gcMarked) {
			tableDelete(table, entry->key);
		}
	}
}
