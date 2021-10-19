#pragma once

template<typename T>
struct List_Node
{
    T value;
    List_Node* next;
    List_Node* prev;
};

template<typename T>
struct List
{
    List_Node<T>* head;
    List_Node<T>* tail;
    int count;
};

template<typename T>
List<T> list_create() {
    List<T> result;
    result.count = 0;
    result.head = 0;
    result.tail = 0;
    return result;
}

template<typename T>
void list_destroy(List<T>* list) 
{
    List_Node<T>* start = list->head;
    while (start != 0) {
        List_Node<T>* next = start->next;
        delete start;
        start = next;
    }
}

template<typename T>
void list_remove_node(List<T>* list, List_Node<T>* node) 
{
    if (node == 0) return;
    list->count--;
    if (node->next != 0) {
        node->next->prev = node->prev;
    }
    else {
        list->tail = node->prev;
    }
    if (node->prev != 0) {
        node->prev->next = node->next;
    }
    else {
        list->head = node->next;
    }
    delete node;
}

template<typename T>
void list_remove_node_item(List<T>* list, T* item) {
    list_remove_node(list, (List_Node<T>*) item);
}

template<typename T>
List_Node<T>* list_add_at_end(List<T>* list, T value) 
{
    list->count++;
    List_Node<T>* node = new List_Node<T>;
    node->value = value;
    if (list->tail == 0) 
    {
        list->head = node;
        list->tail = node;
        node->next = 0;
        node->prev = 0;
        return node;
    }

    list->tail->next = node;
    node->prev = list->tail;
    node->next = 0;
    list->tail = node;
    return node;
}

template<typename T>
List_Node<T>* list_add_at_start(List<T>* list, T value) 
{
    list->count++;
    List_Node<T>* node = new List_Node<T>;
    node->value = value;
    if (list->head == 0) 
    {
        list->head = node;
        list->tail = node;
        node->next = 0;
        node->prev = 0;
        return node;
    }

    list->head->prev = node;
    node->next = list->head;
    node->prev = 0;
    list->head = node;

    return node;
}

