#include "identifier_pool.hpp"

// Identifier Pool
bool Identifier::equals(Identifier other) {
	return string == other.string;
}

bool Identifier::operator==(Identifier other) {
	return other.string == string;
}
bool Identifier::operator!=(Identifier other) {
	return !(*this == other);
}

String Identifier::to_string() {
	return *this->string;
}

Identifier_Pool identifier_pool_create(Arena* arena)
{
	Identifier_Pool result;
	result.arena = arena;
	result.identifiers = DynSet<Identifier>::create(arena, identifier_hash_pointer, identifier_equals_pointer);

	return result;
}

Identifier identifier_pool_add(Identifier_Pool* pool, String string)
{
	Identifier query_id;
	query_id.string = &string;
	Identifier* result = pool->identifiers.find(query_id);
	if (result != nullptr) {
		return *result;
	}

	// Otherwise create new identifier and return it
	Identifier identifier;
	identifier.string = pool->arena->allocate<String>();
	*identifier.string = string_allocate_copy(string, pool->arena->upcast());
	pool->identifiers.insert(identifier);

	return identifier;
}

Order_Comparison_Result identifier_sort_fn(Identifier** app, Identifier** bpp, void* userdata)
{
	Identifier* a = *app;
	Identifier* b = *bpp;
	return string_compare(*a->string, *b->string);
}

void identifier_pool_to_string(Identifier_Pool* pool, DynString* string)
{
	SCRATCH_ARENA_MAKE_SCOPED(pool->arena);

	string->append("Identifiers: ");
	Array<Identifier*> values = pool->identifiers.copy_to_array(scratch_arena);
	array_sort(values, identifier_sort_fn, nullptr);
	for (int i = 0; i < values.size; i++) {
		string->append("\n    ")->append(values[i]->string);
	}
	string->append("\n");
}

bool identifier_equals(Identifier a, Identifier b) {
	return a.string == b.string;
}

bool identifier_equals_pointer(Identifier* a, Identifier* b) {
	return a->string == b->string;
}

u64 identifier_hash(Identifier a) {
	return hash_string(*a.string);
}

u64 identifier_hash_pointer(Identifier* a) {
	return hash_string(*a->string);
}
