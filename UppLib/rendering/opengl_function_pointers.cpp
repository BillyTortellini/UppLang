#include "opengl_function_pointers.hpp"

#include <Windows.h>
#include <gl/GL.h>
#include "../utility/datatypes.hpp"
#include "../utility/utils.hpp"

HMODULE opengl_module = 0;
void* opengl_get_function_address(const char* name_handle) 
{
    void* function_address = (void*)wglGetProcAddress(name_handle);
    // Check if failed
    if ((i64)function_address <= 3 || function_address == (void*)-1) { // wglGetProcAddress failed
        if (opengl_module == 0) {
            opengl_module = LoadLibraryA("opengl32.dll");
            if (opengl_module == 0) panic("Could not load opengl!");
        }
        function_address = GetProcAddress(opengl_module, name_handle);
    }
    return function_address;
}

void opengl_print_all_extensions(void* hdc) {
    GLint extCount; 
    glGetIntegerv(GL_NUM_EXTENSIONS, &extCount);
    logg("Extensions:\n---------\n");
    for (int i = 0; i < extCount; i++)
    {
        const char* extension = (const char*) glGetStringi(GL_EXTENSIONS, (GLuint)i);
        logg("\t#%d: %s\n", i, extension);
    }
    logg("\n");

    const char* wglExtensions = (const char*) wglGetExtensionsStringARB(*((HDC*)hdc));
    logg("WGL Extensions:\n------------------%s\n----------------------\n", wglExtensions);
}

bool opengl_load_extensions()
{
    bool success = true;

    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)opengl_get_function_address("wglSwapIntervalEXT");
    wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)opengl_get_function_address("wglGetExtensionsStringARB");
    success = success &&
        (wglSwapIntervalEXT != NULL) &&
        (wglGetExtensionsStringARB != NULL);

    return success;
}

bool opengl_load_all_functions() 
{
    glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC) opengl_get_function_address("glDebugMessageCallback");
    glGenBuffers = (PFNGLGENBUFFERSPROC) opengl_get_function_address("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC) opengl_get_function_address("glBindBuffer");
    glGenBuffers = (PFNGLGENBUFFERSPROC) opengl_get_function_address("glGenBuffers");
    glBindBuffer = (PFNGLBINDBUFFERPROC) opengl_get_function_address("glBindBuffer");
    glBufferData = (PFNGLBUFFERDATAPROC) opengl_get_function_address("glBufferData");
    glBindBufferBase = (PFNGLBINDBUFFERBASEPROC) opengl_get_function_address("glBindBufferBase");
    glBindBufferRange = (PFNGLBINDBUFFERRANGEPROC) opengl_get_function_address("glBindBufferBase");
    glBufferSubData = (PFNGLBUFFERSUBDATAPROC) opengl_get_function_address("glBufferSubData");
    glDrawBuffers = (PFNGLDRAWBUFFERSPROC) opengl_get_function_address("glDrawBuffers");
    glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC) opengl_get_function_address("glVertexAttribPointer");
    glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC) opengl_get_function_address("glEnableVertexAttribArray");
    glVertexAttribDivisor = (PFNGLVERTEXATTRIBDIVISORPROC) opengl_get_function_address("glVertexAttribDivisor");
    glDrawElementsInstanced = (PFNGLDRAWELEMENTSINSTANCEDPROC) opengl_get_function_address("glDrawElementsInstanced");
    glCreateShader = (PFNGLCREATESHADERPROC) opengl_get_function_address("glCreateShader");
    glShaderSource = (PFNGLSHADERSOURCEPROC) opengl_get_function_address("glShaderSource");
    glCompileShader = (PFNGLCOMPILESHADERPROC) opengl_get_function_address("glCompileShader");
    glDeleteShader = (PFNGLDELETESHADERPROC) opengl_get_function_address("glDeleteShader");
    glCreateProgram = (PFNGLCREATEPROGRAMPROC) opengl_get_function_address("glCreateProgram");
    glDeleteProgram = (PFNGLDELETEPROGRAMPROC) opengl_get_function_address("glDeleteProgram");
    glAttachShader = (PFNGLATTACHSHADERPROC) opengl_get_function_address("glAttachShader");
    glDetachShader = (PFNGLDETACHSHADERPROC) opengl_get_function_address("glDetachShader");
    glLinkProgram = (PFNGLLINKPROGRAMPROC) opengl_get_function_address("glLinkProgram");
    glGetShaderiv = (PFNGLGETSHADERIVPROC) opengl_get_function_address("glGetShaderiv");
    glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC) opengl_get_function_address("glGetShaderInfoLog");
    glGetProgramiv = (PFNGLGETPROGRAMIVPROC) opengl_get_function_address("glGetProgramiv");
    glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC) opengl_get_function_address("glGetProgramInfoLog");
    glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC) opengl_get_function_address("glGenVertexArrays");
    glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC) opengl_get_function_address("glBindVertexArray");
    glUseProgram = (PFNGLUSEPROGRAMPROC) opengl_get_function_address("glUseProgram");
    //glViewport = (PFNGLVIEWPORTPROC) opengl_get_function_address("glViewport");
    glGetActiveUniform = (PFNGLGETACTIVEUNIFORMPROC) opengl_get_function_address("glGetActiveUniform");
    glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC) opengl_get_function_address("glGetUniformLocation");
    glUniform1f = (PFNGLUNIFORM1FPROC) opengl_get_function_address("glUniform1f");
    glUniform2f = (PFNGLUNIFORM2FPROC) opengl_get_function_address("glUniform2f");
    glUniform3f = (PFNGLUNIFORM3FPROC) opengl_get_function_address("glUniform3f");
    glUniform4f = (PFNGLUNIFORM4FPROC) opengl_get_function_address("glUniform4f");
    glUniform1i = (PFNGLUNIFORM1IPROC) opengl_get_function_address("glUniform1i");
    glUniform2i = (PFNGLUNIFORM2IPROC) opengl_get_function_address("glUniform2i");
    glUniform3i = (PFNGLUNIFORM3IPROC) opengl_get_function_address("glUniform3i");
    glUniform4i = (PFNGLUNIFORM4IPROC) opengl_get_function_address("glUniform4i");
    glUniform1ui = (PFNGLUNIFORM1UIPROC) opengl_get_function_address("glUniform1ui");
    glUniform2ui = (PFNGLUNIFORM2UIPROC) opengl_get_function_address("glUniform2ui");
    glUniform3ui = (PFNGLUNIFORM3UIPROC) opengl_get_function_address("glUniform3ui");
    glUniform4ui = (PFNGLUNIFORM4UIPROC) opengl_get_function_address("glUniform4ui");
    glUniform1fv = (PFNGLUNIFORM1FVPROC) opengl_get_function_address("glUniform1fv");
    glUniform2fv = (PFNGLUNIFORM2FVPROC) opengl_get_function_address("glUniform2fv");
    glUniform3fv = (PFNGLUNIFORM3FVPROC) opengl_get_function_address("glUniform3fv");
    glUniform4fv = (PFNGLUNIFORM4FVPROC) opengl_get_function_address("glUniform4fv");
    glUniform1iv = (PFNGLUNIFORM1IVPROC) opengl_get_function_address("glUniform1iv");
    glUniform2iv = (PFNGLUNIFORM2IVPROC) opengl_get_function_address("glUniform2iv");
    glUniform3iv = (PFNGLUNIFORM3IVPROC) opengl_get_function_address("glUniform3iv");
    glUniform4iv = (PFNGLUNIFORM4IVPROC) opengl_get_function_address("glUniform4iv");
    glUniform1uiv = (PFNGLUNIFORM1UIVPROC) opengl_get_function_address("glUniform1uiv");
    glUniform2uiv = (PFNGLUNIFORM2UIVPROC) opengl_get_function_address("glUniform2uiv");
    glUniform3uiv = (PFNGLUNIFORM3UIVPROC) opengl_get_function_address("glUniform3uiv");
    glUniform4uiv = (PFNGLUNIFORM4UIVPROC) opengl_get_function_address("glUniform4uiv");
    glUniformMatrix2fv = (PFNGLUNIFORMMATRIX2FVPROC) opengl_get_function_address("glUniformMatrix2fv");
    glUniformMatrix3fv = (PFNGLUNIFORMMATRIX3FVPROC) opengl_get_function_address("glUniformMatrix3fv");
    glUniformMatrix4fv = (PFNGLUNIFORMMATRIX4FVPROC) opengl_get_function_address("glUniformMatrix4fv");
    glUniformMatrix2x3fv = (PFNGLUNIFORMMATRIX2X3FVPROC) opengl_get_function_address("glUniformMatrix2x3fv");
    glUniformMatrix3x2fv = (PFNGLUNIFORMMATRIX3X2FVPROC) opengl_get_function_address("glUniformMatrix3x2fv");
    glUniformMatrix2x4fv = (PFNGLUNIFORMMATRIX2X4FVPROC) opengl_get_function_address("glUniformMatrix2x4fv");
    glUniformMatrix4x2fv = (PFNGLUNIFORMMATRIX4X2FVPROC) opengl_get_function_address("glUniformMatrix4x2fv");
    glUniformMatrix3x4fv = (PFNGLUNIFORMMATRIX3X4FVPROC) opengl_get_function_address("glUniformMatrix3x4fv");
    glUniformMatrix4x3fv = (PFNGLUNIFORMMATRIX4X3FVPROC) opengl_get_function_address("glUniformMatrix4x3fv");
    glGetStringi = (PFNGLGETSTRINGIPROC) opengl_get_function_address("glGetStringi");
    glDeleteBuffers = (PFNGLDELETEBUFFERSPROC) opengl_get_function_address("glDeleteBuffers");
    glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC) opengl_get_function_address("glDeleteVertexArrays");
    glGetActiveAttrib = (PFNGLGETACTIVEATTRIBPROC) opengl_get_function_address("glGetActiveAttrib");
    glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC) opengl_get_function_address("glGetAttribLocation");
    glGenerateMipmap = (PFNGLGENERATEMIPMAPPROC) opengl_get_function_address("glGenerateMipmap");
    glActiveTexture = (PFNGLACTIVETEXTUREPROC) opengl_get_function_address("glActiveTexture");
    glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC) opengl_get_function_address("glGenFramebuffers");
    glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC) opengl_get_function_address("glBindFramebuffer");
    glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC) opengl_get_function_address("glDeleteFramebuffers");
    glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC) opengl_get_function_address("glCheckFramebufferStatus");
    glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC) opengl_get_function_address("glFramebufferTexture2D");
    glFramebufferRenderbuffer = (PFNGLFRAMEBUFFERRENDERBUFFERPROC) opengl_get_function_address("glFramebufferRenderbuffer");
    glGenRenderbuffers = (PFNGLGENRENDERBUFFERSPROC) opengl_get_function_address("glGenRenderbuffers");
    glDeleteRenderbuffers = (PFNGLDELETERENDERBUFFERSPROC) opengl_get_function_address("glDeleteRenderbuffers");
    glBindRenderbuffer = (PFNGLBINDRENDERBUFFERPROC) opengl_get_function_address("glBindRenderbuffer");
    glRenderbufferStorage = (PFNGLRENDERBUFFERSTORAGEPROC) opengl_get_function_address("glRenderbufferStorage");
    //glTexSubImage2D = (PFNGLTEXSUBIMAGE2DPROC)opengl_get_function_address("glTexSubImage2D");
    glBlendEquation = (PFNGLBLENDEQUATIONPROC)opengl_get_function_address("glBlendEquation");
    glBlendColor = (PFNGLBLENDCOLORPROC)opengl_get_function_address("glBlendColor");

    bool success = true;
    success = success &&
        (glDebugMessageCallback != NULL) &&
        (glGenBuffers != NULL) &&
        (glBindBuffer != NULL) &&
        (glBindBufferBase != NULL) &&
        (glBindBufferRange != NULL) &&
        (glBufferData != NULL) &&
        (glBufferSubData != NULL) &&
        (glVertexAttribPointer != NULL) &&
        (glDrawElementsInstanced != NULL) &&
        (glEnableVertexAttribArray != NULL) &&
        (glCreateShader != NULL) &&
        (glShaderSource != NULL) &&
        (glCompileShader != NULL) &&
        (glDrawBuffers != NULL) &&
        (glDeleteShader != NULL) &&
        (glVertexAttribDivisor != NULL) &&
        (glCreateProgram != NULL) &&
        (glDeleteProgram != NULL) &&
        (glAttachShader != NULL) &&
        (glDetachShader != NULL) &&
        (glLinkProgram != NULL) &&
        (glGetShaderiv != NULL) &&
        (glGetShaderInfoLog != NULL) &&
        (glGetProgramiv != NULL) &&
        (glGenVertexArrays != NULL) &&
        (glBindVertexArray != NULL) &&
        (glUseProgram != NULL) &&
        (glGetActiveUniform != NULL) &&
        (glGetUniformLocation != NULL) &&
        (glUniform1f != NULL) &&
        (glUniform2f != NULL) &&
        (glUniform3f != NULL) &&
        (glUniform4f != NULL) &&
        (glUniform1i != NULL) &&
        (glUniform2i != NULL) &&
        (glUniform3i != NULL) &&
        (glUniform4i != NULL) &&
        (glUniform1ui != NULL) &&
        (glUniform2ui != NULL) &&
        (glUniform3ui != NULL) &&
        (glUniform4ui != NULL) &&
        (glUniform1fv != NULL) &&
        (glUniform2fv != NULL) &&
        (glUniform3fv != NULL) &&
        (glUniform4fv != NULL) &&
        (glUniform1iv != NULL) &&
        (glUniform2iv != NULL) &&
        (glUniform3iv != NULL) &&
        (glUniform4iv != NULL) &&
        (glUniform1uiv != NULL) &&
        (glUniform2uiv != NULL) &&
        (glUniform3uiv != NULL) &&
        (glUniform4uiv != NULL) &&
        (glUniformMatrix2fv != NULL) &&
        (glUniformMatrix3fv != NULL) &&
        (glUniformMatrix4fv != NULL) &&
        (glUniformMatrix2x3fv != NULL) &&
        (glUniformMatrix3x2fv != NULL) &&
        (glUniformMatrix2x4fv != NULL) &&
        (glUniformMatrix4x2fv != NULL) &&
        (glUniformMatrix3x4fv != NULL) &&
        (glUniformMatrix4x3fv != NULL) &&
        (glGetStringi != NULL) &&
        (glDeleteBuffers != NULL) &&
        (glDeleteVertexArrays != NULL) &&
        (glGetActiveAttrib != NULL) &&
        (glGetAttribLocation != NULL) &&
        //        (glViewport != NULL) &&
        (glGetProgramInfoLog != NULL) &&
        (glGenerateMipmap != NULL) &&
        (glActiveTexture != NULL) &&
        (glGenFramebuffers != NULL) &&
        (glBindFramebuffer != NULL) &&
        (glDeleteFramebuffers != NULL) &&
        (glCheckFramebufferStatus != NULL) &&
        (glFramebufferTexture2D != NULL) &&
        (glFramebufferRenderbuffer != NULL) &&
        (glGenRenderbuffers != NULL) &&
        (glDeleteRenderbuffers != NULL) &&
        (glBindRenderbuffer != NULL) &&
        (glRenderbufferStorage != NULL) &&
        //(glTexSubImage2D != NULL) &&
        (glBlendColor != NULL) &&
        (glBlendEquation != NULL);

    // Load extensions
    success = success && opengl_load_extensions();

    return success;
}

/*
    FUNCTION POINTERS
*/

// Debug functions
PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;
// Buffer functions
PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
PFNGLGENBUFFERSPROC glGenBuffers;
PFNGLBINDBUFFERPROC glBindBuffer;
PFNGLBUFFERDATAPROC glBufferData;
PFNGLBUFFERSUBDATAPROC glBufferSubData;
PFNGLBINDBUFFERBASEPROC glBindBufferBase;
PFNGLBINDBUFFERRANGEPROC glBindBufferRange;
PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
PFNGLVERTEXATTRIBDIVISORPROC glVertexAttribDivisor;
PFNGLUSEPROGRAMPROC glUseProgram;
PFNGLDRAWBUFFERSPROC glDrawBuffers;
// Drawing 
//PFNGLDRAWARRAYSPROC glDrawArrays;
//PFNGLDRAWELEMENTSPROC glDrawElements;
PFNGLDRAWELEMENTSINSTANCEDPROC glDrawElementsInstanced;
// Shader creation
PFNGLCREATESHADERPROC glCreateShader;
PFNGLSHADERSOURCEPROC glShaderSource;
PFNGLCOMPILESHADERPROC glCompileShader;
PFNGLDELETESHADERPROC glDeleteShader;
PFNGLCREATEPROGRAMPROC glCreateProgram;
PFNGLDELETEPROGRAMPROC glDeleteProgram;
PFNGLATTACHSHADERPROC glAttachShader;
PFNGLDETACHSHADERPROC glDetachShader;
PFNGLLINKPROGRAMPROC glLinkProgram;
//PFNGLVIEWPORTPROC glViewport;
// Get infos
PFNGLGETSHADERIVPROC glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
PFNGLGETPROGRAMIVPROC glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
// Uniform stuff
PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform;
PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
PFNGLUNIFORM1FPROC glUniform1f;
PFNGLUNIFORM2FPROC glUniform2f;
PFNGLUNIFORM3FPROC glUniform3f;
PFNGLUNIFORM4FPROC glUniform4f;
PFNGLUNIFORM1IPROC glUniform1i;
PFNGLUNIFORM2IPROC glUniform2i;
PFNGLUNIFORM3IPROC glUniform3i;
PFNGLUNIFORM4IPROC glUniform4i;
PFNGLUNIFORM1UIPROC glUniform1ui;
PFNGLUNIFORM2UIPROC glUniform2ui;
PFNGLUNIFORM3UIPROC glUniform3ui;
PFNGLUNIFORM4UIPROC glUniform4ui;
PFNGLUNIFORM1FVPROC glUniform1fv;
PFNGLUNIFORM2FVPROC glUniform2fv;
PFNGLUNIFORM3FVPROC glUniform3fv;
PFNGLUNIFORM4FVPROC glUniform4fv;
PFNGLUNIFORM1IVPROC glUniform1iv;
PFNGLUNIFORM2IVPROC glUniform2iv;
PFNGLUNIFORM3IVPROC glUniform3iv;
PFNGLUNIFORM4IVPROC glUniform4iv;
PFNGLUNIFORM1UIVPROC glUniform1uiv;
PFNGLUNIFORM2UIVPROC glUniform2uiv;
PFNGLUNIFORM3UIVPROC glUniform3uiv;
PFNGLUNIFORM4UIVPROC glUniform4uiv;
PFNGLUNIFORMMATRIX2FVPROC glUniformMatrix2fv;
PFNGLUNIFORMMATRIX3FVPROC glUniformMatrix3fv;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;
PFNGLUNIFORMMATRIX2X3FVPROC glUniformMatrix2x3fv;
PFNGLUNIFORMMATRIX3X2FVPROC glUniformMatrix3x2fv;
PFNGLUNIFORMMATRIX2X4FVPROC glUniformMatrix2x4fv;
PFNGLUNIFORMMATRIX4X2FVPROC glUniformMatrix4x2fv;
PFNGLUNIFORMMATRIX3X4FVPROC glUniformMatrix3x4fv;
PFNGLUNIFORMMATRIX4X3FVPROC glUniformMatrix4x3fv;
PFNGLGETACTIVEATTRIBPROC glGetActiveAttrib;
PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
// Delete Stuff
PFNGLDELETEBUFFERSPROC glDeleteBuffers;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
// String stuff
PFNGLGETSTRINGIPROC glGetStringi;
// EXTENSIONS
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB;
// Textures and framebuffers
PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
PFNGLACTIVETEXTUREPROC glActiveTexture;
PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
//PFNGLTEXSUBIMAGE2DPROC glTexSubImage2D;
// Blending
 PFNGLBLENDEQUATIONPROC glBlendEquation;
 PFNGLBLENDCOLORPROC glBlendColor;
