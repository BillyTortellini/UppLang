#include "incremental_parser.hpp"

#include "../../math/scalars.hpp"

namespace NEW_AST
{
	AST AST::create(Arena* arena)
	{
		AST result;
		result.arena = arena;
		result.buffer = nullptr;
		result.size = 0;
		result.capacity = 0;
		return result;
	}

	enum class Node_Property
	{
		RANGE,
		BOUNDING_RANGE,
		NODE_INFO,
		PARENT_INDEX,
		CHILD_START_INDEX,
		CHILD_COUNT,
		CHILD_INDEX,
		NODE_TYPE,

		MAX_ENUM_VALUE
	};

	struct Node_Property_Info
	{
		int size;
		int alignment;
		int offset;
	};

	struct Node_Infos
	{
		Node_Property_Info property_infos[(int)Node_Property::MAX_ENUM_VALUE];
		int node_size;
	};

	Node_Infos node_infos_make()
	{
		Node_Infos result;
		result.node_size = 0;

		for (int i = 0; i < (int)Node_Property::MAX_ENUM_VALUE; i++)
		{
			Node_Property_Info& info = result.property_infos[i];
			info.size = 0;
			info.alignment = 0;
			info.offset = result.node_size;
			Node_Property property = (Node_Property)i;
			switch (property)
			{
			case Node_Property::RANGE: 
			case Node_Property::BOUNDING_RANGE: 
				info.size = sizeof(Text_Range); info.alignment = alignof(Text_Range); break;
			case Node_Property::NODE_INFO:
				info.size = sizeof(Node_Info); info.alignment = alignof(Node_Info); break;
			case Node_Property::PARENT_INDEX:
			case Node_Property::CHILD_START_INDEX:
			case Node_Property::CHILD_INDEX:
				info.size = sizeof(int); info.alignment = alignof(int); break;
			case Node_Property::CHILD_COUNT:
				info.size = sizeof(u16); info.alignment = alignof(u16); break;
			case Node_Property::NODE_TYPE:
				info.size = sizeof(Node_Type); info.alignment = alignof(Node_Type); break;
			default: panic("");
			}

			result.node_size = math_round_next_multiple(result.node_size, info.alignment);
			info.offset = result.node_size;
			result.node_size += info.size;
		}

		return result;
	}

	static Node_Infos node_infos = node_infos_make();

	void AST::reserve(int new_capacity)
	{
		if (capacity >= new_capacity) return;
		new_capacity = math_maximum(new_capacity, (capacity * 3) / 2 + 1);

		// Allocate new buffer
		void* result_buffer = nullptr;
		if (arena->resize(buffer, node_infos.node_size * capacity, node_infos.node_size * new_capacity)) {
			result_buffer = buffer;
		}
		else {
			result_buffer = arena->allocate_raw(node_infos.node_size * new_capacity, 16);
		}

		// Move all sub-arrays to correct position
		for (int i = ((int)Node_Property::MAX_ENUM_VALUE) - 1; i >= 0; i -= 1)
		{
			Node_Property_Info& info = node_infos.property_infos[i];
			memory_copy_overlapping(
				((char*)buffer) + new_capacity * info.offset, 
				((char*)buffer) + capacity     * info.offset,
				info.size * size
			);
		}

		capacity = new_capacity;
		buffer = result_buffer;
	}

	Node AST::get_root()
	{
		Node result;
		result.ast = this;
		result.index = 0;
		return result;
	}

	// Helpers
	void* ast_get_node_property(AST* ast, int node_index, Node_Property property) {
		assert(node_index > 0 && node_index < ast->size, "");
		Node_Property_Info& info = node_infos.property_infos[(int)Node_Property::NODE_TYPE];
		char* array_start = ((char*)ast->buffer) + info.offset * ast->capacity;
		return (void*) ((array_start) + info.size * node_index);
	}

	Node_Type* AST::get_node_type(int node_index) {
		return (Node_Type*)ast_get_node_property(this, node_index, Node_Property::NODE_TYPE);
	}
	int* AST::get_parent_index(int node_index) {
		return (int*)ast_get_node_property(this, node_index, Node_Property::PARENT_INDEX);
	}
	int* AST::get_child_start_index(int node_index) {
		return (int*)ast_get_node_property(this, node_index, Node_Property::CHILD_START_INDEX);
	}
	int* AST::get_child_count(int node_index) {
		return (int*)ast_get_node_property(this, node_index, Node_Property::CHILD_COUNT);
	}
	Node_Info* AST::get_node_info(int node_index) {
		return (Node_Info*)ast_get_node_property(this, node_index, Node_Property::NODE_INFO);
	}
	Text_Range* AST::get_text_range(int node_index) {
		return (Text_Range*)ast_get_node_property(this, node_index, Node_Property::RANGE);
	}
	Text_Range* AST::get_bounding_range(int node_index) {
		return (Text_Range*)ast_get_node_property(this, node_index, Node_Property::BOUNDING_RANGE);
	}
}