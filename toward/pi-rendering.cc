
#include "pi-rendering.h"

#include <cstdio>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "SDL.h"
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

// Buffers and Attributes
static PFNGLGENBUFFERSPROC glGenBuffers = nullptr;
static PFNGLBINDBUFFERPROC glBindBuffer = nullptr;
static PFNGLBUFFERDATAPROC glBufferData = nullptr;
static PFNGLBUFFERSUBDATAPROC glBufferSubData = nullptr;
static PFNGLDELETEBUFFERSPROC glDeleteBuffers = nullptr;

static PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = nullptr;
static PFNGLBINDVERTEXARRAYPROC glBindVertexArray = nullptr;
static PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = nullptr;

static PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer = nullptr;
static PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray = nullptr;
static PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation = nullptr;

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
  glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
  glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
  glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
  glBufferSubData =
      (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
  glDeleteBuffers =
      (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
  glVertexAttribPointer =
      (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
  glEnableVertexAttribArray =
      (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
  glBindAttribLocation =
      (PFNGLBINDATTRIBLOCATIONPROC)SDL_GL_GetProcAddress("glBindAttribLocation");
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

 static const char *kVertexShader = R"(#version 140
in vec2 pos;
in vec4 color;
uniform vec2 viewport_min;
uniform vec2 viewport_max;

out vec4 v_color;

void main() {
  vec2 size = viewport_max - viewport_min;
  vec2 ndc = ((pos - viewport_min) / size) * 2.0 - 1.0;

  // Convert from y-down to OpenGL's y-up
  gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
  v_color = color;
}
)";

static const char *kFragmentShader = R"(#version 140
in vec4 v_color;
out vec4 frag_color;

void main() {
  frag_color = v_color;
}
)";

static const char *kBgVertexShader = R"(#version 140
out vec2 v_texcoord;

void main() {
  float x = (gl_VertexID == 1) ? 3.0 : -1.0;
  float y = (gl_VertexID == 2) ? 3.0 : -1.0;
  v_texcoord = vec2((x + 1.0) * 0.5, 1.0 - (y + 1.0) * 0.5);
  gl_Position = vec4(x, y, 0.0, 1.0);
}
)";

static const char *kBgFragmentShader = R"(#version 140
in vec2 v_texcoord;
uniform sampler2D tex;
out vec4 frag_color;

void main() {
  vec4 c = texture(tex, v_texcoord);
  frag_color = vec4(c.rgb * c.a, c.a);
}
)";

struct PiRendering : public Rendering {

  static constexpr int SCREEN_WIDTH = 1920;
  static constexpr int SCREEN_HEIGHT = 1080;

  SDL_Window *window = nullptr;
  SDL_GLContext context = {};

  GLuint program = 0;
  GLuint vao = 0;
  GLuint vbo = 0;
  GLint loc_viewport_min = -1;
  GLint loc_viewport_max = -1;
  size_t vbo_capacity = 0;

  GLuint bg_program = 0;
  GLuint bg_vao = 0;
  GLuint bg_texture = 0;
  bool has_bg = false;
  GLint loc_bg_tex = -1;

  void Initialize() {
    Print("Started OK.\n");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);

    // Attempt to get SDL to put the window in the foreground.
    SDL_SetHint(SDL_HINT_FORCE_RAISEWINDOW, "1");

    window = SDL_CreateWindow(
        "Toward", SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        SCREEN_WIDTH, SCREEN_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
        SDL_WINDOW_FULLSCREEN_DESKTOP);
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

    int pixel_w = 0, pixel_h = 0;
    SDL_GL_GetDrawableSize(window, &pixel_w, &pixel_h);
    Print("Window pixel resolution: {}x{}\n", pixel_w, pixel_h);

    // Log the actual version you got
    int major, minor;
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
    SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
    Print(stderr, "Acquired OpenGL Context: {}.{}\n", major, minor);
    if (major < 3 || (major == 3 && minor < 1)) {
      LOG(FATAL) << "Requires OpenGL 3.1+, but got " << major
                 << "." << minor;
    }

    LoadGLFunctions();

    GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
    program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "pos");
    glBindAttribLocation(program, 1, "color");
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
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          (void*)(2 * sizeof(float)));

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

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    int vw = w;
    int vh = h;
    int vx = 0;
    int vy = 0;

    if (w * SCREEN_HEIGHT > h * SCREEN_WIDTH) {
      vw = h * SCREEN_WIDTH / SCREEN_HEIGHT;
      vx = (w - vw) / 2;
    } else if (w * SCREEN_HEIGHT < h * SCREEN_WIDTH) {
      vh = w * SCREEN_HEIGHT / SCREEN_WIDTH;
      vy = (h - vh) / 2;
    }

    glViewport(vx, vy, vw, vh);

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

    struct Vertex {
      float x, y, r, g, b, a;
    };
    std::vector<Vertex> vertices;
    vertices.reserve(scene.size() * 3);
    for (const auto &t : scene) {
      uint32_t c = t.rgba;
      float r = float((c >> 24u) & 0xFFu) / 255.0f;
      float g = float((c >> 16u) & 0xFFu) / 255.0f;
      float b = float((c >> 8u) & 0xFFu) / 255.0f;
      float a = float(c & 0xFFu) / 255.0f;
      r *= a; g *= a; b *= a;
      vertices.push_back({t.a.x, t.a.y, r, g, b, a});
      vertices.push_back({t.b.x, t.b.y, r, g, b, a});
      vertices.push_back({t.c.x, t.c.y, r, g, b, a});
    }

    size_t data_size = vertices.size() * sizeof(Vertex);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (data_size > vbo_capacity) {
      glBufferData(GL_ARRAY_BUFFER, data_size, vertices.data(),
                   GL_DYNAMIC_DRAW);
      vbo_capacity = data_size;
    } else {
      glBufferSubData(GL_ARRAY_BUFFER, 0, data_size, vertices.data());
    }

    glUseProgram(program);
    glUniform2f(loc_viewport_min, viewport_min.x, viewport_min.y);
    glUniform2f(loc_viewport_max, viewport_max.x, viewport_max.y);

    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)vertices.size());

    SDL_GL_SwapWindow(window);
  }

  vec2f ScreenToWorld(vec2f viewport_min,
                      vec2f viewport_max,
                      int x, int y) override {
    int w = 1, h = 1;
    SDL_GetWindowSize(window, &w, &h);

    int vw = w;
    int vh = h;
    int vx = 0;
    int vy = 0;

    if (w * SCREEN_HEIGHT > h * SCREEN_WIDTH) {
      vw = h * SCREEN_WIDTH / SCREEN_HEIGHT;
      vx = (w - vw) / 2;
    } else if (w * SCREEN_HEIGHT < h * SCREEN_WIDTH) {
      vh = w * SCREEN_HEIGHT / SCREEN_WIDTH;
      vy = (h - vh) / 2;
    }

    float tx = (x - vx) / (float)vw;
    float ty = (y - vy) / (float)vh;

    return vec2f{
      viewport_min.x + tx * (viewport_max.x - viewport_min.x),
      viewport_min.y + ty * (viewport_max.y - viewport_min.y),
    };
  }

  ~PiRendering() override {
    if (bg_texture) glDeleteTextures(1, &bg_texture);
    if (bg_vao) glDeleteVertexArrays(1, &bg_vao);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
  }
};

}  // namespace

std::unique_ptr<Rendering> CreatePiRendering() {
  auto ret = std::make_unique<PiRendering>();
  ret->Initialize();
  return ret;
}
