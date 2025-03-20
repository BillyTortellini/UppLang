#pragma once

#include <../../datastructures/string.hpp>
#include <../../datastructures/array.hpp>
#include <../../math/umath.hpp>

struct Input;
struct Window;
struct Render_Pass;

struct ivec2
{
    ivec2() {}
    explicit ivec2(int val) : x(val), y(val) {}
    explicit ivec2(int x, int y) : x(x), y(y) {}

    ivec2 operator+(ivec2& other) { return ivec2(this->x + other.x, this->y + other.y); }
    ivec2 operator-(ivec2& other) { return ivec2(this->x - other.x, this->y - other.y); }
    ivec2 operator*(ivec2& other) { return ivec2(this->x * other.x, this->y * other.y); }
    ivec2 operator/(ivec2& other) { return ivec2(this->x / other.x, this->y / other.y); }
    ivec2 operator+(int value) { return ivec2(this->x + value, this->y + value); }
    ivec2 operator-(int value) { return ivec2(this->x - value, this->y - value); }
    ivec2 operator*(int value) { return ivec2(this->x * value, this->y * value); }
    ivec2 operator/(int value) { return ivec2(this->x / value, this->y / value); }

    int x;
    int y;
};

struct Container_Handle
{
	int container_index;
};

struct Widget_Handle
{
	int widget_index;
	bool created_this_frame;
};

struct Window_Handle
{
	int window_index;
	bool created_this_frame;
	Container_Handle container;
};

enum class Window_Layout
{
	FLOAT,
	ANCHOR_RIGHT,
	DROPDOWN,
};

struct UI_String
{
	int start_index;
	int length;
};

struct Window_Style
{
	Window_Layout layout;
	bool has_title_bar;
	UI_String title;
	vec4 bg_color;
	ivec2 min_size;
	bool is_hidden;
	union {
		Widget_Handle dropdown_parent_widget;
	} options;
};

struct UI_Input_Info
{
	bool has_mouse_hover;
	bool has_keyboard_input;
};


void ui_system_initialize();
void ui_system_shutdown();
UI_Input_Info ui_system_start_frame(Input* input);
void ui_system_end_frame_and_render(Window* whole_window, Input* input, Render_Pass* render_pass_alpha_blended);

void ui_system_push_active_container(Container_Handle handle, bool pop_after_next_push);
void ui_system_pop_active_container();
Window_Handle ui_system_add_window(Window_Style style);
void ui_system_set_window_topmost(Window_Handle window_handle);

Window_Style window_style_make_floating(const char* title);
Window_Style window_style_make_anchored(const char* title);



// Builder code
struct Button_Input
{
	Widget_Handle widget;
	bool was_pressed;
};

struct Text_Input_State {
	bool text_was_changed;
	String new_text;
	Widget_Handle handle;
};

struct UI_Subsection_Info
{
	bool enabled;
	Container_Handle container;
};

struct Dropdown_State
{
	bool is_open;
	int value;
	bool value_was_changed;
};

Widget_Handle ui_system_push_label(String text, bool restrain_label_size);
Widget_Handle ui_system_push_label(const char* text, bool restrain_label_size);
void ui_system_push_next_component_label(const char* label_text);
Button_Input ui_system_push_button(const char* label_text);
Text_Input_State ui_system_push_text_input(String text);
int ui_system_push_int_input(int value);
float ui_system_push_float_input(float value);
bool ui_system_push_checkbox(bool enabled);
bool ui_system_push_close_button();
UI_Subsection_Info ui_system_push_subsection(bool enabled, const char* section_name, bool own_scrollbar);
void ui_system_push_dropdown(Dropdown_State& state, Array<String> possible_values);
Container_Handle ui_system_push_line_container();


void ui_system_push_test_windows();