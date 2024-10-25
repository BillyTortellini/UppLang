#pragma once

#include "../datastructures/string.hpp"
#include "../datastructures/dynamic_array.hpp"

struct Fuzzy_Item
{
    String item_name;
    int user_index;

    // Ranking metrics
    int matched_character_count;
    int lower_upper_missmatches;
    int substring_count;
    int substring_order_missmatches;
    int preamble_match_length; // e.g. if the first substring is the start
    int max_substring_distance; // Distance between substrings, e.g. search "add_foo" ranks "add_2foo" higher than "add_something_foo"
};

void fuzzy_search_start_search(String search_term, int max_result_count);
void fuzzy_search_add_item(String item_name, int user_index = 0);
int fuzzy_search_get_item_count();
// If cutoff is set, matches will be cut-off if they differ too much
Dynamic_Array<Fuzzy_Item> fuzzy_search_get_results(bool cutoff_between_large_match_differences, int min_cutoff_length);