#include "window.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <gl/GL.h>
#include <wingdi.h>

#include "../utility/utils.hpp"
#include "../utility/datatypes.hpp"
#include "../datastructures/string.hpp"
#include "windows_helper_functions.hpp"
#include "../rendering/opengl_function_pointers.hpp"

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
};

void window_cursor_update_contrain_rect(Window* window);
void window_set_cursor_into_center_of_screen(Window* window);

byte key_translation_table[KEYBOARD_KEY_COUNT];
void keyboard_initialize_translation_table()
{
    for (int i = 0; i < KEYBOARD_KEY_COUNT; i++) {
        key_translation_table[i] = KEY_CODE::UNASSIGNED;
    }

    key_translation_table['A'] = KEY_CODE::A;
    key_translation_table['B'] = KEY_CODE::B;
    key_translation_table['C'] = KEY_CODE::C;
    key_translation_table['D'] = KEY_CODE::D;
    key_translation_table['E'] = KEY_CODE::E;
    key_translation_table['F'] = KEY_CODE::F;
    key_translation_table['G'] = KEY_CODE::G;
    key_translation_table['H'] = KEY_CODE::H;
    key_translation_table['I'] = KEY_CODE::I;
    key_translation_table['J'] = KEY_CODE::J;
    key_translation_table['K'] = KEY_CODE::K;
    key_translation_table['L'] = KEY_CODE::L;
    key_translation_table['M'] = KEY_CODE::M;
    key_translation_table['N'] = KEY_CODE::N;
    key_translation_table['O'] = KEY_CODE::O;
    key_translation_table['P'] = KEY_CODE::P;
    key_translation_table['Q'] = KEY_CODE::Q;
    key_translation_table['R'] = KEY_CODE::R;
    key_translation_table['S'] = KEY_CODE::S;
    key_translation_table['T'] = KEY_CODE::T;
    key_translation_table['U'] = KEY_CODE::U;
    key_translation_table['V'] = KEY_CODE::V;
    key_translation_table['W'] = KEY_CODE::W;
    key_translation_table['X'] = KEY_CODE::X;
    key_translation_table['Y'] = KEY_CODE::Y;
    key_translation_table['Z'] = KEY_CODE::Z;

    key_translation_table['1'] = KEY_CODE::NUM_1;
    key_translation_table['2'] = KEY_CODE::NUM_2;
    key_translation_table['3'] = KEY_CODE::NUM_3;
    key_translation_table['4'] = KEY_CODE::NUM_4;
    key_translation_table['5'] = KEY_CODE::NUM_5;
    key_translation_table['6'] = KEY_CODE::NUM_6;
    key_translation_table['7'] = KEY_CODE::NUM_7;
    key_translation_table['8'] = KEY_CODE::NUM_8;
    key_translation_table['9'] = KEY_CODE::NUM_9;
    key_translation_table['0'] = KEY_CODE::NUM_0;

    key_translation_table[VK_F1] = KEY_CODE::F1;
    key_translation_table[VK_F2] = KEY_CODE::F2;
    key_translation_table[VK_F3] = KEY_CODE::F3;
    key_translation_table[VK_F4] = KEY_CODE::F4;
    key_translation_table[VK_F5] = KEY_CODE::F5;
    key_translation_table[VK_F6] = KEY_CODE::F6;
    key_translation_table[VK_F7] = KEY_CODE::F7;
    key_translation_table[VK_F8] = KEY_CODE::F8;
    key_translation_table[VK_F9] = KEY_CODE::F9;
    key_translation_table[VK_F10] = KEY_CODE::F10;
    key_translation_table[VK_F11] = KEY_CODE::F11;
    key_translation_table[VK_F12] = KEY_CODE::F12;

    key_translation_table[VK_RETURN] = KEY_CODE::RETURN;
    key_translation_table[VK_ESCAPE] = KEY_CODE::ESCAPE;
    key_translation_table[VK_BACK] = KEY_CODE::BACKSPACE;
    key_translation_table[VK_TAB] = KEY_CODE::TAB;
    key_translation_table[VK_SPACE] = KEY_CODE::SPACE;

    key_translation_table[VK_LCONTROL] = KEY_CODE::LCTRL;
    key_translation_table[VK_LSHIFT] = KEY_CODE::LSHIFT;
    key_translation_table[VK_SHIFT] = KEY_CODE::SHIFT;
    key_translation_table[VK_CONTROL] = KEY_CODE::CTRL;
    key_translation_table[VK_MENU] = KEY_CODE::ALT;
    key_translation_table[VK_RCONTROL] = KEY_CODE::RCTRL;
    key_translation_table[VK_RSHIFT] = KEY_CODE::RSHIFT;
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
            if (window_for_message_callback->input.key_messages.size == 0) {
                logg("I think this should not happen\n");
                break;
            }
            window_for_message_callback->input.key_messages.data[window_for_message_callback->input.key_messages.size-1].character = (char)key;
            window_for_message_callback->put_next_char_into_last_key_message = false;
        }
        else {
            input_add_key_message(&window_for_message_callback->input,
                key_message_make(KEY_CODE::UNASSIGNED, false, (char)key,
                    window_for_message_callback->input.key_down[KEY_CODE::SHIFT],
                    window_for_message_callback->input.key_down[KEY_CODE::ALT],
                    window_for_message_callback->input.key_down[KEY_CODE::CTRL]
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
        if (window_for_message_callback->input.key_down[key_translation_table[key]] == false &&
            repeatCount == 1) {
            window_for_message_callback->input.key_pressed[key_translation_table[key]]++;
        }
        input_add_key_message(&window_for_message_callback->input,
            key_message_make((KEY_CODE::ENUM)key_translation_table[key], true, 0,
                window_for_message_callback->input.key_down[KEY_CODE::SHIFT],
                window_for_message_callback->input.key_down[KEY_CODE::ALT],
                window_for_message_callback->input.key_down[KEY_CODE::CTRL]
            ));
        window_for_message_callback->input.key_down[key_translation_table[key]] = true;
        window_for_message_callback->put_next_char_into_last_key_message = true;
        break;
    }
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
        int key = (int)wparam;
        window_for_message_callback->input.key_down[key_translation_table[key]] = false;
        input_add_key_message(&window_for_message_callback->input, 
            key_message_make((KEY_CODE::ENUM)key_translation_table[key], false, 0,
                window_for_message_callback->input.key_down[KEY_CODE::SHIFT],
                window_for_message_callback->input.key_down[KEY_CODE::ALT],
                window_for_message_callback->input.key_down[KEY_CODE::CTRL]
            ));
        window_for_message_callback->put_next_char_into_last_key_message = true;
        break;
    }
    // Mouse input
    case WM_LBUTTONDOWN:
        window_for_message_callback->input.mouse_pressed[MOUSE_KEY_CODE::LEFT] = true;
        input_add_mouse_message(input,
            mouse_message_make(MOUSE_KEY_CODE::LEFT, true, input)
        );
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::LEFT] = true;
        return 0;
    case WM_LBUTTONUP:
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::LEFT] = false;
        window_for_message_callback->input.mouse_released[MOUSE_KEY_CODE::LEFT] = true;
        input_add_mouse_message(input,
            mouse_message_make(MOUSE_KEY_CODE::LEFT, false, input)
        );
        return 0;
    case WM_MBUTTONDOWN:
        if (window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::MIDDLE]) {
            window_for_message_callback->input.mouse_pressed[MOUSE_KEY_CODE::MIDDLE] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(MOUSE_KEY_CODE::MIDDLE, true, input)
        );
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::MIDDLE] = true;
        return 0;
    case WM_MBUTTONUP:
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::MIDDLE] = false;
        if (!window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::MIDDLE]) {
            window_for_message_callback->input.mouse_released[MOUSE_KEY_CODE::MIDDLE] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(MOUSE_KEY_CODE::MIDDLE, false, input)
        );
        return 0;
    case WM_RBUTTONDOWN:
        if (window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::RIGHT]) {
            window_for_message_callback->input.mouse_pressed[MOUSE_KEY_CODE::RIGHT] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(MOUSE_KEY_CODE::RIGHT, true, input)
        );
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::RIGHT] = true;
        return 0;
    case WM_RBUTTONUP:
        if (!window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::RIGHT]) {
            window_for_message_callback->input.mouse_released[MOUSE_KEY_CODE::RIGHT] = true;
        }
        input_add_mouse_message(input,
            mouse_message_make(MOUSE_KEY_CODE::RIGHT, false, input)
        );
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::RIGHT] = false;
        return 0;
    case WM_MOUSELEAVE: {
        //logg("MOUSE_LEAVE\n");
        ClipCursor(0);
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::LEFT] = false;
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::RIGHT] = false;
        window_for_message_callback->input.mouse_down[MOUSE_KEY_CODE::MIDDLE] = false;
        break;
    }
    case WM_MOUSEWHEEL:
    {
        int zDelta = GET_WHEEL_DELTA_WPARAM(wparam);
        window_for_message_callback->input.mouse_wheel_delta += zDelta / ((float)WHEEL_DELTA);
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
            window_for_message_callback->state.in_focus = false;
            ClipCursor(0);
            SetCursor(window_for_message_callback->cursor_default);
            window_cursor_update_contrain_rect(window_for_message_callback);
        }
        //logg("WM_ACTIVATE\n");
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
        Input* input = &window_for_message_callback->input;
        input->mouse_delta_x += (x - input->mouse_x);
        input->mouse_delta_y += (y - input->mouse_y);
        input->mouse_normalized_delta_x = (float)input->mouse_delta_x / window_for_message_callback->primary_monitor_width;
        input->mouse_normalized_delta_y = (float)input->mouse_delta_y / window_for_message_callback->primary_monitor_height;
        input->mouse_x = x;
        input->mouse_y = y;
        return 0;
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
            Input* input = &window_for_message_callback->input;
            memset(input->key_pressed, 0, KEYBOARD_KEY_COUNT);
            memset(input->mouse_pressed, 0, MOUSE_KEY_COUNT);
            memset(input->key_down, 0, KEYBOARD_KEY_COUNT);
            memset(input->key_down, 0, MOUSE_KEY_COUNT);
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
            window_for_message_callback->input.client_area_resized = true;
        }
        state->width = new_width;
        state->height = new_height;
        state->minimized = false;

        return 0;
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
        window_for_message_callback->input.close_request_issued = true;
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
    if (id == 131185 || id == 131218) return;
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
            WGL_CONTEXT_MINOR_VERSION_ARB, 5,
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
        //opengl_print_all_extensions((void*)&hdc);

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

        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(opengl_debug_callback, 0);

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
bool window_handle_messages(Window* window, bool block_until_next_message)
{
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

    MSG msg = {};
    if (!PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE)) // Check if a message exists
    {
        // No messages found
        if (block_until_next_message) { // Call GetMessage, which blocks
            if (!GetMessage(&msg, 0, 0, 0)) {
                return false; // WM_QUIT was sent
            }
            else { // Handle message received from GetMessage
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else { // Just return if we should not block
            return true;
        }
    }

    // Handle all remaining available messages
    while (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        if (msg.message == WM_QUIT) { // WM_QUIT was sent
            return false;
        }
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

void window_check_if_size_changed(Window* window)
{
    RECT window_rect;
    GetClientRect(window->hwnd, &window_rect);
    int x = window_rect.left;
    int y = window_rect.top;
    int width = window_rect.right - window_rect.left;
    int height = window_rect.bottom - window_rect.top;
    if (window->state.width != width || window->state.height != height) {
        window->input.client_area_resized = true;
        window->state.width = width;
        window->state.height = height;
    }
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
            SetCursor(window->cursor_default);
        }
        else {
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
    window->last_mouse_reset_pos_x = window_rect.left + width/2;
    window->last_mouse_reset_pos_y = window_rect.top + height/2;
    SetCursorPos(window->last_mouse_reset_pos_x, window->last_mouse_reset_pos_y);

}
