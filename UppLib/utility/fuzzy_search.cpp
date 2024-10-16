#include "fuzzy_search.hpp"

struct Fuzzy_Searcher {
    Dynamic_Array<Fuzzy_Item> items;
    String search_term;
};

static bool searcher_initialized = false;
static Fuzzy_Searcher searcher;

void fuzzy_search_start_search(String search_term)
{
    if (!searcher_initialized) {
        searcher.items = dynamic_array_create<Fuzzy_Item>();
        searcher_initialized = true;
    }

    searcher.search_term = search_term;
    dynamic_array_reset(&searcher.items);
}

bool fuzzy_item_compare(Fuzzy_Item& a, Fuzzy_Item& b)
{
    if (a.is_longer != b.is_longer) {
        return b.is_longer;
    }
    if (a.all_characters_contained != b.all_characters_contained) {
        return a.all_characters_contained;
    }
    if (a.preamble_length != b.preamble_length) {
        return a.preamble_length > b.preamble_length;
    }
    if (a.substring_count != b.substring_count) {
        return a.substring_count < b.substring_count;
    }
    if (a.substrings_in_order != b.substrings_in_order) {
        return a.substrings_in_order;
    }
    return string_in_order(&a.item_name, &b.item_name);
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
    result.is_longer = typed.size > option.size;
    result.substring_count = 0;
    result.substrings_in_order = true;
    result.preamble_length = 0;
    result.all_characters_contained = true;
    if (typed.size == 0) {
        dynamic_array_push_back(&searcher.items, result);
        return;
    }

    int last_sub_start = -1;
    int typed_index = 0;
    while (typed_index < typed.size)
    {
        // Find maximum substring
        int max_length = 0;
        int max_start_index = 0;
        for (int start = 0; start < option.size; start++)
        {
            int length = 0;
            while (start + length < option.size && typed_index + length < typed.size)
            {
                auto char_o = option[start + length];
                auto char_t = typed[typed_index + length];
                if (char_o != char_t) {
                    break;
                }
                length += 1;
            }
            if (length > max_length) {
                max_length = length;
                max_start_index = start;
            }
        }

        // Add to statistic
        result.substring_count += 1;
        if (max_start_index == 0 && typed_index == 0) {
            result.preamble_length = max_length;
        }
        if (max_length == 0) {
            result.all_characters_contained = false;
            typed_index += 1;
            continue;
        }
        if (max_start_index <= last_sub_start) {
            result.substrings_in_order = false;
        }
        last_sub_start = max_start_index;
        typed_index += max_length;
    }

    dynamic_array_push_back(&searcher.items, result);
    return;
}

int fuzzy_search_get_item_count() {
    return searcher.items.size;
}

Dynamic_Array<Fuzzy_Item> fuzzy_search_rank_results(bool allow_cutoff, int min_cutoff_length)
{
    // Exit if no suggestions are available
    if (searcher.items.size == 0) {
        return searcher.items;
    }

    // Sort by ranking
    dynamic_array_sort(&searcher.items, fuzzy_item_compare);

    if (!allow_cutoff) {
        return searcher.items;
    }

    // Cut off at appropriate point
    int last_cutoff = 1;
    auto& last_sug = searcher.items[0];
    const int MIN_CUTTOFF_VALUE = 3;
    for (int i = 1; i < searcher.items.size; i++)
    {
        auto& sug = searcher.items[i];
        bool valid_cutoff = false;
        if (last_sug.is_longer != sug.is_longer) valid_cutoff = true;
        if (last_sug.substrings_in_order != sug.substrings_in_order) valid_cutoff = true;
        if (last_sug.preamble_length != sug.preamble_length) valid_cutoff = true;
        if (last_sug.all_characters_contained != sug.all_characters_contained) valid_cutoff = true;
        if (last_sug.substring_count != sug.substring_count) valid_cutoff = true;
        if (valid_cutoff && i >= min_cutoff_length) {
            dynamic_array_rollback_to_size(&searcher.items, i);
            return searcher.items;
        }
    }

    return searcher.items;
}