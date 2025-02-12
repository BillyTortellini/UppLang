#pragma once

#include "../datastructures/string.hpp"

// Returns true if successfull
bool clipboard_store_text(String string);
bool clipboard_load_text(String* string);