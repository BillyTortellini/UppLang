#include "system_clipboard.hpp"

#include <Windows.h>

bool clipboard_store_text(String string)
{
    // Copy data into clippboard using Win32
    if (!OpenClipboard(NULL)) {
        return false;
    }
    SCOPE_EXIT(CloseClipboard());
    EmptyClipboard();

    HGLOBAL global_handle = GlobalAlloc(GMEM_MOVEABLE, string.size + 1);
    SCOPE_EXIT(if (global_handle != nullptr) { GlobalFree(global_handle); });
    if (global_handle == nullptr) {
        return false;
    }

    u8* data_ptr = (u8*)GlobalLock(global_handle);
    if (data_ptr == nullptr) {
        return false;
    }
    data_ptr[string.size] = '\0';
    memory_copy(data_ptr, string.characters, string.size + 1);
    GlobalUnlock(global_handle);

    SetClipboardData(CF_TEXT, global_handle);
    return true;
}

bool clipboard_load_text(String* string)
{
    if (!IsClipboardFormatAvailable(CF_TEXT)) {
        return false;
    }

    if (!OpenClipboard(NULL)) {
        return false;
    }
    SCOPE_EXIT(CloseClipboard());

    HGLOBAL global_handle = GetClipboardData(CF_TEXT);
    if (global_handle == nullptr) return false;
    u8* data = (u8*)GlobalLock(global_handle);
    if (data == nullptr) return false;
    SCOPE_EXIT(GlobalUnlock(global_handle));

    string_reset(string);
    string_append(string, (const char*)data);
    return true;
}
