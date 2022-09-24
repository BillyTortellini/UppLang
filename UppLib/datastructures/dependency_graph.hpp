#pragma once

#include "dynamic_array.hpp"

struct Dependency_Node
{
    bool finished;
    int open_dependency_count;
    Dynamic_Array<int> dependents;
};

struct Dependency_Graph
{
    Dynamic_Array<Dependency_Node> items;
    Dynamic_Array<int> execution_order;
};

Dependency_Graph dependency_graph_create();
void dependency_graph_destroy(Dependency_Graph* graph);
int dependency_graph_add_node(Dependency_Graph* graph);
void dependency_graph_add_dependency(Dependency_Graph* graph, int node_index, int dependency_index);
bool dependency_graph_resolve(Dependency_Graph* graph);


