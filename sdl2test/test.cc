
#include <iostream>
#include <cmath>
#include <vector>

#include "SDL.h"
#include "SDL_error.h"
#include "SDL_events.h"
#include "SDL_keycode.h"
#include "SDL_main.h"
#include "SDL_opengl.h"
#include "SDL_timer.h"
#include "SDL_video.h"
#include "base/logging.h"
#include "base/print.h"
#include "arcfour.h"

#define GL_GLEXT_PROTOTYPES 1
#include <GL/gl.h>
// #include "glext.h"

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
  glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
  glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
  glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
  glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
  glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
  glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
  glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
  glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
  glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
  glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
  glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
  glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
  glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
  glUniform1f = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
  glUniform2f = (PFNGLUNIFORM2FPROC)SDL_GL_GetProcAddress("glUniform2f");
  glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");

  glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glGenVertexArrays");
  glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)SDL_GL_GetProcAddress("glBindVertexArray");
  glDeleteVertexArrays = (PFNGLDELETEVERTEXARRAYSPROC)SDL_GL_GetProcAddress("glDeleteVertexArrays");
  glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)SDL_GL_GetProcAddress("glBindBufferBase");
  glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
  glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
  glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
  glBufferSubData = (PFNGLBUFFERSUBDATAPROC)SDL_GL_GetProcAddress("glBufferSubData");
  glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
}

struct SpriteData {
  uint32_t x, y, w, h;
};

const char* vertexShaderSource = R"(
  #version 430 core

  // We will generate a fullscreen quad in the vertex shader directly!
  // No VBOs or VAOs needed for this simple case.
  // The positions of the 6 vertices for two triangles forming a quad.
  const vec2 positions[6] = vec2[](
      vec2(-1.0, -1.0),
      vec2( 1.0, -1.0),
      vec2( 1.0,  1.0),

      vec2(-1.0, -1.0),
      vec2( 1.0,  1.0),
      vec2(-1.0,  1.0)
  );

  out vec2 v_fragCoord;

  void main() {
      // gl_VertexID is a built-in variable that contains the index of the current vertex.
      // We use it to pick a position from our hardcoded array.
      vec2 pos = positions[gl_VertexID];
      gl_Position = vec4(pos, 0.0, 1.0);

      // Pass the coordinate to the fragment shader, mapping from (-1,1) to (0,1)
      v_fragCoord = pos * 0.5 + 0.5;
  }
)";

const char* fragmentShaderSource = R"(
  #version 430 core // IMPORTANT: We must request 4.3 for SSBOs

  in vec2 v_fragCoord;
  out vec4 out_color;

  uniform float u_time;
  uniform vec2 u_resolution;
  uniform int u_sprite_count;

  // This is the C++ struct, mirrored in GLSL
  struct SpriteData {
      uint x, y, w, h; // Note: In GLSL, uint16_t is not a type.
                       // Data is promoted to 32-bit uints.
                       // The std430 layout handles this correctly.
  };

  // This is the SSBO block declaration.
  // 'std430' is the memory layout. 'binding = 0' is the slot we bind it to.
  layout(std430, binding = 0) buffer SpriteBuffer {
      SpriteData sprites[]; // An unsized array, just like OpenCL
  };

  void main() {
      vec2 pixel_coord = v_fragCoord * u_resolution;
      vec4 final_color = vec4(0.1, 0.1, 0.15, 1.0);

      for (int i = 0; i < u_sprite_count; i++) {
          // Direct, simple, array-like access!
          uint x = sprites[i].x;
          uint y = sprites[i].y;
          uint w = sprites[i].w;
          uint h = sprites[i].h;

          if (pixel_coord.x >= x && pixel_coord.x < x + w &&
              pixel_coord.y >= y && pixel_coord.y < y + h)
          {
              float r = float(i % 10) / 10.0;
              float g = float((i / 10) % 10) / 10.0;
              float b = 1.0;
              final_color = vec4(r, g, b, 1.0);
          }
      }

      out_color = final_color;
  }
)";

int main(int argc, char* argv[]) {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return -1;
  }

  Print("Started OK.\n");

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window *window = SDL_CreateWindow(
      "Pixel Shader Demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800,
      600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    SDL_Quit();
    return -1;
  }

  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context) {
    std::cerr << "Failed to create OpenGL context: " << SDL_GetError()
              << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return -1;
  }


  // Log the actual version you got
  int major, minor;
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &major);
  SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &minor);
  std::cout << "Acquired OpenGL Context: " << major << "." <<
    minor << std::endl;
  if (major < 4 || (major == 4 && minor < 3)) {
    std::cerr << "SSBOs require OpenGL 4.3+. Exiting." << std::endl;
    return -1;
  }

  LoadGLFunctions();


  // Compile the shaders.
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);

  // Error checking for shader compilation
  GLint success;
  GLchar infoLog[512];
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
    LOG(FATAL) << "Vertex shader failed to compile:\n"
               << infoLog << std::endl;
  }

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(fragmentShader, 512, NULL, infoLog);
    std::cerr << "Fragment shader failed to compile:\n"
              << infoLog << std::endl;
  }

  // LINK SHADER PROGRAM
  GLuint shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);

  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    glGetProgramInfoLog(shaderProgram, 512, NULL, infoLog);
    std::cerr << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n"
              << infoLog << std::endl;
  }

  // The individual shader objects are no longer needed after linking
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  GLint spriteCountLoc = glGetUniformLocation(shaderProgram, "u_sprite_count");

  // Get uniform locations
  GLint timeLoc = glGetUniformLocation(shaderProgram, "u_time");
  GLint resLoc = glGetUniformLocation(shaderProgram, "u_resolution");

  GLuint VAO;
  glGenVertexArrays(1, &VAO);
  glBindVertexArray(VAO);

  // --- SET UP THE SHADER STORAGE BUFFER OBJECT ---
  const int MAX_SPRITES = 10000;
  GLuint ssbo;
  glGenBuffers(1, &ssbo);
  // Bind it to the specific SSBO target
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
  glBufferData(GL_SHADER_STORAGE_BUFFER, MAX_SPRITES * sizeof(SpriteData), nullptr, GL_DYNAMIC_DRAW);

  // Bind the SSBO to "binding point" 0. This connects the buffer object
  // to the `binding = 0` declaration in the shader.
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ssbo);

  // Unbind from the general target, but it stays bound to the indexed binding point.
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

  std::vector<SpriteData> sprites;
  ArcFour rc("test");

  // MAIN LOOP
  bool running = true;
  while (running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT ||
          (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      }
    }

    sprites.clear();
    for (int i = 0; i < 1000; i++) {
      SpriteData s;
      s.x = rc.Byte();
      s.y = rc.Byte();
      s.w = 5;
      s.h = 4;
      sprites.push_back(s);
    }

    // Bind the buffer to the general target to update it
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sprites.size() * sizeof(SpriteData), sprites.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Get window size for the resolution uniform
    int width, height;
    SDL_GetWindowSize(window, &width, &height);
    glViewport(0, 0, width, height);

    // Clear the screen
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Use our shader
    glUseProgram(shaderProgram);
    glUniform1i(spriteCountLoc, sprites.size());

    // Update uniforms
    glUniform1f(timeLoc, SDL_GetTicks() / 1000.0f);
    glUniform2f(resLoc, (float)width, (float)height);

    // Draw our fullscreen quad!
    // This will run the vertex shader 6 times. Thanks to gl_VertexID,
    // it generates the correct positions without any buffers.
    glDrawArrays(GL_TRIANGLES, 0, 6);

    SDL_GL_SwapWindow(window);
  }

  glDeleteBuffers(1, &ssbo);
  glDeleteVertexArrays(1, &VAO);
  SDL_GL_DeleteContext(context);
  SDL_DestroyWindow(window);
  SDL_Quit();

  return 0;
}
