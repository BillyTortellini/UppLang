#pragma once

#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"

struct Fuzzy_Item
{
    String item_name;
    int user_index;

    // Ranking metrics
    int preamble_length;
    bool is_longer;
    bool substrings_in_order;
    bool all_characters_contained;
    int substring_count;
};

void fuzzy_search_start_search(String search_term);
void fuzzy_search_add_item(String item_name, int user_index = 0);
int fuzzy_search_get_item_count();
Dynamic_Array<Fuzzy_Item> fuzzy_search_rank_results(bool allow_cutoff, int min_cutoff_length);