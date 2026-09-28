#pragma once

#include "../datastructures/string.hpp"

// Returns true if successfull
bool clipboard_store_text(DynString string);
bool clipboard_load_text(DynString* string);