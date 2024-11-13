#include "fuzzy_search.hpp"

#include "character_info.hpp"

struct Fuzzy_Searcher {
    Dynamic_Array<Fuzzy_Item> items;
    Dynamic_Array<bool> used_chars;
    String search_term;
    int max_result_count;
};

static bool searcher_initialized = false;
static Fuzzy_Searcher searcher;

void fuzzy_search_start_search(String search_term, int max_result_count)
{
    if (!searcher_initialized) {
        searcher.items = dynamic_array_create<Fuzzy_Item>();
        searcher.used_chars = dynamic_array_create<bool>();
        searcher_initialized = true;
    }

    dynamic_array_reserve(&searcher.items, max_result_count + 1);
    searcher.max_result_count = max_result_count;
    searcher.search_term = search_term;
    dynamic_array_reset(&searcher.items);
}

bool fuzzy_item_compare(Fuzzy_Item& a, Fuzzy_Item& b)
{
    // Check if any character weren't matched
    if (a.matched_character_count != b.matched_character_count) {
        return a.matched_character_count > b.matched_character_count;
    }

    if (a.substring_count != b.substring_count) {
        return a.substring_count < b.substring_count;
    }

    // Now we have same number of substrings
    if (a.substring_order_missmatches != b.substring_order_missmatches) {
        return a.substring_order_missmatches < b.substring_order_missmatches;
    }

    if (a.preamble_match_length != b.preamble_match_length) {
        return a.preamble_match_length > b.preamble_match_length;
    }

    if (a.lower_upper_missmatches != b.lower_upper_missmatches) {
        return a.lower_upper_missmatches < b.lower_upper_missmatches;
    }

    if (a.max_substring_distance != b.max_substring_distance) {
        return a.max_substring_distance < b.max_substring_distance;
    }

    // Otherwise we don't know, so just sort lexically
    return !string_in_order(&a.item_name, &b.item_name);
}

void fuzzy_search_insert_item_ordered(Fuzzy_Item& item)
{
    int insert_pos = 0;
    auto& items = searcher.items;

    // Perf: If we are at the maximum list-size, early-exit if item is lower ranked than the lowest rank so far
    if (items.size == searcher.max_result_count) {
        auto& last = searcher.items[searcher.max_result_count - 1];
        if (fuzzy_item_compare(last, item)) {
            return;
        }
    }

    // Note: Here we could use a binary search to find the correct spot... (But this is probably only usefull for longer lists of items)
    int i = 0;
    for (i = 0; i < items.size; i++) {
        auto& list_item = items[i];
        if (fuzzy_item_compare(item, list_item)) {
            break;
        }
    }
    dynamic_array_insert_ordered(&items, item, i);

    if (items.size > searcher.max_result_count) {
        dynamic_array_rollback_to_size(&items, searcher.max_result_count);
    }
}

void fuzzy_search_add_item(String item_name, int user_index)
{
    if (item_name.size == 0) {
        return;
    }
    String option = item_name;
    String typed = searcher.search_term;

    Fuzzy_Item result;
    result.item_name = item_name;
    result.user_index = user_index;

    result.lower_upper_missmatches = 0;
    result.substring_count = 0;
    result.substring_order_missmatches = 0;
    result.matched_character_count = 0;
    result.preamble_match_length = 0;
    result.max_substring_distance = 0;

    if (typed.size == 0) {
        // If nothing is typed fuzzy search just sorts items lexigraphically.
        fuzzy_search_insert_item_ordered(result);
        return;
    }

    auto& used_chars = searcher.used_chars;
    dynamic_array_reserve(&used_chars, option.size);
    dynamic_array_reset(&used_chars);
    for (int i = 0; i < option.size; i++) {
        dynamic_array_push_back(&used_chars, false);
    }

    int last_sub_start = -1;
    int typed_index = 0;
    while (typed_index < typed.size)
    {
        // Find maximum substring
        int sub_length = 0;
        int sub_distance_to_last = 0;
        int sub_start_index = 0;
        int sub_lower_upper_missmatches = 0;
        for (int start = 0; start < option.size; start++)
        {
            int length = 0;
            int missmatch_count = 0;
            while (start + length < option.size && typed_index + length < typed.size)
            {
                auto char_o = option[start + length];
                auto char_t = typed[typed_index + length];
                bool used = used_chars[start + length];
                if (used) {
                    break;
                }
                if (char_o != char_t) {
                    if (char_get_lowercase(char_o) == char_get_lowercase(char_t)) {
                        missmatch_count += 1;
                    }
                    else {
                        break;
                    }
                }
                length += 1;
            }

            if (length == 0) {
                continue;
            }

            bool set_as_sub = false;
            int distance = start - last_sub_start;
            if (length > sub_length) {
                set_as_sub = true;
            }
            else if (length == sub_length) // Found same substring in 2 places, e.g. sub 'add' in hello_add_something_and_add_twelve
            {
                if (missmatch_count < sub_lower_upper_missmatches) {
                    set_as_sub = true;
                }
                else {
                    if (distance > 0 && sub_distance_to_last < 0) {
                        set_as_sub = true;
                    }
                    else if (distance < 0 && sub_distance_to_last > 0) {
                        set_as_sub = false;
                    }
                    else if (distance < 0 && sub_distance_to_last < 0) {
                        set_as_sub = distance > sub_distance_to_last;
                    }
                    else { // Both larger than 0
                        set_as_sub = distance < sub_distance_to_last;
                    }
                }
            }

            if (set_as_sub) {
                sub_length = length;
                sub_distance_to_last = distance;
                sub_start_index = start;
                sub_lower_upper_missmatches = missmatch_count;
            }
        }

        // Goto next character if substring wasn't found
        if (sub_length == 0) {
            typed_index += 1;
            continue;
        }

        // Flag characters as already used
        for (int i = 0; i < sub_length; i++) {
            used_chars[sub_start_index + i] = true;
        }

        // Add to statistic
        result.matched_character_count += sub_length;
        result.substring_count += 1;
        if (typed_index == 0 && sub_start_index == 0 && sub_lower_upper_missmatches == 0) { // Preamble match only valid if correct lower-upper case
            result.preamble_match_length = sub_length;
        }
        if (sub_start_index <= last_sub_start) {
            result.substring_order_missmatches += 1;
        }
        last_sub_start = sub_start_index;
        typed_index += sub_length;
        result.lower_upper_missmatches += sub_lower_upper_missmatches;
        if (sub_distance_to_last > 0 && sub_distance_to_last > result.max_substring_distance) {
            result.max_substring_distance = sub_distance_to_last;
        }
    }

    fuzzy_search_insert_item_ordered(result);
}

int fuzzy_search_get_item_count() {
    return searcher.items.size;
}

Dynamic_Array<Fuzzy_Item> fuzzy_search_get_results(bool allow_cutoff, int min_cutoff_length)
{
    // Exit if no suggestions are available
    if (searcher.items.size == 0) {
        return searcher.items;
    }

    // Sort by ranking
    if (!allow_cutoff) {
        return searcher.items;
    }

    // Cut off at appropriate point
    int last_cutoff = 1;
    auto& last_sug = searcher.items[0];
    const int MIN_CUTTOFF_VALUE = 3;
    bool valid_cutoff = false;
    for (int i = 1; i < searcher.items.size; i++)
    {
        auto& sug = searcher.items[i];
        if (last_sug.matched_character_count != sug.matched_character_count) valid_cutoff = true;
        if (last_sug.substring_count != sug.substring_count) valid_cutoff = true;
        // if (last_sug.substring_order_missmatches != sug.substring_order_missmatches) valid_cutoff = true;
        // if (last_sug.preamble_match_length != sug.preamble_match_length) valid_cutoff = true;
        // if (last_sug.lower_upper_missmatches != sug.lower_upper_missmatches) valid_cutoff = true;

        if (valid_cutoff && i >= min_cutoff_length) {
            dynamic_array_rollback_to_size(&searcher.items, i);
            return searcher.items;
        }
    }

    return searcher.items;
}