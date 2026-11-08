#pragma once

struct DynString;
struct Identifier;
struct String;
struct Arena;

#include "../datastructures/allocators.hpp"
#include "../datastructures/string.hpp"

// Identifier Pool
struct Identifier_Pool
{
	Arena* arena;
	DynSet<Identifier> identifiers;
};

Identifier_Pool identifier_pool_create(Arena* arena);
Identifier identifier_pool_add(Identifier_Pool* pool, String identifier);
void identifier_pool_to_string(Identifier_Pool* pool, DynString* string);

bool identifier_equals(Identifier a, Identifier b);
bool identifier_equals_pointer(Identifier* a, Identifier* b);
u64 identifier_hash(Identifier a);
u64 identifier_hash_pointer(Identifier* a);
