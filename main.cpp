// clang-format off
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// imgui
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// clang-format on
#include <algorithm>
#include <cmath>
#include <iostream>
#include <queue>
#include <vector>

// Dear ImGui custom menu for the program
void imGuiMenu();

// initializing functions
void handleCanvasResize(int width,
                        int height); // since textureID is global now, we can

// Create a canvas since its independent from the window now
void createCanvas(int width, int height);

// just use width and height
void framebuffer_size_callback(GLFWwindow *window, int width, int height);

ImVec2 screenToCanvas(ImVec2 s);

void fitCanvasToWindow(GLFWwindow *window);

void updateCamera(ImVec2 mouse);

void processInput(GLFWwindow *window);

ImVec4 getColor(int x, int y);

bool colorsMatch(ImVec4 c1, ImVec4 c2);

void floodFill(int startX, int startY, ImVec4 targetColor,
               ImVec4 replacementColor);

void plot(int x, int y);

void plot(int x, int y, ImVec4 color);

void plotCell(int cellX, int cellY);

void plotCell(int cellX, int cellY, ImVec4 color);

void plotLineLow(int x0, int y0, int x1, int y1, ImVec4 color);

void plotLineHigh(int x0, int y0, int x1, int y1, ImVec4 color);

void drawLine(int x0, int y0, int x1, int y1, ImVec4 color);

void bresCircle(int xc, int yc, int r);

void bresElipse(int xc, int yc, int r1, int r2);

void drawCircle(int xc, int yc, int x, int y);

void drawRectangle(int x0, int y0, int x1, int y1, ImVec4 color);

void drawTriangle(int x0, int y0, int x1, int y1, ImVec4 color);

float mainMenuBar();

//---------------------------------------------
// GLOBAL VARIABLES
//---------------------------------------------
std::vector<unsigned char> canvasData;
GLuint textureID;

// Dynamic variables for the size of the canvas
int canvasWidth = 192;
int canvasHeight = 108;

// settings
const unsigned int SCR_WIDTH = 192;
const unsigned int SCR_HEIGHT = 108;

const unsigned int WINDOW_WIDTH = 1280;
const unsigned int WINDOW_HEIGHT = 720;

// unused for now
const int CELL_SIZE = 10; // Size of every block in the grid

// Camera global variables
float zoom = 1.0f;
ImVec2 pan = {0.0f, 0.0f};

// MOUSE COORDINATES FOR DRAWING LOGIC
int startY = 0;
int startX = 0;
int endY = 0;
int endX = 0;

// Left and Right click variables, currently only using left click
bool firstLeftClick = false;
bool firstRightClick = false;

// Color variable
static ImVec4 color =
    ImVec4(0.0f / 255.0f, 0.0f / 255.0f, 0.0f / 255.0f, 255.0f / 255.0f);

// Button variables
bool freeDrawing = true;
bool line = false;
bool circle = false;
bool rectangle = false;
bool triangle = false;
bool fill = false;

// Shaders
const char *vertexShaderSource =
    "#version 460 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "uniform float uZoom;\n"
    "uniform vec2 uPan;\n"
    "uniform vec2 uViewport;\n"
    "out vec2 TexCoord;\n"
    "void main()\n"
    "{\n"
    "   vec2 screen = aPos * uZoom + uPan;\n"
    "   vec2 ndc = (screen / uViewport) * 2.0 - 1.0;\n"
    "   gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);\n"
    "   TexCoord = aTexCoord;\n"
    "}\0";

const char *fragmentShaderSource =
    "#version 460 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D ourTexture;\n"
    "void main()\n"
    "{\n"
    "   FragColor = texture(ourTexture, TexCoord);\n"
    "}\n\0";

int main() {
  using namespace std;

  // glfw: initialize and configure
  // ------------------------------
  glfwInit();
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  // glfw window creation
  // --------------------
  GLFWwindow *window = glfwCreateWindow(WINDOW_WIDTH, WINDOW_HEIGHT,
                                        "Drawing program V2", NULL, NULL);
  if (window == NULL) {
    std::cout << "Failed to create GLFW window" << std::endl;
    glfwTerminate();
    return -1;
  }
  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  // imgui context
  // --------------------
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // Setup Platform/Renderer backends
  ImGui_ImplGlfw_InitForOpenGL(
      window, true); // Second param install_callback=true will install GLFW
  // callbacks and chain to existing ones.
  ImGui_ImplOpenGL3_Init();
  bool open = true;

  // glad: load all OpenGL function pointers
  // ---------------------------------------
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

  // Build and compile shaders
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
  glCompileShader(vertexShader);

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
  glCompileShader(fragmentShader);

  GLuint shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);

  // Clean up individual shader objects after linking
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);

  // SETTING UP GEOMETRY (Quad, VBO, VAO)
  float W = (float)canvasWidth, H = (float)canvasHeight;
  float quadVertices[] = {
      // pos     // uv
      0.0f, 0.0f, 0.0f, 0.0f, // top-left
      0.0f, H,    0.0f, 1.0f, // bottom-left
      W,    H,    1.0f, 1.0f, // bottom-right

      0.0f, 0.0f, 0.0f, 0.0f, // top-left
      W,    H,    1.0f, 1.0f, // bottom-right
      W,    0.0f, 1.0f, 0.0f  // top-right
  };

  //---------------------------------------
  // TEXTURES, VBO AND VAO
  //---------------------------------------
  GLint zoomLoc = glGetUniformLocation(shaderProgram, "uZoom");
  GLint panLoc = glGetUniformLocation(shaderProgram, "uPan");
  GLint viewportLoc = glGetUniformLocation(shaderProgram, "uViewport");
  GLuint VAO, VBO;
  glGenVertexArrays(1, &VAO);
  glGenBuffers(1, &VBO);

  glBindVertexArray(VAO);

  glBindBuffer(GL_ARRAY_BUFFER, VBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices,
               GL_STATIC_DRAW);

  // Position Attribute (location = 0)
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(0);

  // TexCoord Attribute (location = 1)
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                        (void *)(2 * sizeof(float)));
  glEnableVertexAttribArray(1);

  // Setting up canvas texture

  textureID;                               // Texture declaration
  glGenTextures(1, &textureID);            // ID and vector/pointer of textures
  glBindTexture(GL_TEXTURE_2D, textureID); // We bind the texture to GPU VRAM

  // Set texture filtering (GL_NEAREST for crisp textures)

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  createCanvas(canvasWidth, canvasHeight);
  fitCanvasToWindow(window);
  // handleCanvasResize(SCR_WIDTH, SCR_HEIGHT);

  // RENDER LOOP
  // -----------
  while (!glfwWindowShouldClose(window)) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuiStyle &style = ImGui::GetStyle();
    // ImGui::ShowDemoWindow(); // Show demo window! :)
    imGuiMenu();

    // input
    processInput(window);

    // render
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);

    glClearColor(0.18f, 0.18f, 0.18f, 1.0f); // dark gray around the canvas
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(shaderProgram);
    glUniform1f(zoomLoc, zoom);
    glUniform2f(panLoc, pan.x, pan.y);
    glUniform2f(viewportLoc, (float)winW, (float)winH);

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, canvasWidth, canvasHeight, GL_RGB,
                    GL_UNSIGNED_BYTE, canvasData.data());
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved
    // etc.)
    glfwSwapBuffers(window);
    glfwPollEvents();
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);
  glDeleteProgram(shaderProgram);

  // glfw: terminate, clearing all previously allocated GLFW resources.
  glfwTerminate();
  return 0;
}

void imGuiMenu() {
  //---------------------------------------
  // My ImGUI window!!
  //---------------------------------------
  float menuBarHeight = mainMenuBar();
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;

  ImGui::SetNextWindowSize(
      ImVec2(displaySize.x * 0.12f, displaySize.y - menuBarHeight));
  ImGui::SetNextWindowPos(
      ImVec2(displaySize.x - displaySize.x * 0.12f, menuBarHeight));
  ImGui::Begin("Paintlike UI", NULL,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoResize);
  // ImGui::Text("Color Picker and shapes v1");
  ImVec2 buttonWidth = ImVec2(-1.0f, 0.0f);

  ImGui::SeparatorText("Drawing Options");
  if (ImGui::RadioButton("Free Drawing", freeDrawing)) {
    freeDrawing = true;
    line = false;
    circle = false;
    rectangle = false;
    triangle = false;
    fill = false;
  }
  if (ImGui::RadioButton("Line", line)) {
    freeDrawing = false;
    line = true;
    circle = false;
    rectangle = false;
    triangle = false;
    fill = false;
  }
  if (ImGui::RadioButton("Circle", circle)) {
    freeDrawing = false;
    line = false;
    circle = true;
    rectangle = false;
    triangle = false;
    fill = false;
  }
  if (ImGui::RadioButton("Rectangle", rectangle)) {
    freeDrawing = false;
    line = false;
    circle = false;
    rectangle = true;
    triangle = false;
    fill = false;
  }
  if (ImGui::RadioButton("Triangle", triangle)) {
    freeDrawing = false;
    line = false;
    circle = false;
    rectangle = false;
    triangle = true;
    fill = false;
  }
  if (ImGui::RadioButton("Fill", fill)) {
    freeDrawing = false;
    line = false;
    circle = false;
    rectangle = false;
    triangle = false;
    fill = true;
  }

  float w = (ImGui::GetContentRegionAvail().x);
  // ImGui::ColorPicker3("Select your color!!!", (float*)&color,
  // ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoAlpha);
  ImGui::SeparatorText("Color Options");
  ImGui::SetNextItemWidth(w);
  ImGui::ColorPicker3(
      "", (float *)&color,
      ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);

  ImGui::End();
  std::cout << "Coordinates set in: "
            << (displaySize.x * 0.12f, displaySize.y - menuBarHeight)
            << std::endl;
}

ImVec2 screenToCanvas(ImVec2 s) {
  return {(s.x - pan.x) / zoom, (s.y - pan.y) / zoom};
}

void fitCanvasToWindow(GLFWwindow *window) {
  int w, h;
  glfwGetWindowSize(window, &w, &h);

  float menuH = ImGui::GetFrameHeight();
  float availW = w - w * 0.12f;
  float availH = h - menuH;

  zoom = std::min(availW / canvasWidth, availH / canvasHeight) * 0.9f;
  pan.x = (availW - canvasWidth * zoom) * 0.5f;
  pan.y = menuH + (availH - canvasHeight * zoom) * 0.5f;
}

void updateCamera(ImVec2 mouse) {
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse)
    return;

  if (io.MouseWheel != 0.0f) {
    ImVec2 before = screenToCanvas(mouse);
    zoom = std::clamp(zoom * powf(1.1f, io.MouseWheel), 0.5f, 64.0f);
    pan.x = mouse.x - before.x * zoom;
    pan.y = mouse.y - before.y * zoom;
  }

  if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    pan.x += io.MouseDelta.x;
    pan.y += io.MouseDelta.y;
  }
}

// process all input: query GLFW whether relevant keys are pressed/released this
// frame and react accordingly
void processInput(GLFWwindow *window) {
  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
    glfwSetWindowShouldClose(window, true);
  }
  if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) {
  }

  double dXpos, dYpos;
  glfwGetCursorPos(window, &dXpos, &dYpos);
  // int xpos = (int)dXpos;
  // int ypos = (int)dYpos;

  ImVec2 mouse = {(float)dXpos, (float)dYpos};
  updateCamera(mouse);

  if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS)
    fitCanvasToWindow(window);

  ImVec2 c = screenToCanvas(mouse);
  int cellX = (int)floorf(c.x);
  int cellY = (int)floorf(c.y);

  // static bool lastRightClickMouseState = false;
  static bool lastLeftClickMouseState = false;

  if (ImGui::GetIO().WantCaptureMouse == true) {
  } else {
    // CURSOR POSITION IN GRID LOGIC
    bool currentLeftClickMouseState =
        (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS);

    if (freeDrawing == true) {
      // FREE DRAWING INPUT AND LOGIC (doesnt require a function)
      static int lastX = 0;
      static int lastY = 0;

      if (currentLeftClickMouseState) {
        if (!lastLeftClickMouseState) {
          lastX = cellX;
          lastY = cellY;
          plot(lastX, lastY, color);
        } else {
          drawLine(lastX, lastY, cellX, cellY, color);
          lastX = cellX;
          lastY = cellY;
        }
      }
      lastLeftClickMouseState = currentLeftClickMouseState;
    } else if (line == true) {
      // LINE DRAWING INPUT
      if (currentLeftClickMouseState && !lastLeftClickMouseState) {
        if (!firstLeftClick) {
          startX = cellX;
          startY = cellY;
          firstLeftClick = true;

          plot(startX, startY, color);

          std::cout << "Initial tile: (" << startX << ", " << startY << ")\n";
        } else {
          endX = cellX;
          endY = cellY;
          drawLine(startX, startY, endX, endY, color);
          firstLeftClick = false;

          std::cout << "Line drawn till tile: (" << endX << ", " << endY
                    << ")\n";
        }
      }
      lastLeftClickMouseState = currentLeftClickMouseState;
    } else if (circle == true) {
      // CIRCLE DRAWING INPUT
      if (currentLeftClickMouseState && !lastLeftClickMouseState) {
        if (!firstLeftClick) {
          startX = cellX;
          startY = cellY;
          firstLeftClick = true;

          plot(startX, startY, color);

          std::cout << "Initial point: (" << startX << ", " << startY << ")\n";
        } else {
          endX = cellX;
          endY = cellY;
          // Parse and calculation of the radius
          int radius = (int)(sqrt(((endX - startX) * (endX - startX)) +
                                  ((endY - startY) * (endY - startY))));
          bresCircle(startX, startY, radius);
          firstLeftClick = false;

          std::cout << "Circle drawn in: (" << endX << ", " << endY << ")\n";
        }
      }
      lastLeftClickMouseState = currentLeftClickMouseState;
    } else if (rectangle == true) {
      // RECTANGLE DRAWING INPUT
      if (currentLeftClickMouseState && !lastLeftClickMouseState) {
        if (!firstLeftClick) {
          startX = cellX;
          startY = cellY;
          firstLeftClick = true;

          plot(startX, startY, color);

          std::cout << "Initial tile: (" << startX << ", " << startY << ")\n";
        } else {
          endX = cellX;
          endY = cellY;
          drawRectangle(startX, startY, endX, endY, color);
          firstLeftClick = false;

          std::cout << "Rectangle drawn till tile: (" << endX << ", " << endY
                    << ")\n";
        }
      }
      lastLeftClickMouseState = currentLeftClickMouseState;
    } else if (triangle == true) {
      // TRIANGLE DRAWING INPUT
      if (currentLeftClickMouseState && !lastLeftClickMouseState) {
        if (!firstLeftClick) {
          startX = cellX;
          startY = cellY;
          firstLeftClick = true;

        } else {
          endX = cellX;
          endY = cellY;
          drawTriangle(startX, startY, endX, endY, color);
          firstLeftClick = false;
        }
      }

    } else if (fill == true) {
      // FILL DRAWING INPUT
      startX = cellX;
      startY = cellY;

      if (currentLeftClickMouseState && !lastLeftClickMouseState) {
        if (startX >= 0 && startX < canvasWidth && startY >= 0 &&
            startY < canvasHeight)
          floodFill(startX, startY, getColor(startX, startY), color);
      }
    }
    lastLeftClickMouseState = currentLeftClickMouseState;
  }
}

void handleCanvasResize(int width, int height) {
  canvasWidth = width;
  canvasHeight = height;
  // we resize the vector when needed
  canvasData.resize(width * height * 3);

  // Fill the vector with 255 (White)
  std::fill(canvasData.begin(), canvasData.end(), 255);

  for (int y = 0; y < canvasHeight; y++) {
    for (int x = 0; x < canvasWidth; x++) {
      int index = (y * canvasWidth + x) * 3;

      int cellX = x / CELL_SIZE;
      int cellY = y / CELL_SIZE;

      // Checkerboard texture, unused now, fill doesnt work with this on
      if ((cellX + cellY) % 2 == 0) {
        canvasData[index + 0] = 255;
        canvasData[index + 1] = 255;
        canvasData[index + 2] = 255;
      } else {
        canvasData[index + 0] = 255;
        canvasData[index + 1] = 255;
        canvasData[index + 2] = 255;
      }
    }
  }

  // Fix of grid not working properly, gives allignment to the grid
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Bind and reallocate GPU texture
  // Upload the resized vector data to the GPU (vibe coded)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
               GL_UNSIGNED_BYTE, canvasData.data());
}

void createCanvas(int width, int height) {
  canvasWidth = width;
  canvasHeight = height;
  canvasData.assign(width * height * 3, 255); // white

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glBindTexture(GL_TEXTURE_2D, textureID);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB,
               GL_UNSIGNED_BYTE, canvasData.data());
}

// glfw: whenever the window size changed (by OS or user resize) this callback
// function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  // make sure the viewport matches the new window dimensions; note that width
  // and height will be significantly larger than specified on retina displays.
  glViewport(0, 0, width, height);

  // handleCanvasResize(width, height);
}

void drawLine(int x0, int y0, int x1, int y1, ImVec4 color) {
  // Check if the line is horizontal-leaning (|slope| <= 1) or vertical-leaning
  // (|slope| > 1)
  if (abs(y1 - y0) < abs(x1 - x0)) {
    // Low slope: step along X
    // Ensure we always draw from left to right (x0 <= x1)
    if (x0 > x1) {
      plotLineLow(x1, y1, x0, y0, color);
    } else {
      plotLineLow(x0, y0, x1, y1, color);
    }
  } else {
    // High slope: step along Y
    // Ensure we always draw from bottom to top (y0 <= y1)
    if (y0 > y1) {
      plotLineHigh(x1, y1, x0, y0, color);
    } else {
      plotLineHigh(x0, y0, x1, y1, color);
    }
  }
}

// function for plotting in the lower parts of the octant
void plotLineLow(int x0, int y0, int x1, int y1, ImVec4 color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int yi = 1;

  // handle downward slopes
  if (dy < 0) {
    yi = -1;
    dy = -dy;
  }

  int D = (2 * dy) - dx;
  int y = y0;

  // drive the loop along the y axis
  for (int x = x0; x <= x1; x++) {
    plot(x, y, color);

    if (D > 0) {
      y += yi;
      D += 2 * (dy - dx);
    } else {
      D += 2 * dy;
    }
  }
}

void plotLineHigh(int x0, int y0, int x1, int y1, ImVec4 color) {
  int dx = x1 - x0;
  int dy = y1 - y0;
  int xi = 1;

  // handle upward slopes
  if (dx < 0) {
    xi = -1;
    dx = -dx;
  }

  int D = (2 * dx) - dy;
  int x = x0;

  // drive the loop along the x axis
  for (int y = y0; y <= y1; y++) {
    plot(x, y, color);

    if (D > 0) {
      x += xi;
      D += 2 * (dx - dy);
    } else {
      D += 2 * dx;
    }
  }
}

// So we can get color information of a tile
ImVec4 getColor(int x, int y) {
  if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight)
    return ImVec4(0, 0, 0, 1);
  int index = (y * canvasWidth + x) * 3; // no flip
  return ImVec4(canvasData[index] / 255.0f, canvasData[index + 1] / 255.0f,
                canvasData[index + 2] / 255.0f, 1.0f);
}

// So we can compare colors
bool colorsMatch(ImVec4 c1, ImVec4 c2) {
  return std::abs(c1.x - c2.x) < 0.01f && std::abs(c1.y - c2.y) < 0.01f &&
         std::abs(c1.z - c2.z) < 0.01f;
}

void floodFill(int startX, int startY, ImVec4 targetColor,
               ImVec4 replacementColor) {
  if (colorsMatch(targetColor, replacementColor))
    return;

  std::queue<std::pair<int, int>> q;
  q.push({startX, startY});
  plot(startX, startY, replacementColor);

  int dx[] = {1, -1, 0, 0};
  int dy[] = {0, 0, 1, -1};

  while (!q.empty()) {
    auto [cx, cy] = q.front();
    q.pop();

    for (int i = 0; i < 4; i++) {
      int nx = cx + dx[i];
      int ny = cy + dy[i];

      if (nx >= 0 && nx < canvasWidth && ny >= 0 && ny < canvasHeight) {
        if (colorsMatch(getColor(nx, ny), targetColor)) {
          plot(nx, ny, replacementColor);
          q.push({nx, ny});
        }
      }
    }
  }
}

void plot(int x, int y, ImVec4 color) {
  if (x < 0 || x >= canvasWidth || y < 0 || y >= canvasHeight)
    return;

  int index = (y * canvasWidth + x) * 3;
  canvasData[index + 0] = (unsigned char)(color.x * 255.0f);
  canvasData[index + 1] = (unsigned char)(color.y * 255.0f);
  canvasData[index + 2] = (unsigned char)(color.z * 255.0f);
}

void drawCircle(int xc, int yc, int x, int y) {
  plot(xc + x, yc + y, color);
  plot(xc - x, yc + y, color);
  plot(xc - x, yc - y, color);
  plot(xc + x, yc - y, color);
  plot(xc + y, yc + x, color);
  plot(xc - y, yc + x, color);
  plot(xc - y, yc - x, color);
  plot(xc + y, yc - x, color);
}

void bresCircle(int xc, int yc, int r) {
  int x = 0, y = r;
  int d = 3 - (2 * r);
  drawCircle(xc, yc, x, y);

  while (y >= x) {
    x++;
    if (d > 0) {
      y--;
      d = d + 4 * (x - y) + 10;
    } else {
      d = d + 4 * x + 6;
    }
    drawCircle(xc, yc, x, y);
  }
}

void drawRectangle(int x0, int y0, int x1, int y1, ImVec4 color) {
  drawLine(x0, y0, x0, y1, color);
  drawLine(x0, y0, x1, y0, color);
  drawLine(x1, y0, x1, y1, color);
  drawLine(x0, y1, x1, y1, color);
}

void drawTriangle(int x0, int y0, int x1, int y1, ImVec4 color) {
  int left = std::min(x0, x1);
  int right = std::max(x0, x1);
  int top = std::min(y0, y1);
  int bottom = std::max(y0, y1);
  int apexX = (left + right) / 2;

  drawLine(left, bottom, right, bottom, color);
  drawLine(left, bottom, apexX, top, color);
  drawLine(right, bottom, apexX, top, color);
}

float mainMenuBar() {
  float menuHeight = 0.0f;

  if (ImGui::BeginMainMenuBar()) {
    menuHeight = ImGui::GetWindowSize().y;

    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New", "Ctrl+N")) {
      }
      if (ImGui::MenuItem("Open", "Ctrl+O")) {
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
  return menuHeight;
}
