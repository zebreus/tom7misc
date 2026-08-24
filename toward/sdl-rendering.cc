
#include "sdl-rendering.h"

#include <cstdio>
#include <format>
#include <memory>
#include <span>
#include <string>

#include "SDL.h"  // IWYU pragma: keep
#include "SDL_error.h"
#include "SDL_hints.h"
#include "SDL_opengl.h"
#include "SDL_stdinc.h"
#include "SDL_video.h"
#include "base/logging.h"
#include "base/print.h"
#include "image.h"
#include "rendering.h"

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
static PFNGLACTIVETEXTUREPROC glActiveTextureProc = nullptr;

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
  glActiveTextureProc =
      (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");

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

  // Convert from y-down to OpenGL's y-up
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);

  uint c = t.rgba;
  float r = float((c >> 24u) & 0xFFu) / 255.0;
  float g = float((c >> 16u) & 0xFFu) / 255.0;
  float b = float((c >> 8u) & 0xFFu) / 255.0;
  float a = float(c & 0xFFu) / 255.0;
  // Using premultiplied alpha.
  v_color = vec4(r * a, g * a, b * a, a);
}
)";

static const char *kFragmentShader = R"(#version 430 core
in vec4 v_color;
out vec4 frag_color;

void main() {
  frag_color = v_color;
}
)";

static const char *kBgVertexShader = R"(#version 430 core
out vec2 v_texcoord;

void main() {
  float x = (gl_VertexID == 1) ? 3.0 : -1.0;
  float y = (gl_VertexID == 2) ? 3.0 : -1.0;
  v_texcoord = vec2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
  gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

static const char *kBgFragmentShader = R"(#version 430 core
in vec2 v_texcoord;
uniform sampler2D tex;
out vec4 frag_color;

void main() {
  vec4 c = texture(tex, v_texcoord);
  frag_color = vec4(c.rgb * c.a, c.a);
}
)";

struct SDLGLRendering : public Rendering {

  static constexpr int SCREEN_WIDTH = 1920;
  static constexpr int SCREEN_HEIGHT = 1080;

  SDL_Window *window = nullptr;
  SDL_GLContext context = {};

  GLuint program = 0;
  GLuint vao = 0;
  GLuint ssbo = 0;
  GLint loc_viewport_min = -1;
  GLint loc_viewport_max = -1;
  size_t ssbo_capacity = 0;

  GLuint bg_program = 0;
  GLuint bg_vao = 0;
  GLuint bg_texture = 0;
  bool has_bg = false;
  GLint loc_bg_tex = -1;

  void Initialize() {
    Print("Started OK.\n");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    // Attempt to get SDL to put the window in the foreground.
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");

    window = SDL_CreateWindow("Toward", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED,
                              SCREEN_WIDTH, SCREEN_HEIGHT,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) {
      std::string err = std::format("Failed to create window: {}",
                                    SDL_GetError());
      LOG(FATAL) << "Couldn't initialize SDL.\n" << err;
    }

    // Trick to try to get the window in the foreground.
    SDL_SetWindowAlwaysOnTop(window, SDL_TRUE);
    SDL_SetWindowAlwaysOnTop(window, SDL_FALSE);
    SDL_RaiseWindow(window);

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

    GLuint bg_vs = CompileShader(GL_VERTEX_SHADER, kBgVertexShader);
    GLuint bg_fs = CompileShader(GL_FRAGMENT_SHADER, kBgFragmentShader);
    bg_program = glCreateProgram();
    glAttachShader(bg_program, bg_vs);
    glAttachShader(bg_program, bg_fs);
    glLinkProgram(bg_program);
    glGetProgramiv(bg_program, GL_LINK_STATUS, &success);
    if (!success) {
      char info_log[512];
      glGetProgramInfoLog(bg_program, sizeof(info_log), nullptr, info_log);
      LOG(FATAL) << "BG Program link failed:\n" << info_log;
    }
    glDeleteShader(bg_vs);
    glDeleteShader(bg_fs);

    loc_viewport_min = glGetUniformLocation(program, "viewport_min");
    loc_viewport_max = glGetUniformLocation(program, "viewport_max");
    loc_bg_tex = glGetUniformLocation(bg_program, "tex");

    glGenVertexArrays(1, &vao);
    glGenVertexArrays(1, &bg_vao);
    glGenBuffers(1, &ssbo);

    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    // Premultiplied alpha.
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }

  void SetBackground(const ImageRGBA &img) override {
    if (img.Width() == 0 || img.Height() == 0) {
      ClearBackground();
      return;
    }
    if (!bg_texture) {
      glGenTextures(1, &bg_texture);
    }
    glBindTexture(GL_TEXTURE_2D, bg_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.Width(), img.Height(), 0,
                 GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, img.data().data());
    has_bg = true;
  }

  void ClearBackground() override {
    has_bg = false;
  }

  void RenderScene(vec2f viewport_min,
                   vec2f viewport_max,
                   std::span<const Triangle> scene) override {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    glViewport(0, 0, w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (has_bg) {
      glUseProgram(bg_program);
      if (glActiveTextureProc) glActiveTextureProc(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, bg_texture);
      glUniform1i(loc_bg_tex, 0);

      glBindVertexArray(bg_vao);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }

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

  vec2f ScreenToWorld(vec2f viewport_min,
                      vec2f viewport_max,
                      int x, int y) override {
    // There is also...
    // SDL_GetWindowSize(window, &w, &h);

    float tx = x / (float)SCREEN_WIDTH;
    float ty = y / (float)SCREEN_HEIGHT;

    vec2f c{
      .x = viewport_min.x + tx * (viewport_max.x - viewport_min.x),
      .y = viewport_min.y + ty * (viewport_max.y - viewport_min.y),
    };

    #if 0
    Print("\n\nx {} y {}\n"
          "{} + {} * ({} - {})\n"
          "{} + {} * ({} - {})\n"
          " = ({}, {})\n\n",
          x, y,
          viewport_min.x, tx, viewport_max.x, viewport_min.x,
          viewport_min.y, ty, viewport_max.y, viewport_min.y,
          c.x, c.y);
    fflush(stdout);
    #endif

    return c;
  }

  ~SDLGLRendering() override {
    if (bg_texture) glDeleteTextures(1, &bg_texture);
    if (bg_vao) glDeleteVertexArrays(1, &bg_vao);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &ssbo);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
  }
};

}  // namespace

std::unique_ptr<Rendering> CreateSDLGLRendering() {
  auto ret = std::make_unique<SDLGLRendering>();
  ret->Initialize();
  return ret;
}
