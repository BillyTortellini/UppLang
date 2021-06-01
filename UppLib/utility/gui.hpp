#pragma once

#include "../math/umath.hpp"
#include "../datastructures/string.hpp"

enum class Anchor_2D 
{
    TOP_LEFT, TOP_CENTER, TOP_RIGHT,
    BOTTOM_LEFT, BOTTOM_CENTER, BOTTOM_RIGHT,
    CENTER_LEFT, CENTER_CENTER, CENTER_RIGHT
};

// Forward Declarations
struct Input;
struct Window_State;
struct Renderer_2D;
struct Rendering_Core;

struct GUI
{
    vec2 mouse_pos; // Normalized
    vec2 mouse_pos_last_frame;
    bool mouse_down_last_frame;
    bool mouse_down_this_frame;

    bool element_in_focus;
    bool draw_in_focus;
    vec2 focused_size;
    vec2 focused_pos;

    bool backspace_was_down = false;
    double backspace_down_time;
    String text_in_edit;
    String numeric_input_buffer;
    float current_depth = 0.99f;

    Input* input;
    Window_State* window_state;
    Renderer_2D* renderer_2d;
};

GUI gui_create(Renderer_2D* renderer_2d, Window_State* window_state, Input* input);
void gui_destroy(GUI* gui);
void gui_update(GUI* gui, Input* input, Window_State* window_state);
void gui_render(GUI* gui, Rendering_Core* core);
float gui_next_depth(GUI* gui);
void gui_set_focus(GUI* gui, vec2 pos, vec2 size);
bool gui_is_in_focus(GUI* gui, vec2 pos, vec2 size);
vec2 gui_calculate_text_size(GUI* gui, int char_count, float height);

struct GUI_Position {
    vec2 pos;
    vec2 size;
};

GUI_Position gui_position_make(vec2 pos, vec2 size);
GUI_Position gui_position_make_neighbour(GUI_Position origin, Anchor_2D anchor, vec2 size);
GUI_Position gui_position_make_on_window_border(GUI* gui, vec2 size, Anchor_2D anchor);
GUI_Position gui_position_make_inside(GUI_Position parent, Anchor_2D anchor, vec2 size);

bool gui_checkbox(GUI* gui, vec2 pos, vec2 size, bool* value);
bool gui_checkbox(GUI* gui, GUI_Position pos, bool* value);
bool gui_slider(GUI* gui, GUI_Position pos, float* value, float min, float max) ;
void gui_label(GUI* gui, GUI_Position pos, const char* text);
void gui_label_float(GUI* gui, GUI_Position pos, float f);
bool gui_text_input_string(GUI* gui, String* to_fill, vec2 pos, vec2 size, bool only_write_on_enter, bool clear_on_focus);
bool gui_text_input_string(GUI* gui, String* to_fill, GUI_Position pos, bool only_write_on_enter, bool clear_on_focus);
bool gui_text_input_int(GUI* gui, vec2 pos, vec2 size, int* value);
bool gui_text_input_int(GUI* gui, GUI_Position pos, int* value);
bool gui_text_input_float(GUI* gui, vec2 pos, vec2 size, float* value);
bool gui_text_input_float(GUI* gui, GUI_Position pos, float* value);
bool gui_button(GUI* gui, vec2 pos, vec2 size, const char* text);
bool gui_button(GUI* gui, GUI_Position pos, const char* text);

/* 
TODO:
    GUI:
     - Object placement in windows
     - Text renderer with depth
     - Movable windows -> Handling overlaps
     - Popup-like stuff, or toasts (Like on mobile phones)
     - Rerouting logg output to GUI-Elements Display
     - Variable tweaker (Search bar and input stuff)
     - Config for GUI-style (Colors, sizes...)
*/

/*
 GUI machen:
 -----------
 Was will ich genau?
    Einfach benutzbare, minimalistische GUI library, mit der ich elemente auf den screen platzieren kann.
    USE_CASES:
        - Ein Parameter tuner, mit dem ich parameter dynamisch setzen kann
            - integer, floats, vectoren, booleans, strings
        - Einfache Menüs, mit denen ich variablen überwachen und bearbeiten kann (E.g. Camera pos/direction anzeigen)
            Normalerweise Structs anschauen können, die werte bearbeiten (Eventuell auch mit parameter tuner)
        - Ausgabe von Error messages
  
    Was braucht die library:
        Grundbausteine:
        - Buttons (With text)
        - Labels (String ausgabe)
        - Text Input (Numerisch/String input)
        - Drop-Down liste
        - Slider (Für numerische Values)
        - Text Area (Großes feld, wo man text bearbeiten kann)

        Advanced Stuff:
        - Layout Optionen (Grid layout, other stuff?)
        - Text passt size an, nicht umgekehrt
        - Overlay handling
        - Scrollbars
        - Images/Icons
        - Drag-and-Drop-able items
        - Movable Windows
        - Popups
        - Antialiasing, schönere Effekte

Meilensteine:
    - Slider
    - Checkboxes
    - Dropdown?
    - Buttons respond to click mit animation
    - Mehrere Buttons, welche überlappen können und nicht scheiße sind (Eventuell Layer system)
    //- Movable Buttons mit rechtsclick -> in imgui müsste dann der caller die pos speichern -> eventuell movable window?

Implementiert:
    - Text input (Braucht focus mechanik)
    - Single Button, der auf Button-Press reagiert
        - Einfärbiges Rechteck
        - Statische Position auf Bildschirm
        - Print wenn der Button gepresst wird
    - Hover-Feature für button implementieren
    - Text innerhalb des Buttons (Zentriert) zeichnen, mit text cutoff!
    - Aspect ratio und size zeug festlegen
    

 Wie implementiere ich dass?
    ALLGEMEIN: Functionen über Aussehen

 Wie werde ich den Button implementieren?
    Ich will sowas haben wie
        GUI gui = gui_create(...);

        // Start of frame
        gui_update(&gui, input);

        // Irgendwo (Einmal pro frame)
        bool button_pressed = gui_button(&gui, size = vec2(0.1, 0.2), pos = vec2(0, 0))

        // End of frame
        gui_draw(&gui);

    Problem: GUI call (gui_button) hat keinen input als übergabe, deswegen muss es mouse-clicks am anfang des frames speichern
        Ich muss mal nachschauen wie ich zurzeit maus input mache

        Überlappende elemente werden doppelt angeschprochen

    Frage:
        Wie werden Größen/Positionen angegeben? Wie interagieren die Sachen mit aspekt ratios? 

        Wie schauen normale Use-Cases aus?
            User definiert Fenster, in dem die elemente platziert werden
            Wenn ich size = 0.1/0.2 angebe, dann will ich dass es x anhand von y skaliert wird        

        Approaches für Größen:
            - Fixe Größen in Physikalischer Größe, unabhängig von Window Size:
                + Immer gleich gut benutzbar, weil immer gleich groß
                + Einfache Berechnung
                - Nicht responsive, bei kleinen Fenstern ist alles zu groß, viele überlappende sachen
            - Größen relativ zu Bildschirmhöhe
                + Unterschiedlich groß
                - Bei großen Fenster zu große Elemente
            - Kombinierter Approch, physikalische größe, außer bei zu kleinen Fenster
                + Mehr zeug zum angeben

            Größen relativ zu kleinster Bildschirmdimension -> Geht das überhaupt mit GUI?
                Input: Box größe x/y
                
            Eine prozentangabe zur kleinsten Bildschirm-Dimension (Also hauptsächlich Höhe)
            Wieso/was macht das?
        
 */


/* 
I want to have order independet rendering of primitives:
Primitives:
    * Rectangles
    * Text
    * Lines 2D
    * Circles
    * Lines 3D

Advanced stuff:
    Anti aliasing for everything
    
Implementation:
    * First implement depth with colored rectangles
    * Depth for text rendering
*/


// Actually gui stuff is actually just drawing rectangles and text
// To clean this up, i think i just want to have a 2d drawing library, 
// I also want to have order independent rendering

