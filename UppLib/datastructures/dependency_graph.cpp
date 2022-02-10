#include "dependency_graph.hpp"

Dependency_Graph dependency_graph_create()
{
    Dependency_Graph result;
    result.execution_order = dynamic_array_create_empty<int>(1);
    result.nodes = dynamic_array_create_empty<Dependency_Node>(1);
    return result;
}

void dependency_graph_destroy(Dependency_Graph* graph)
{
    dynamic_array_destroy(&graph->execution_order);
    for (int i = 0; i < graph->nodes.size; i++) {
        dynamic_array_destroy(&graph->nodes[i].dependents);
    }
    dynamic_array_destroy(&graph->nodes);
}

int dependency_graph_add_node(Dependency_Graph* graph)
{
    Dependency_Node node;
    node.dependents = dynamic_array_create_empty<int>(1);
    node.finished = false;
    node.open_dependency_count = 0;
    dynamic_array_push_back(&graph->nodes, node);
    return graph->nodes.size - 1;
}

void dependency_graph_add_dependency(Dependency_Graph* graph, int node_index, int dependency_index)
{
    dynamic_array_push_back(&graph->nodes[dependency_index].dependents, node_index);
    graph->nodes[node_index].open_dependency_count += 1;
}

void dependency_node_resolve(Dependency_Graph* graph, Dependency_Node* node, int node_index)
{
    if (node->finished) return;
    if (node->open_dependency_count == 0) 
    {
        node->finished = true;
        dynamic_array_push_back(&graph->execution_order, node_index);
        for (int i = 0; i < node->dependents.size; i++) {
            Dependency_Node* dependent = &graph->nodes[node->dependents[i]];
            dependent->open_dependency_count -= 1;
            dependency_node_resolve(graph, dependent, node->dependents[i]);
        }
    }
}

bool dependency_graph_resolve(Dependency_Graph* graph)
{
    dynamic_array_reset(&graph->execution_order);
    for (int i = 0; i < graph->nodes.size; i++) {
        Dependency_Node* node = &graph->nodes[i];
        dependency_node_resolve(graph, node, i);
    }
    return graph->execution_order.size == graph->nodes.size;
}