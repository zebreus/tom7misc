
#include "rendering.h"

#include <cstdio>
#include <format>
#include <memory>
#include <span>
#include <string>

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_opengl.h"
#include "SDL_video.h"
#include "base/logging.h"
#include "base/print.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
#include "GL/glext.h"

#ifndef GL_COMPILE_STATUS
#error These should come from GL/glext.h ?
#endif

// Pointers to the modern OpenGL functions we will use
static PFNGLCREATESHADERPROC glCreateShader = nullptr;
static PFNGLSHADERSOURCEPROC glShaderSource = nullptr;
static PFNGLCOMPILESHADERPROC glCompileShader = nullptr;
static PFNGLGETSHADERIVPROC glGetShaderiv = nullptr;
static PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog = nullptr;
static PFNGLCREATEPROGRAMPROC glCreateProgram = nullptr;
static PFNGLATTACHSHADERPROC glAttachShader = nullptr;
static PFNGLLINKPROGRAMPROC glLinkProgram = nullptr;
static PFNGLGETPROGRAMIVPROC glGetProgramiv = nullptr;
static PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog = nullptr;
static PFNGLDELETESHADERPROC glDeleteShader = nullptr;
static PFNGLUSEPROGRAMPROC glUseProgram = nullptr;
static PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation = nullptr;
static PFNGLUNIFORM1FPROC glUniform1f = nullptr;
static PFNGLUNIFORM2FPROC glUniform2f = nullptr;
static PFNGLUNIFORM1IPROC glUniform1i = nullptr;

// SSBOs
static PFNGLBINDBUFFERBASEPROC glBindBufferBase = nullptr;
static PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC glBufferData = nullptr;
static PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;

static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;

static void LoadGLFunctions() {
  glCreateShader =
      (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
  glShaderSource =
      (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
  glCompileShader =
      (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
  glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
  glGetShaderInfoLog =
      (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
  glCreateProgram =
      (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
  glAttachShader =
      (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
  glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
  glGetProgramiv =
      (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
  glGetProgramInfoLog =
      (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
  glDeleteShader =
      (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
  glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
  glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress(
      "glGetUniformLocation");
  glUniform1f = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
  glUniform2f = (PFNGLUNIFORM2FPROC)SDL_GL_GetProcAddress("glUniform2f");
  glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");

  glGenVertexArrays =
      (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
  glBindVertexArray =
      (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
  glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)SDL_GL_GetProcAddress(
      "glDeleteVertexArrays");
  glBindBufferBase =
      (PFNGLBINDBUFFERBASEPROC)SDL_GL_GetProcAddress("glBindBufferBase");
  glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
  glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
  glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
  glBufferSubData =
      (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
  glDeleteBuffers =
      (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
}

namespace {

GLuint CompileShader(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint success = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char info_log[512];
    glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
    LOG(FATAL) << "Shader compilation failed:\n" << info_log;
  }
  return shader;
}

static const char *kVertexShader = R"(#version 430 core
struct Triangle {
  vec2 a, b, c;
  uint rgba;
  uint reserved;
};
layout(std430, binding = 0) buffer SceneData {
  Triangle triangles[];
};
uniform vec2 viewport_min;
uniform vec2 viewport_max;

out vec4 v_color;

void main() {
  uint tri_id = gl_VertexID / 3;
  uint v_id = gl_VertexID % 3;

  Triangle t = triangles[tri_id];
  vec2 pos;
  if (v_id == 0) pos = t.a;
  else if (v_id == 1) pos = t.b;
  else pos = t.c;

  vec2 size = viewport_max - viewport_min;
  vec2 ndc = ((pos - viewport_min) / size) * 2.0 - 1.0;

  gl_Position = vec4(ndc, 0.0, 1.0);

  uint c = t.rgba;
  float r = float(c & 0xFFu) / 255.0;
  float g = float((c >> 8u) & 0xFFu) / 255.0;
  float b = float((c >> 16u) & 0xFFu) / 255.0;
  float a = float((c >> 24u) & 0xFFu) / 255.0;
  v_color = vec4(r, g, b, a);
}
)";

static const char *kFragmentShader = R"(#version 430 core
in vec4 v_color;
out vec4 frag_color;

void main() {
  frag_color = v_color;
}
)";

struct SDLGLRendering : public Rendering {

  SDL_Window *window = nullptr;
  SDL_GLContext context = {};

  GLuint program = 0;
  GLuint vao = 0;
  GLuint ssbo = 0;
  GLint loc_viewport_min = -1;
  GLint loc_viewport_max = -1;
  size_t ssbo_capacity = 0;

  void Initialize() {
    Print("Started OK.\n");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    window = SDL_CreateWindow("Toward", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, 1920, 1080,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
      std::string err = std::format("Failed to create window: {}",
                                    SDL_GetError());
      LOG(FATAL) << "Couldn't initialize SDL.\n" << err;
    }

    context = SDL_GL_CreateContext(window);
    if (!context) {
      std::string err =
        std::format("Failed to create OpenGL context: {}", SDL_GetError());
      SDL_DestroyWindow(window);
      LOG(FATAL) << "Couldn't initialize SDL.\n" << err;
    }

    // Log the actual version you got
    int major, minor;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
    Print(stderr, "Acquired OpenGL Context: {}.{}\n", major, minor);
    if (major < 4 || (major == 4 && minor < 3)) {
      LOG(FATAL) << "SSBOs require OpenGL 4.3+, but got " << major
                 << "." << minor;
    }

    LoadGLFunctions();

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    GLint success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
      char info_log[512];
      glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
      LOG(FATAL) << "Program link failed:\n" << info_log;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);

    loc_viewport_min = glGetUniformLocation(program, "viewport_min");
    loc_viewport_max = glGetUniformLocation(program, "viewport_max");

    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &ssbo);

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  }

  void RenderScene(vec2f viewport_min,
                   vec2f viewport_max,
                   std::span<const Triangle> scene) override {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    glViewport(0, 0, w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (scene.empty()) {
      SDL_GL_SwapWindow(window);
      return;
    }

    size_t data_size = scene.size_bytes();
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    if (data_size > ssbo_capacity) {
      glBufferData(GL_SHADER_STORAGE_BUFFER, data_size, scene.data(),
                   GL_DYNAMIC_DRAW);
      ssbo_capacity = data_size;
    } else {
      glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, data_size, scene.data());
    }
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

    glUseProgram(program);
    glUniform2f(loc_viewport_min, viewport_min.x, viewport_min.y);
    glUniform2f(loc_viewport_max, viewport_max.x, viewport_max.y);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(scene.size() * 3));

    SDL_GL_SwapWindow(window);
  }

  ~SDLGLRendering() override {
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &ssbo);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
  }
};

}  // namespace

Rendering::~Rendering() {}

std::unique_ptr<Rendering> Rendering::CreateSDLGL() {
  auto ret = std::make_unique<SDLGLRendering>();
  ret->Initialize();
  return ret;
}
