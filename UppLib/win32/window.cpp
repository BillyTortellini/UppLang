#include "window.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <dxgi.h>
#include <gl/GL.h>
#include <wingdi.h>
#include <dcomp.h>

#include <vector>
#include "timing.hpp"

#include "../utility/utils.hpp"
#include "../utility/datatypes.hpp"
#include "../datastructures/string.hpp"
#include "windows_helper_functions.hpp"
#include "../rendering/opengl_function_pointers.hpp"
#include "../utility/file_io.hpp"

struct Window
{
    HWND hwnd;
    HDC hdc;
    HGLRC opengl_context;
    Window_State state;
    Input input;
    HCURSOR cursor_default;

    // To make input mouse normalization
    int primary_monitor_width;
    int primary_monitor_height;

    // Saving windowed state when in fullscreen
    int saved_pos_x, saved_pos_y;
    int saved_width, saved_height;
    LONG saved_style, saved_style_ex;

    // Saved mouse cursor position for reseting into center
    int last_mouse_reset_pos_x, last_mouse_reset_pos_y;

    // For handling WM_KEYUP/WM_KEYDOWN and WM_CHAR
    bool put_next_char_into_last_key_message;

    // Problem SetWindowPos for fullscreen sends the WM_SIZE message, but input_reset resets the resized thing
    // which is why fullscreen request are buffered until handle messages is called
    bool fullscreen_state_request_was_made;
    bool desired_fullscreen_state;
    bool cursor_enabled;
};

void window_cursor_update_contrain_rect(Window* window);
void window_set_cursor_into_center_of_screen(Window* window);

byte key_translation_table[KEYBOARD_KEY_COUNT];
void keyboard_initialize_translation_table()
{
    for (int i = 0; i < KEYBOARD_KEY_COUNT; i++) {
        key_translation_table[i] = (byte)Key_Code::UNASSIGNED;
    }

    key_translation_table['A'] = (byte)Key_Code::A;
    key_translation_table['B'] = (byte)Key_Code::B;
    key_translation_table['C'] = (byte)Key_Code::C;
    key_translation_table['D'] = (byte)Key_Code::D;
    key_translation_table['E'] = (byte)Key_Code::E;
    key_translation_table['F'] = (byte)Key_Code::F;
    key_translation_table['G'] = (byte)Key_Code::G;
    key_translation_table['H'] = (byte)Key_Code::H;
    key_translation_table['I'] = (byte)Key_Code::I;
    key_translation_table['J'] = (byte)Key_Code::J;
    key_translation_table['K'] = (byte)Key_Code::K;
    key_translation_table['L'] = (byte)Key_Code::L;
    key_translation_table['M'] = (byte)Key_Code::M;
    key_translation_table['N'] = (byte)Key_Code::N;
    key_translation_table['O'] = (byte)Key_Code::O;
    key_translation_table['P'] = (byte)Key_Code::P;
    key_translation_table['Q'] = (byte)Key_Code::Q;
    key_translation_table['R'] = (byte)Key_Code::R;
    key_translation_table['S'] = (byte)Key_Code::S;
    key_translation_table['T'] = (byte)Key_Code::T;
    key_translation_table['U'] = (byte)Key_Code::U;
    key_translation_table['V'] = (byte)Key_Code::V;
    key_translation_table['W'] = (byte)Key_Code::W;
    key_translation_table['X'] = (byte)Key_Code::X;
    key_translation_table['Y'] = (byte)Key_Code::Y;
    key_translation_table['Z'] = (byte)Key_Code::Z;

    key_translation_table['1'] = (byte)Key_Code::NUM_1;
    key_translation_table['2'] = (byte)Key_Code::NUM_2;
    key_translation_table['3'] = (byte)Key_Code::NUM_3;
    key_translation_table['4'] = (byte)Key_Code::NUM_4;
    key_translation_table['5'] = (byte)Key_Code::NUM_5;
    key_translation_table['6'] = (byte)Key_Code::NUM_6;
    key_translation_table['7'] = (byte)Key_Code::NUM_7;
    key_translation_table['8'] = (byte)Key_Code::NUM_8;
    key_translation_table['9'] = (byte)Key_Code::NUM_9;
    key_translation_table['0'] = (byte)Key_Code::NUM_0;

    key_translation_table[VK_F1] = (byte)Key_Code::F1;
    key_translation_table[VK_F2] = (byte)Key_Code::F2;
    key_translation_table[VK_F3] = (byte)Key_Code::F3;
    key_translation_table[VK_F4] = (byte)Key_Code::F4;
    key_translation_table[VK_F5] = (byte)Key_Code::F5;
    key_translation_table[VK_F6] = (byte)Key_Code::F6;
    key_translation_table[VK_F7] = (byte)Key_Code::F7;
    key_translation_table[VK_F8] = (byte)Key_Code::F8;
    key_translation_table[VK_F9] = (byte)Key_Code::F9;
    key_translation_table[VK_F10] = (byte)Key_Code::F10;
    key_translation_table[VK_F11] = (byte)Key_Code::F11;
    key_translation_table[VK_F12] = (byte)Key_Code::F12;

    key_translation_table[VK_RETURN] = (byte)Key_Code::RETURN;
    key_translation_table[VK_ESCAPE] = (byte)Key_Code::ESCAPE;
    key_translation_table[VK_BACK] = (byte)Key_Code::BACKSPACE;
    key_translation_table[VK_TAB] = (byte)Key_Code::TAB;
    key_translation_table[VK_SPACE] = (byte)Key_Code::SPACE;

    key_translation_table[VK_LCONTROL] = (byte)Key_Code::LCTRL;
    key_translation_table[VK_LSHIFT] = (byte)Key_Code::LSHIFT;
    key_translation_table[VK_SHIFT] = (byte)Key_Code::SHIFT;
    key_translation_table[VK_CONTROL] = (byte)Key_Code::CTRL;
    key_translation_table[VK_MENU] = (byte)Key_Code::ALT;
    key_translation_table[VK_RCONTROL] = (byte)Key_Code::RCTRL;
    key_translation_table[VK_RSHIFT] = (byte)Key_Code::RSHIFT;

    key_translation_table[VK_LEFT] = (byte)Key_Code::ARROW_LEFT;
    key_translation_table[VK_UP] = (byte)Key_Code::ARROW_UP;
    key_translation_table[VK_RIGHT] = (byte)Key_Code::ARROW_RIGHT;
    key_translation_table[VK_DOWN] = (byte)Key_Code::ARROW_DOWN;
    //keyTranslationTable[] = KEY_RALT; 
}

LRESULT CALLBACK window_message_callback(HWND hwnd, UINT msg_type, WPARAM wparam, LPARAM lparam) 
{
    Window* window_for_message_callback = (Window*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (window_for_message_callback == nullptr) { // This should only happen for the dummy window
        return DefWindowProc(hwnd, msg_type, wparam, lparam);
    }
    Input* input = &window_for_message_callback->input;

    /* Messages on Cleanup:
        WM_CLOSE:       X is pressed on the window/ALT-F4 -> DefWindowProc sends WM_DESTROY
        WM_DESTROY:     Window will be destroyed, cannot be ignored,
                        used for cleanup (Sending WM_QUIT), DestroyWindow sends this-> DefWindowProc does NOTHING
        WM_QUIT:        GetMessage/PeekMessage will return false, PostQuitMessage functions sends this
                        ! WM_QUIT will not be received by GetMessage or PeekMessage if hwnd is not set to NULL,
                        since this message gets sent to the thread, not the window.
    */
    switch (msg_type)
    {
    // Keyboard input
    case WM_CHAR:
    {
        int key = (int)wparam;
        if (key > 255 || key < 32) { // TODO handle UTF8/UTF16 also, currently we just ignore non-ascii characters
            break;
        }
        if (window_for_message_callback->put_next_char_into_last_key_message) {
            if (input->key_messages.size == 0) {
                logg("I think this should not happen\n");
                break;
            }
            input->key_messages.data[input->key_messages.size-1].character = (char)key;
            window_for_message_callback->put_next_char_into_last_key_message = false;
        }
        else {
            input_add_key_message(input,
                key_message_make(Key_Code::UNASSIGNED, false, (char)key,
                    input->key_down[(int)Key_Code::SHIFT],
                    input->key_down[(int)Key_Code::ALT],
                    input->key_down[(int)Key_Code::CTRL]
                ));
        }
        break;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    {
        int key = (int)wparam;
        int repeatCount = lparam & 0xFFFF;
        // Check if the key was not down last frame
        //logg("Key_Down: %s\n", key_code_to_string((Key_Code)key_translation_table[key]));
        if (input->key_down[key_translation_table[key]] == false &&
            repeatCount == 1) {
            input->key_pressed[key_translation_table[key]]++;
        }
        input_add_key_message(input,
            key_message_make((Key_Code)key_translation_table[key], true, 0,
                input->key_down[(int)Key_Code::SHIFT],
                input->key_down[(int)Key_Code::ALT],
                input->key_down[(int)Key_Code::CTRL]
            ));
        input->key_down[key_translation_table[key]] = true;
        window_for_message_callback->put_next_char_into_last_key_message = true;
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        int key = (int)wparam;
        input->key_down[key_translation_table[key]] = false;
        //logg("Key_Up: %s\n", key_code_to_string((Key_Code)key_translation_table[key]));
        input_add_key_message(input,
            key_message_make((Key_Code)key_translation_table[key], false, 0,
                input->key_down[(int)Key_Code::SHIFT],
                input->key_down[(int)Key_Code::ALT],
                input->key_down[(int)Key_Code::CTRL]
            ));
        window_for_message_callback->put_next_char_into_last_key_message = true;
        break;
    }
    // Mouse input
    case WM_LBUTTONDOWN:
        input->mouse_pressed[(int)Mouse_Key_Code::LEFT] = true;
        input_add_mouse_message(input,
            mouse_message_make(Mouse_Key_Code::LEFT, true, input)
        );
        input->mouse_down[(int)Mouse_Key_Code::LEFT] = true;
        return 0;
    case WM_LBUTTONUP:
        input->mouse_down[(int)Mouse_Key_Code::LEFT] = false;
        input->mouse_released[(int)Mouse_Key_Code::LEFT] = true;
        input_add_mouse_message(input,
            mouse_message_make(Mouse_Key_Code::LEFT, false, input)
        );
        return 0;
    case WM_MBUTTONDOWN:
        if (input->mouse_down[(int)Mouse_Key_Code::MIDDLE]) {
            input->mouse_pressed[(int)Mouse_Key_Code::MIDDLE] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(Mouse_Key_Code::MIDDLE, true, input)
        );
        input->mouse_down[(int)Mouse_Key_Code::MIDDLE] = true;
        return 0;
    case WM_MBUTTONUP:
        input->mouse_down[(int)Mouse_Key_Code::MIDDLE] = false;
        if (!input->mouse_down[(int)Mouse_Key_Code::MIDDLE]) {
            input->mouse_released[(int)Mouse_Key_Code::MIDDLE] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(Mouse_Key_Code::MIDDLE, false, input)
        );
        return 0;
    case WM_RBUTTONDOWN:
        if (input->mouse_down[(int)Mouse_Key_Code::RIGHT]) {
            input->mouse_pressed[(int)Mouse_Key_Code::RIGHT] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(Mouse_Key_Code::RIGHT, true, input)
        );
        input->mouse_down[(int)Mouse_Key_Code::RIGHT] = true;
        return 0;
    case WM_RBUTTONUP:
        if (!input->mouse_down[(int)Mouse_Key_Code::RIGHT]) {
            input->mouse_released[(int)Mouse_Key_Code::RIGHT] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(Mouse_Key_Code::RIGHT, false, input)
        );
        input->mouse_down[(int)Mouse_Key_Code::RIGHT] = false;
        return 0;
    case WM_MOUSELEAVE: {
        //logg("MOUSE_LEAVE\n");
        ClipCursor(0);
        input->mouse_down[(int)Mouse_Key_Code::LEFT] = false;
        input->mouse_down[(int)Mouse_Key_Code::RIGHT] = false;
        input->mouse_down[(int)Mouse_Key_Code::MIDDLE] = false;
        break;
    }
    case WM_MOUSEWHEEL:
    {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wparam);
        input->mouse_wheel_delta += zDelta / ((float)WHEEL_DELTA);
        return 0;
    }
    case WM_ACTIVATE: 
    {
        if (wparam == WA_ACTIVE || wparam == WA_CLICKACTIVE) 
        {
            window_for_message_callback->state.in_focus = true;
            if (window_for_message_callback->state.cursor_visible) {
                SetCursor(window_for_message_callback->cursor_default);
            }
            else {
                SetCursor(0);
            }
            if (window_for_message_callback->state.cursor_reset_into_center) {
                window_set_cursor_into_center_of_screen(window_for_message_callback);
            }
        }
        else {
            input_on_focus_lost(input);
            //logg("Key_Down after Reset: %s\n", input->key_down[(int)Key_Code::O] ? "TRUE" : "FALSE");
            window_for_message_callback->state.in_focus = false;
            ClipCursor(0);
            SetCursor(window_for_message_callback->cursor_default);
            window_cursor_update_contrain_rect(window_for_message_callback);
        }
        //logg("WM_ACTIVATE\n");
        break;
    }
    case WM_MOUSEMOVE: {
        if (window_for_message_callback->state.cursor_visible) {
            SetCursor(window_for_message_callback->cursor_default);
        }
        // Skip the mouse movement calculations if we always reset into center, this is then handled in window_handle_messages
        if (window_for_message_callback->state.cursor_reset_into_center) {
            return 0;
        }
        int x = GET_X_LPARAM(lparam);
        int y = GET_Y_LPARAM(lparam);
        input->mouse_delta_x += (x - input->mouse_x);
        input->mouse_delta_y += (y - input->mouse_y);
        input->mouse_normalized_delta_x = (float)input->mouse_delta_x / window_for_message_callback->primary_monitor_width;
        input->mouse_normalized_delta_y = (float)input->mouse_delta_y / window_for_message_callback->primary_monitor_height;
        input->mouse_x = x;
        input->mouse_y = y;
        break;
    }
    case WM_SIZE:
    {
        // Get new dimension
        int new_width = LOWORD(lparam);
        int new_height = HIWORD(lparam);

        // Update window state
        Window_State* state = &window_for_message_callback->state;
        if (wparam == SIZE_MINIMIZED) { // Handle Minimization
            state->minimized = true;
            input_on_focus_lost(input);
            if (!state->cursor_visible) {
                SetCursor(0);
            }
            return 0;
        }
        if (window_for_message_callback->state.cursor_visible) {
            SetCursor(window_for_message_callback->cursor_default);
        }
        else {
            SetCursor(0);
        }

        // Handle resizing
        if (state->width != LOWORD(lparam) || state->height != HIWORD(lparam)) {
            input->client_area_resized = true;
        }
        state->width = new_width;
        state->height = new_height;
        state->minimized = false;

        return 0;
    }
    case WM_SETCURSOR: {
        // if (LOWORD(lparam) == HTCLIENT) {
        //     return TRUE;
        // }
        break;
    }
    case WM_MOVE: {
        Window_State* state = &window_for_message_callback->state;
        state->x = (i16)LOWORD(lparam);
        state->y = (i16)HIWORD(lparam);
        return 0;
    }
    case WM_DPICHANGED: {
        window_for_message_callback->state.dpi = HIWORD(wparam);
        logg("WM_DPICHANGED: %d\n", HIWORD(wparam));
    }
    case WM_CLOSE:
        logg("WM_CLOSE\n");
        input->close_request_issued = true;
        return 0;
        break;
    case WM_DESTROY:
        logg("WM_DESTROY\n");
        //PostQuitMessage(0); // Sends WM_QUIT to current threads message queue
        return 0;
        break;
    }

    return DefWindowProc(hwnd, msg_type, wparam, lparam);
}

void APIENTRY opengl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei vector_length, const GLchar* message, const void* userParam)
{
    // Ignore performance warnings and unnecessary stuff
    // if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;
    // if (id == 131185 || id == 131218) return;
    String formatted_message = string_create_empty(1024);

    switch (source)
    {
    case GL_DEBUG_SOURCE_API:             string_append(&formatted_message, "Source: API"); break;
    case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   string_append(&formatted_message, "Source: Window System"); break;
    case GL_DEBUG_SOURCE_SHADER_COMPILER: string_append(&formatted_message, "Source: Shader Compiler"); break;
    case GL_DEBUG_SOURCE_THIRD_PARTY:     string_append(&formatted_message, "Source: Third Party"); break;
    case GL_DEBUG_SOURCE_APPLICATION:     string_append(&formatted_message, "Source: Application"); break;
    case GL_DEBUG_SOURCE_OTHER:           string_append(&formatted_message, "Source: Other"); break;
    }
    string_append(&formatted_message, ", ");

    switch (type)
    {
    case GL_DEBUG_TYPE_ERROR:               string_append(&formatted_message, "Type: Error"); break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: string_append(&formatted_message, "Type: Deprecated Behaviour"); break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  string_append(&formatted_message, "Type: Undefined Behaviour"); break;
    case GL_DEBUG_TYPE_PORTABILITY:         string_append(&formatted_message, "Type: Portability"); break;
    case GL_DEBUG_TYPE_PERFORMANCE:         string_append(&formatted_message, "Type: Performance"); break;
    case GL_DEBUG_TYPE_MARKER:              string_append(&formatted_message, "Type: Marker"); break;
    case GL_DEBUG_TYPE_PUSH_GROUP:          string_append(&formatted_message, "Type: Push Group"); break;
    case GL_DEBUG_TYPE_POP_GROUP:           string_append(&formatted_message, "Type: Pop Group"); break;
    case GL_DEBUG_TYPE_OTHER:               string_append(&formatted_message, "Type: Other"); break;
    }
    string_append(&formatted_message, ", ");

    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH:         string_append(&formatted_message, "Severity: high"); break;
    case GL_DEBUG_SEVERITY_MEDIUM:       string_append(&formatted_message, "Severity: medium"); break;
    case GL_DEBUG_SEVERITY_LOW:          string_append(&formatted_message, "Severity: low"); break;
    case GL_DEBUG_SEVERITY_NOTIFICATION: string_append(&formatted_message, "Severity: notification"); break;
    }
    string_append(&formatted_message, ", message: ");
    string_append(&formatted_message, message);

    logg("OpenglError (#%d): %s\n", id, formatted_message.characters);
}



PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;
PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB;
bool window_system_initialized = false;
const char* WINDOW_CLASS_NAME = "UppGUI_WND_CLASS";

void window_initialize_system()
{
    if (window_system_initialized) {
        return;
    }
    window_system_initialized = true;

    // Register Window Class
    HINSTANCE hinstance = (HINSTANCE)GetModuleHandle(NULL);
    assert(hinstance != 0, "GetModuleHandle failed!\n");
    {
        WNDCLASS window_class = {};
        window_class.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &window_message_callback;
        window_class.lpszClassName = WINDOW_CLASS_NAME;
        window_class.hCursor = 0; // If cursor is set here, than it will be shown after each mouse move
        window_class.hInstance = hinstance;
        if (RegisterClass(&window_class) == 0) {
            helper_print_last_error();
            panic("Could not register window class\n");
        }
    }

    /*
        Initializing OpenGL under Windows:
        ----------------------------------
        Problem: Default windows SDK only supports OpenGL 1.1 functions (Header gl/GL.h is a Windows header)
         * To load other functions, we use wglGetProcAddress or LoadLibraryA + GetProcAddress

        Problem: How do we specify OpenGL version and the type of pixel buffer of the window?
         * We can use SetPixelFormat provided by Windows, but this function doesn't allow all features (sRGB...)
         * To do this we load two extensions, as described below.
         * Then we destroy the window, set the pixel format, and create the context again

        Problem: To load the extension functions for pixel format and openGL context, we need an OpenGL context:
         * Solution: Create window with dummy pixelformat and standard-dummy opengl-Context, then
                     retrieve the function pointers, destroy the window, and set it up again using the new functions.
                     This works because WGL-Functions RETRIEVAL requires an active context, but the USE does not.

        The functions we want are:
            * wglCreateContextAttribsARB
            * wglChoosePixelFormatARBG

        Afterwards we can load the remaining OpenGL function pointers we want with the methods used before.
        The specification of the new OpenGL headers can be found in the Khronos OpenGL registry, which contains:
            * glext.h     ... Headers for new OpenGL versions (Versions higher than 1.1, which is given by windows)
            * glcorearb.h ... ARB extensions, meaning extensions that were approved by the Khronos board
            * wglext.h    ... WGL extensions, extensions for windows Window creation and context handling
    */

    // Load the two wglFunctions by using dummy windows, contexts and hdcs.

    // Dummy Window
    HWND dummy_hwnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        WINDOW_CLASS_NAME,
        "dummy_window_should_not_be_visible",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        0, 0,
        hinstance,
        0
    );

    if (dummy_hwnd == NULL) {
        helper_print_last_error();
        panic("CreateWindowEx failed, could not create dummy hwnd\n");
    }

    HDC dummy_hdc = GetDC(dummy_hwnd);
    if (dummy_hdc == 0) {
        panic("GetDC failed");
    }

    // Set Pixel format, so we hopefully get hardware accelerated opengl-context so it supports OpenGL
    {
        PIXELFORMATDESCRIPTOR desired_pixel_format_descriptor;
        desired_pixel_format_descriptor.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        desired_pixel_format_descriptor.nVersion = 1;
        desired_pixel_format_descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        desired_pixel_format_descriptor.iPixelType = PFD_TYPE_RGBA;
        desired_pixel_format_descriptor.cColorBits = 32;
        desired_pixel_format_descriptor.cRedBits = 0;
        desired_pixel_format_descriptor.cRedShift = 0;
        desired_pixel_format_descriptor.cGreenBits = 0;
        desired_pixel_format_descriptor.cGreenShift = 0;
        desired_pixel_format_descriptor.cBlueBits = 0;
        desired_pixel_format_descriptor.cBlueShift = 0;
        desired_pixel_format_descriptor.cAlphaBits = 0;
        desired_pixel_format_descriptor.cAlphaShift = 0;
        desired_pixel_format_descriptor.cAccumBits = 0;
        desired_pixel_format_descriptor.cAccumRedBits = 0;
        desired_pixel_format_descriptor.cAccumGreenBits = 0;
        desired_pixel_format_descriptor.cAccumBlueBits = 0;
        desired_pixel_format_descriptor.cAccumAlphaBits = 0;
        desired_pixel_format_descriptor.cDepthBits = 24;
        desired_pixel_format_descriptor.cStencilBits = 8;
        desired_pixel_format_descriptor.cAuxBuffers = 0;
        desired_pixel_format_descriptor.iLayerType = PFD_MAIN_PLANE;
        desired_pixel_format_descriptor.bReserved = 0;
        desired_pixel_format_descriptor.dwLayerMask = 0;
        desired_pixel_format_descriptor.dwVisibleMask = 0;
        desired_pixel_format_descriptor.dwDamageMask = 0;

        int closest_pixel_format_id = ChoosePixelFormat(dummy_hdc, &desired_pixel_format_descriptor);
        if (closest_pixel_format_id == 0) {
            helper_print_last_error();
            panic("ChoosePixelFormat failed!\n");
        }

        PIXELFORMATDESCRIPTOR closest_pixel_format_descriptor;
        DescribePixelFormat(dummy_hdc, closest_pixel_format_id, sizeof(PIXELFORMATDESCRIPTOR), &closest_pixel_format_descriptor);
        if (!SetPixelFormat(dummy_hdc, closest_pixel_format_id, &closest_pixel_format_descriptor)) {
            helper_print_last_error();
            panic("SetPixelFormat failed!");
        }
    }

    HGLRC dummy_gl_context = wglCreateContext(dummy_hdc);
    if (dummy_gl_context == 0) {
        helper_print_last_error();
        panic("WGLCreateContext failed\n!");
    }

    if (!wglMakeCurrent(dummy_hdc, dummy_gl_context)) {
        helper_print_last_error();
        panic("WGLMakeCurrent failed\n");
    }

    // Get wglCreateContextAttribsArb
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)opengl_get_function_address("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)opengl_get_function_address("wglChoosePixelFormatARB");

    if (wglCreateContextAttribsARB == nullptr) {
        panic("Could not retrieve wglCreateContextAttribsARB function pointer.");
    }
    if (wglChoosePixelFormatARB == nullptr) {
        panic("Could not retrieve wglCreateContextAttribsARB function pointer.");
    }

    // Delete dummy context and dummy hwnd
    wglMakeCurrent(dummy_hdc, 0); // Remove current hdc 
    wglDeleteContext(dummy_gl_context); // Delete dummy context
    ReleaseDC(dummy_hwnd, dummy_hdc);
    DestroyWindow(dummy_hwnd); // Delete the dummy_window

    // DestroyWindow sends PostQuitMessage, which needs to be removed from threads message queue
    // NEWER: Dont know if it is actually necessary
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) {
            break;
        }
    }
}

// Position, visible, fullscreen, decoration, opengl version, framebuffer stuff
Window* window_create(const char* window_title, int multisample_count)
{
    window_initialize_system();
    HINSTANCE hinstance = (HINSTANCE)GetModuleHandle(NULL);
    assert(hinstance != 0, "GetModuleHandle failed!\n");

    // Setup new Window with the wgl Functions
    Window* window = new Window();
    window->input = input_create();
    {
        // Create Window
        HWND hwnd = CreateWindowEx(
            WS_EX_APPWINDOW,
            WINDOW_CLASS_NAME,
            window_title,
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            0, 0,
            hinstance,
            0
        );
        if (hwnd == NULL) {
            panic("Could not create hwnd");
        }

        // Get DeviceContext
        HDC hdc = GetDC(hwnd);
        if (hdc == NULL) {
            panic("Could not get hdc");
        }

        // Set PixelFormat
        {
            if (multisample_count > 16) {
                multisample_count = 16;
            }
            int pixel_format_without_multisampling[] = {
                WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
                WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
                WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
                WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
                WGL_COLOR_BITS_ARB,     32,
                WGL_DEPTH_BITS_ARB,     24,
                WGL_STENCIL_BITS_ARB,   8,
                WGL_SWAP_METHOD_ARB,    WGL_SWAP_EXCHANGE_ARB, 
                0
            };
            int pixel_format_with_multisampling[] = {
                WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
                WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
                WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
                WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
                WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
                WGL_COLOR_BITS_ARB,     32,
                WGL_DEPTH_BITS_ARB,     24,
                WGL_STENCIL_BITS_ARB,   8,
                WGL_SAMPLE_BUFFERS_ARB, 1, // Number of multisampling buffers (I think should always be one
                WGL_SAMPLES_ARB,        multisample_count, // Number of samples
                0
            };
            int* pixel_format_attributes;
            if (multisample_count == 0 || multisample_count == 1) {
                pixel_format_attributes = pixel_format_without_multisampling;
            }
            else {
                pixel_format_attributes = pixel_format_with_multisampling;
            }

            int pixel_format;
            UINT available_format_count;
            wglChoosePixelFormatARB(hdc, pixel_format_attributes, 0, 1, &pixel_format, &available_format_count);
            if (available_format_count == 0) {
                panic("No pixel format is available");
            }

            PIXELFORMATDESCRIPTOR pfd;
            DescribePixelFormat(hdc, pixel_format, sizeof(pfd), &pfd);
            if (!SetPixelFormat(hdc, pixel_format, &pfd)) {
                panic("Could not set pixel format!");
            }
        }

        // Create OpenGL context
        int context_attributes[] = {
            WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
            WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
            WGL_CONTEXT_MINOR_VERSION_ARB, 3,
            0 // 0 termintes the attributes list
        };
        HGLRC opengl_context = wglCreateContextAttribsARB(hdc, 0, context_attributes);
        if (!opengl_context) {
            panic("wlgCreateContextAttribsARB failed!");
        }

        if (!wglMakeCurrent(hdc, opengl_context)) {
            panic("Failed to make context current");
        }

        // Load OpenGL Functions
        if (!opengl_load_all_functions()) {
            panic("Could not load opengl_functions");
        }
        opengl_print_all_extensions((void*)&hdc);

        // Display new Window
        ShowWindow(hwnd, SW_SHOWNORMAL);

        // Set window attributes for later use
        window->hwnd = hwnd;
        window->hdc = hdc;
        window->opengl_context = opengl_context;

        // Handle all messages, so that window pos will be set
        window_handle_messages(window, false);

        // Get window state
        window->state.fullscreen = false;
        {
            RECT rect;
            GetClientRect(window->hwnd, &rect);
            window->state.width = rect.right;
            window->state.height = rect.bottom;

            POINT point;
            point.x = 0;
            point.y = 0;
            ClientToScreen(window->hwnd, &point);
            window->state.x = point.x;
            window->state.y = point.y;
        }
        window->state.minimized = false;
        window->state.cursor_constrained = false;
        window->state.cursor_reset_into_center = false;
        window->state.cursor_visible = true;
        window->state.in_focus = true;
        window->put_next_char_into_last_key_message = false;
        window->cursor_default = LoadCursor(NULL, IDC_ARROW);
        window->cursor_enabled = true;
        if (window->cursor_default == 0) {
            panic("Could not load cursor");
        }
        SetCursor(window->cursor_default);
        window_set_cursor_visibility(window, true);

        input_reset(&window->input);
        keyboard_initialize_translation_table();

        // Opengl Options and debugging
        window->state.vsync = true;
        wglSwapIntervalEXT(1);

        // glEnable(GL_DEBUG_OUTPUT);
        // glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        // glDebugMessageCallback(opengl_debug_callback, 0);

        // Set dpi
        {
            // Fucking windows this does not work as proven on my laptop
            //SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            //SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE); 
            SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            window->state.dpi = GetDpiForWindow(window->hwnd);
        }

        // Get primary monitor info
        MONITORINFO monitor_info;
        monitor_info.cbSize = sizeof(monitor_info);
        GetMonitorInfo(MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTOPRIMARY), &monitor_info);
        window->primary_monitor_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
            window->primary_monitor_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
        
        SetWindowLongPtr(window->hwnd, GWLP_USERDATA, (LONG_PTR)window);
    }

    //FreeConsole();
    return window;
}

void window_activate_context(Window* window) {
    wglMakeCurrent(window->hdc, window->opengl_context);
}

void window_change_fullscreen_mode(Window* window, bool fullscreen);

/* 
    What happens when i press A
    WM_KEY_DOWN KEY_A
    WM_CHAR     'A'
    WM_KEY_UP   KEY_A

    What happens when i press Escape
    WM_KEY_DOWN ESCAPE
    WM_KEY_UP   ESCAPE

    So what i want to do:
    If I get KEY_DOWN/UP in message_handler, I tell WM_CHAR that it should put itself into the current message
*/
bool time_init = false;
Timer timer;
bool window_handle_messages(Window* window, bool block_until_next_message, int* message_count)
{
    if (!time_init) {
        timer = timer_make();
        time_init = true;
    }
    double before_all = timer_current_time_in_seconds(&timer);

    // Handle fullscreen requests
    if (window->fullscreen_state_request_was_made) {
        window_change_fullscreen_mode(window, window->desired_fullscreen_state);
        window->fullscreen_state_request_was_made = false;
    }

    // Handle cursor if it we are in reset state
    if (window->state.cursor_reset_into_center && !window->state.minimized && window->state.in_focus)
    {
        Input* input = &window->input;

        POINT cursor_pos;
        GetCursorPos(&cursor_pos);
        input->mouse_delta_x += (cursor_pos.x - window->last_mouse_reset_pos_x);
        input->mouse_delta_y += (cursor_pos.y - window->last_mouse_reset_pos_y);
        input->mouse_normalized_delta_x = (float)input->mouse_delta_x / window->primary_monitor_width;
        input->mouse_normalized_delta_y = (float)input->mouse_delta_y / window->primary_monitor_height;
        input->mouse_x = cursor_pos.x;
        input->mouse_y = cursor_pos.y;

        // Set cursor into center of screen
        window_set_cursor_into_center_of_screen(window);
    }

    double start = timer_current_time_in_seconds(&timer);
    MSG msg = {};
    int msg_count = 0;
    if (block_until_next_message)
    {
        if (!GetMessage(&msg, 0, 0, 0)) {
            return false; // WM_QUIT was sent
        }
        else { // Handle message received from GetMessage
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            msg_count += 1;
        }
        // Handle all remaining available messages
        while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            msg_count += 1;
            if (msg.message == WM_QUIT) { // WM_QUIT was sent
                return false;
            }
        }
    }
    else
    {
        // Handle all messages
        while (PeekMessage(&msg, window->hwnd, 0, 0, PM_REMOVE | PM_NOYIELD))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) { // WM_QUIT was sent
                return false;
            }
            msg_count += 1;
        }
    }
    if (message_count != 0) {
        *message_count = msg_count;
    }

    double end = timer_current_time_in_seconds(&timer);
    if (end - start > 0.003 && !block_until_next_message) {
        logg("Took longer than a ms %3.2fms\n", (end-start) * 1000.0f);
        // logg("Time for first: %3.2fms\n", (end - start) * 1000.0f);
        // logg("MEssage count: #%d\n", msg_count);
    }

    return true;
}

void window_close(Window* window) {
    DestroyWindow(window->hwnd); // Delete the dummy_window
    window->input.close_request_issued = true;
}

void window_destroy(Window* window)
{
    input_destroy(&window->input);
    wglMakeCurrent(window->hdc, 0); // Remove current hdc 
    wglDeleteContext(window->opengl_context); // Delete dummy context
    ReleaseDC(window->hwnd, window->hdc);
}

void window_swap_buffers(Window* window) {
    SwapBuffers(window->hdc);
}

void window_change_fullscreen_mode(Window* window, bool fullscreen)
{
    /* Useful ex styles
    WS_EX_APPWINDOW (Forces onto taskbar when visible)
    WS_EX_CLIENTEDGE (border)
    WS_EX_WINDOWEDGE (border)
    WS_EX_TOPMOST

    Useful Normal styles
    WS_VISIBLE      Sonst net clickable
    WS_CAPTION      Title + Titlebar
    WS_SYSMENU      X-Button (Needs WS_CAPTION)
    WS_MAXIMIZEBOX  Maximize (Needs WS_SYSMENU)
    WS_MINIMIZEBOX  Minimize (Needs WS_SYSMENU)
    WS_THICKFRAME   Resizable (Same as WS_SIZEBOX)

    Completely Useless:
    WS_BORDER       Doesn't do anything with WS_THICKFRAME
    WS_OVERLAPPED   Is literally 0x0

    SetWindowLong(window->hwnd, GWL_STYLE, WS_VISIBLE | WS_BORDER);
    SetWindowLong(window->hwnd, GWL_EXSTYLE, 0);
    */
    if (window->state.fullscreen != fullscreen && !window->state.minimized)
    {
        window->state.fullscreen = fullscreen;
        if (fullscreen) {
            // Save current position, size and style
            RECT window_rect;
            GetWindowRect(window->hwnd, &window_rect);
            window->saved_pos_x = window_rect.left;
            window->saved_pos_y = window_rect.top;
            window->saved_width = window_rect.right - window_rect.left;
            window->saved_height = window_rect.bottom - window_rect.top;
            window->saved_style = GetWindowLong(window->hwnd, GWL_STYLE);
            window->saved_style_ex = GetWindowLong(window->hwnd, GWL_EXSTYLE);

            // Set fullscreen
            SetWindowLong(window->hwnd, GWL_STYLE, WS_VISIBLE);
            SetWindowLong(window->hwnd, GWL_EXSTYLE, 0);

            MONITORINFO monitor_info;
            monitor_info.cbSize = sizeof(monitor_info);
            GetMonitorInfo(MonitorFromWindow(window->hwnd, MONITOR_DEFAULTTONEAREST), &monitor_info);
            SetWindowPos(window->hwnd, 0, monitor_info.rcMonitor.left, monitor_info.rcMonitor.top,
                monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                SWP_NOZORDER | SWP_FRAMECHANGED);
        }
        else {
            // Restore saved position, width and style
            SetWindowLong(window->hwnd, GWL_STYLE, window->saved_style);
            SetWindowLong(window->hwnd, GWL_EXSTYLE, window->saved_style_ex);
            SetWindowPos(window->hwnd, 0,
                window->saved_pos_x, window->saved_pos_y,
                window->saved_width, window->saved_height,
                SWP_NOZORDER | SWP_FRAMECHANGED);
        }
    }

}

void window_set_fullscreen(Window* window, bool fullscreen)
{
    window->fullscreen_state_request_was_made = true;
    window->desired_fullscreen_state = fullscreen;
}

void window_set_position(Window* window, int x, int y) {
    if (!window->state.minimized && !window->state.fullscreen) {
        if (window->state.x != x || window->state.y != y) {
            window->state.x = x;
            window->state.y = y;

            RECT rect;
            GetWindowRect(window->hwnd, &rect);
            SetWindowPos(window->hwnd, 0,
                x, y,
                window->saved_width, window->saved_height,
                SWP_NOZORDER | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
        }
    }
}

void window_set_size(Window* window, int width, int height) {
    if (!window->state.minimized && !window->state.fullscreen) {
        if (window->state.width != width || window->state.height != height) {
            window->state.width = width;
            window->state.height = height;

            RECT rect;
            GetWindowRect(window->hwnd, &rect);
            SetWindowPos(window->hwnd, 0,
                0, 0,
                width, height,
                SWP_NOZORDER | SWP_NOMOVE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
        }
    }
}

Window_State* window_get_window_state(Window* window) {
    return &window->state;
}

void window_set_vsync(Window* window, bool vsync) {
    if (vsync != window->state.vsync)
    {
        window->state.vsync = vsync;
        if (vsync) {
            wglSwapIntervalEXT(1);
        }
        else {
            wglSwapIntervalEXT(0);
        }
    }
}

void window_set_focus(Window* window) {
    if (window->state.minimized) return;
    BOOL success = SetForegroundWindow(window->hwnd);
    if (success == 0) {
        printf("Set foreground window failed!\n");
    }
}

void window_set_minimized(Window* window, bool minimized) {
    if (window->state.minimized != minimized)
    {
        window->state.minimized = minimized;
        if (minimized) {
            ShowWindow(window->hwnd, SW_MINIMIZE);
        }
        else {
            ShowWindow(window->hwnd, SW_RESTORE | SW_SHOW);
        }
    }
}

void window_set_cursor_visibility(Window* window, bool visible) {
    if (window->state.cursor_visible != visible) {
        window->state.cursor_visible = visible;
        if (visible) {
            window->cursor_enabled = true;
            SetCursor(window->cursor_default);
        }
        else {
            window->cursor_enabled = false;
            SetCursor(0);
        }
    }
}

void window_cursor_update_contrain_rect(Window* window)
{
    if (window->state.cursor_constrained && !window->state.minimized && window->state.in_focus)
    {
        RECT client_rect;
        GetClientRect(window->hwnd, &client_rect);
        POINT left_top;
        left_top.x = client_rect.left;
        left_top.y = client_rect.top;
        ClientToScreen(window->hwnd, &left_top);

        RECT confined;
        confined.left = left_top.x;
        confined.top = left_top.y;
        confined.right = confined.left + (client_rect.right - client_rect.left);
        confined.bottom = confined.top + (client_rect.bottom - client_rect.top);

        ClipCursor(&confined);
    }
    else {
        ClipCursor(0);
    }
}

void window_set_cursor_reset_into_center(Window* window, bool reset) {
    if (window->state.cursor_reset_into_center != reset)
    {
        window->state.cursor_reset_into_center = reset;
        if (reset) {
            window_set_cursor_into_center_of_screen(window);
        }
    }
}

void window_set_cursor_icon(Window* window, Cursor_Icon_Type cursor)
{
    HCURSOR cursorHandle = LoadCursor(NULL, IDC_HAND);
    switch (cursor)
    {
    case Cursor_Icon_Type::ARROW: cursorHandle = LoadCursor(NULL, IDC_ARROW); break;
    case Cursor_Icon_Type::HAND:  cursorHandle = LoadCursor(NULL, IDC_HAND); break;
    case Cursor_Icon_Type::IBEAM: cursorHandle = LoadCursor(NULL, IDC_IBEAM); break;
    case Cursor_Icon_Type::SIZE_HORIZONTAL: cursorHandle = LoadCursor(NULL, IDC_SIZEWE); break;
    case Cursor_Icon_Type::SIZE_VERTICAL: cursorHandle = LoadCursor(NULL, IDC_SIZENS); break;
    case Cursor_Icon_Type::SIZE_NORTHEAST: cursorHandle = LoadCursor(NULL, IDC_SIZENESW); break;
    case Cursor_Icon_Type::SIZE_SOUTHEAST: cursorHandle = LoadCursor(NULL, IDC_SIZENWSE); break;
    default:panic("");
    }
    if (cursorHandle == NULL) {
        panic("I think the normal cursors should all work!");
        return;
    }
    window->cursor_default = cursorHandle;
    if (window->cursor_enabled) {
        SetCursor(cursorHandle);
    }
}

Input* window_get_input(Window* window) {
    return &window->input;
}

void window_set_cursor_constrain(Window* window, bool constrain) {
    if (window->state.cursor_constrained != constrain) {
        window->state.cursor_constrained = constrain;
        window_cursor_update_contrain_rect(window);
    }
}

void window_set_cursor_into_center_of_screen(Window* window)
{
    RECT window_rect;
    GetWindowRect(window->hwnd, &window_rect);
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    window->last_mouse_reset_pos_x = window_rect.left + width / 2;
    window->last_mouse_reset_pos_y = window_rect.top + height / 2;
    SetCursorPos(window->last_mouse_reset_pos_x, window->last_mouse_reset_pos_y);
}

struct Window_Saved_Position
{
    RECT window_rect;
    RECT console_rect;
};

void window_load_position(Window* window, const char* filename)
{
    auto result = file_io_load_binary_file(filename);
    SCOPE_EXIT(file_io_unload_binary_file(&result));
    if (result.available)
    {
        assert(result.value.size == sizeof(Window_Saved_Position), "Hey");
        auto pos = *((Window_Saved_Position*)result.value.data);
        auto win = pos.window_rect;
        if (win.left == win.right || win.top == win.bottom) return;
        MoveWindow(window->hwnd, win.left, win.top, win.right - win.left, win.bottom - win.top, false);

        HWND hwnd = GetConsoleWindow();
        win = pos.console_rect;
        if (hwnd != NULL) {
            if (win.left == win.right || win.top == win.bottom) return;
            MoveWindow(hwnd, win.left, win.top, win.right - win.left, win.bottom - win.top, false);
        }
    }
}

void window_save_position(Window* window, const char* filename)
{
    Window_Saved_Position pos;
    memory_zero(&pos);

    // save window rect
    GetWindowRect(window->hwnd, &pos.window_rect);
    // Save console rect if exists
    HWND hwnd = GetConsoleWindow();
    if (hwnd != NULL) {
        GetWindowRect(hwnd, &pos.console_rect);
    }

    auto data = array_create_static_as_bytes(&pos, 1);
    file_io_write_file(filename, data);
}

IDXGIOutput* g_output = 0;
bool initialized = false;

void window_initialize_dxgi_output()
{
    if (!initialized) {
        IDXGIFactory* pFactory;
        HRESULT hr = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&pFactory));

        UINT i = 0;
        IDXGIAdapter* pAdapter;
        std::vector<IDXGIAdapter*> vAdapters;
        while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
            vAdapters.push_back(pAdapter);
            UINT j = 0;
            DXGI_ADAPTER_DESC desc;
            pAdapter->GetDesc(&desc);
            IDXGIOutput* output;
            while (pAdapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND) {
                g_output = output;

                DXGI_OUTPUT_DESC desc;
                output->GetDesc(&desc);
                int len = lstrlenW(desc.DeviceName);
                auto tmp = string_create_empty(32);
                SCOPE_EXIT(string_destroy(&tmp));
                wcstombs(tmp.characters, desc.DeviceName, 32);
                tmp.size = (int) strlen(tmp.characters);
                logg("Import output: %s\n", tmp.characters);

                break;
                j++;
            }
            i++;
        }
        initialized = true;
    }
}

void window_wait_vsynch()
{
    window_initialize_dxgi_output();
    g_output->WaitForVBlank();
}

void window_calculate_vsynch_beat(double& vsync_start, double& time_between_vsynchs, Timer& timer)
{
    window_initialize_dxgi_output();
    double reference = timer_current_time_in_seconds(&timer);
    bool first = true;
    g_output->WaitForVBlank();
    vsync_start = timer_current_time_in_seconds(&timer);
    time_between_vsynchs = 1 / 60.0;

    /// while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND) {
    ///     vAdapters.push_back(pAdapter);
    ///     UINT j = 0;
    ///     IDXGIOutput* output;
    ///     DXGI_ADAPTER_DESC desc;
    ///     pAdapter->GetDesc(&desc);

    ///     while (pAdapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND) {
    ///         DXGI_OUTPUT_DESC desc;
    ///         output->GetDesc(&desc);
    ///         int len = lstrlenW(desc.DeviceName);
    ///         auto tmp = string_create_empty(32);
    ///         SCOPE_EXIT(string_destroy(&tmp));
    ///         wcstombs(tmp.characters, desc.DeviceName, 32);
    ///         tmp.size = strlen(tmp.characters);
    ///         logg("%s\n", tmp.characters);

    ///         // Do something stupid here, like measuring 3 time spots right after wait for vsynch
    ///         const int timestamp_count = 20;
    ///         double timestamps[timestamp_count];
    ///         for (int k = 0; k < timestamp_count; k++) {
    ///             if (output->WaitForVBlank() != S_OK) {
    ///                 panic("");
    ///                 logg("Something isn't working lol\n");
    ///             }
    ///             timestamps[k] = timer_current_time_in_seconds(&timer);
    ///         }

    ///         // Print timestamps, and difference to previous
    ///         logg("Timestamps:\n");
    ///         double average_difference = 0;
    ///         for (int k = 0; k < timestamp_count; k++) {
    ///             double diff = 0;
    ///             if (k > 0) {
    ///                 diff = timestamps[k] - timestamps[k - 1];
    ///                 average_difference += diff;
    ///             }
    ///             // logg("    %f (-%f)\n", timestamps[k], diff);
    ///         }
    ///         average_difference = average_difference / (timestamp_count - 1); // Don't count first timestamp
    ///         // Calculate max difference
    ///         double max_difference = 0;
    ///         for (int k = 0; k < timestamp_count; k++) {
    ///             double diff = 0;
    ///             if (k > 0) {
    ///                 diff = timestamps[k] - timestamps[k - 1];
    ///                 max_difference = math_maximum(max_difference, math_absolute(diff - average_difference));
    ///             }
    ///         }
    ///         logg("Average difference: %f, max: %f\n", average_difference, max_difference);


    ///         logg("Offsets:\n");
    ///         double average_offset = 0;
    ///         double max_offset = 0;
    ///         for (int k = 0; k < timestamp_count; k++) {
    ///             double offset = math_modulo(timestamps[k] - reference, average_difference);
    ///             average_offset += offset;
    ///             // logg("    %f\n", offset);
    ///         }
    ///         average_offset = average_offset / timestamp_count;
    ///         for (int k = 0; k < timestamp_count; k++) {
    ///             double offset = math_modulo(timestamps[k] - reference, average_difference);
    ///             max_offset = math_maximum(max_offset, math_absolute(offset - average_offset));
    ///         }
    ///         logg("Average offset: %f, max: %f\n", average_offset, max_offset);
    ///         if (first) {
    ///             vsync_start = reference + average_offset;
    ///             time_between_vsynchs = average_difference;
    ///             if (math_absolute(time_between_vsynchs - (1 / 60.0)) < 0.0005) {
    ///                 time_between_vsynchs = 1 / 60.0;
    ///             }
    ///             first = false;
    ///         }

    ///         break;
    ///         j++;
    ///     }

    ///     ++i;
    /// }
}
