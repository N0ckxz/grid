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
#include <cstdlib>
#include <iostream>
#include <queue>
#include <utility>
#include <vector>

// From the STB libraries for C++
// (PNG export is disabled if stb_image_write.h isn't found)
#if __has_include("stb_image_write.h")
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define HAVE_STB 1
#else
#define HAVE_STB 0
#endif

// =============================================================================
// CONTROLS
//   Scroll wheel ... zoom toward the cursor
//   Middle drag .... pan
//   F .............. fit canvas to window
//   Select tool .... drag = select, drag inside = move, Enter = commit,
//                    Delete = erase selection, panel buttons = transforms
//   Esc ............ quit
// =============================================================================

//---------------------------------------------
// CONSTANTS
//---------------------------------------------
const unsigned int WINDOW_WIDTH = 1280;
const unsigned int WINDOW_HEIGHT = 720;
const int CH = 4; // RGBA, 4 bytes per pixel
const float PANEL_FRACTION =
    0.12f; // right-hand panel width (fraction of window)

//---------------------------------------------
// 2D TRANSFORM MATRIX (homogeneous coordinates)
//---------------------------------------------
// 3x3 matrix: translate, scale, rotate, shear. A * B means "apply B first".
struct Mat3 {
  float m[3][3];

  static Mat3 identity() { return {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}; }
  static Mat3 translate(float tx, float ty) {
    return {{{1, 0, tx}, {0, 1, ty}, {0, 0, 1}}};
  }
  static Mat3 scale(float sx, float sy) {
    return {{{sx, 0, 0}, {0, sy, 0}, {0, 0, 1}}};
  }
  static Mat3 rotate(float rad) {
    float c = cosf(rad), s = sinf(rad);
    return {{{c, -s, 0}, {s, c, 0}, {0, 0, 1}}};
  }
  static Mat3 shear(float shx, float shy) {
    return {{{1, shx, 0}, {shy, 1, 0}, {0, 0, 1}}};
  }

  Mat3 operator*(const Mat3 &b) const {
    Mat3 r{};
    for (int i = 0; i < 3; i++)
      for (int j = 0; j < 3; j++)
        for (int k = 0; k < 3; k++)
          r.m[i][j] += m[i][k] * b.m[k][j];
    return r;
  }

  // Transforms a point
  ImVec2 apply(ImVec2 p) const {
    return {m[0][0] * p.x + m[0][1] * p.y + m[0][2],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2]};
  }

  // Inverse of an affine matrix (bottom row is 0 0 1)
  Mat3 inverse() const {
    float a = m[0][0], b = m[0][1], c = m[0][2];
    float d = m[1][0], e = m[1][1], f = m[1][2];
    float det = a * e - b * d;
    if (fabsf(det) < 1e-6f)
      return identity();
    float id = 1.0f / det;
    Mat3 r = identity();
    r.m[0][0] = e * id;
    r.m[0][1] = -b * id;
    r.m[1][0] = -d * id;
    r.m[1][1] = a * id;
    r.m[0][2] = -(r.m[0][0] * c + r.m[0][1] * f);
    r.m[1][2] = -(r.m[1][0] * c + r.m[1][1] * f);
    return r;
  }
};

//---------------------------------------------
// GLOBAL STATE
//---------------------------------------------

// The canvas: RGBA, row 0 = top of the image, (0,0,0,0) = transparent
std::vector<unsigned char> canvasData;
GLuint textureID = 0;

// Dynamic variables for the size of the canvas
int canvasWidth = 192;
int canvasHeight = 108;

// Camera global variables
float zoom = 1.0f;         // screen pixels per canvas pixel
ImVec2 pan = {0.0f, 0.0f}; // screen position of the canvas top-left

// Button variables
enum class Tool { Free, Line, Circle, Rectangle, Triangle, Fill, Select };

struct DrawState {
  Tool tool = Tool::Free;

  // Color variable
  ImVec4 color = ImVec4(0, 0, 0, 1);

  // MOUSE COORDINATES FOR DRAWING LOGIC
  bool hasFirstPoint = false; // two-click shapes: first click already made
  int startX = 0, startY = 0; // where that first click landed
  int lastX = 0, lastY = 0;   // previous pixel while free drawing
};
DrawState ds;

// Selection: pixels are lifted out of the canvas into a floating buffer,
// transformed with a matrix, and stamped back
struct Selection {
  bool exists = false; // a rectangle is defined
  bool lifted = false; // pixels have been copied out of the canvas
  int w = 0, h = 0;
  std::vector<unsigned char> pixels; // floating buffer, RGBA, w*h*4
  std::vector<unsigned char> base;   // canvas WITHOUT the floating pixels
  Mat3 transform = Mat3::identity(); // floating-buffer space -> canvas space
};
Selection sel;

enum class SelState { None, Marquee, Ready, Moving };
SelState selState = SelState::None;

int mStartX = 0, mStartY = 0, mEndX = 0, mEndY = 0; // marquee, canvas pixels
ImVec2 dragStart = {0.0f, 0.0f}; // canvas coords at move start
Mat3 transformAtDragStart = Mat3::identity();

//---------------------------------------------
// SHADERS
//---------------------------------------------
const char *vertexShaderSource = R"GLSL(#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
uniform float uZoom;
uniform vec2 uPan;
uniform vec2 uViewport;
out vec2 TexCoord;
void main()
{
    vec2 screen = aPos * uZoom + uPan;
    vec2 ndc = (screen / uViewport) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
    TexCoord = aTexCoord;
}
)GLSL";

// Fragment shader: canvas blended over a checkerboard
// Checkerboard texture, unused now, fill doesnt work with this on
const char *fragmentShaderSource = R"GLSL(#version 460 core
out vec4 FragColor;
in vec2 TexCoord;
uniform sampler2D ourTexture;
void main()
{
    vec4 t = texture(ourTexture, TexCoord);
    float c = mod(floor(gl_FragCoord.x / 8.0) + floor(gl_FragCoord.y / 8.0), 2.0);
    vec3 bg = mix(vec3(0.80), vec3(1.0), c);
    FragColor = vec4(mix(bg, t.rgb, t.a), 1.0);
}
)GLSL";

//=============================================================================
// CANVAS / PIXEL FUNCTIONS
//=============================================================================

bool inCanvas(int x, int y) {
  // check how the bounds are against the current resolution
  return x >= 0 && x < canvasWidth && y >= 0 && y < canvasHeight;
}

// 0..1 float -> 0..255 byte
unsigned char toByte(float v) {
  return (unsigned char)(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Create a canvas since its independent from the window now
void createCanvas(int width, int height) {
  canvasWidth = width;
  canvasHeight = height;
  canvasData.assign(width * height * CH, 0); // all zeros = fully transparent

  // Fix of grid not working properly, gives allignment to the grid
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Bind and reallocate GPU texture
  glBindTexture(GL_TEXTURE_2D, textureID);
  // Upload the resized vector data to the GPU (vibe coded)
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, canvasData.data());
}

// Writes one pixel (overwrites, no blending); out-of-bounds is ignored
void plot(int x, int y, ImVec4 c) {
  if (!inCanvas(x, y))
    return;
  // Calculating the offset for 4 channels
  int i = (y * canvasWidth + x) * CH;
  // Updating the global canvas vector
  canvasData[i + 0] = toByte(c.x);
  canvasData[i + 1] = toByte(c.y);
  canvasData[i + 2] = toByte(c.z);
  canvasData[i + 3] = toByte(c.w);
}

// So we can get color information of a tile
ImVec4 getColor(int x, int y) {
  if (!inCanvas(x, y))
    return ImVec4(0, 0, 0, 0);
  int i = (y * canvasWidth + x) * CH;
  return ImVec4(canvasData[i] / 255.0f, canvasData[i + 1] / 255.0f,
                canvasData[i + 2] / 255.0f, canvasData[i + 3] / 255.0f);
}

// So we can compare colors
bool colorsMatch(ImVec4 a, ImVec4 b) {
  // Every fully transparent pixel counts as the same "color", whatever its RGB
  if (a.w < 0.01f && b.w < 0.01f)
    return true;
  return fabsf(a.x - b.x) < 0.01f && fabsf(a.y - b.y) < 0.01f &&
         fabsf(a.z - b.z) < 0.01f && fabsf(a.w - b.w) < 0.01f;
}

// Flood fill (BFS, 4 directions)
void floodFill(int startX, int startY, ImVec4 targetColor,
               ImVec4 replacementColor) {
  if (colorsMatch(targetColor, replacementColor))
    return;

  std::queue<std::pair<int, int>> q;
  q.push({startX, startY});
  plot(startX, startY, replacementColor);

  const int dx[] = {1, -1, 0, 0};
  const int dy[] = {0, 0, 1, -1};

  while (!q.empty()) {
    auto [cx, cy] = q.front();
    q.pop();

    for (int i = 0; i < 4; i++) {
      int nx = cx + dx[i];
      int ny = cy + dy[i];
      if (inCanvas(nx, ny) && colorsMatch(getColor(nx, ny), targetColor)) {
        plot(nx, ny, replacementColor);
        q.push({nx, ny});
      }
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

  // drive the loop along the x axis
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

  // drive the loop along the y axis
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

void drawLine(int x0, int y0, int x1, int y1, ImVec4 color) {
  // Check if the line is horizontal-leaning (|slope| <= 1) or vertical-leaning
  // (|slope| > 1)
  if (std::abs(y1 - y0) < std::abs(x1 - x0)) {
    // Low slope: step along X
    // Ensure we always draw from left to right (x0 <= x1)
    if (x0 > x1)
      plotLineLow(x1, y1, x0, y0, color);
    else
      plotLineLow(x0, y0, x1, y1, color);
  } else {
    // High slope: step along Y
    // Ensure we always draw from bottom to top (y0 <= y1)
    if (y0 > y1)
      plotLineHigh(x1, y1, x0, y0, color);
    else
      plotLineHigh(x0, y0, x1, y1, color);
  }
}

// first drawCircle implementation, not gonna work tho
// it did work, each one of these are quadrants of the circle
void plotCircleOctants(int xc, int yc, int x, int y, ImVec4 color) {
  plot(xc + x, yc + y, color);
  plot(xc - x, yc + y, color);
  plot(xc - x, yc - y, color);
  plot(xc + x, yc - y, color);
  plot(xc + y, yc + x, color);
  plot(xc - y, yc + x, color);
  plot(xc - y, yc - x, color);
  plot(xc + y, yc - x, color);
}

// Bresenham circle
void bresCircle(int xc, int yc, int r, ImVec4 color) {
  int x = 0, y = r;
  int d = 3 - (2 * r);
  plotCircleOctants(xc, yc, x, y, color);

  while (y >= x) {
    x++;
    if (d > 0) {
      y--;
      d = d + 4 * (x - y) + 10;
    } else {
      d = d + 4 * x + 6;
    }
    plotCircleOctants(xc, yc, x, y, color);
  }
}

void drawRectangle(int x0, int y0, int x1, int y1, ImVec4 color) {
  drawLine(x0, y0, x0, y1, color);
  drawLine(x0, y0, x1, y0, color);
  drawLine(x1, y0, x1, y1, color);
  drawLine(x0, y1, x1, y1, color);
}

// Triangle inside the box made by two corners (base at the bottom)
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

//=============================================================================
// CAMERA
//=============================================================================

// Screen position -> canvas position
ImVec2 screenToCanvas(ImVec2 s) {
  return {(s.x - pan.x) / zoom, (s.y - pan.y) / zoom};
}

// Canvas position -> screen position
ImVec2 canvasToScreen(ImVec2 c) {
  return {c.x * zoom + pan.x, c.y * zoom + pan.y};
}

// Center the canvas in the free area (left of the panel, below the menu bar)
void fitCanvasToWindow(GLFWwindow *window) {
  int w, h;
  glfwGetWindowSize(window, &w, &h);

  float menuH = ImGui::GetFrameHeight();
  float availW = w - w * PANEL_FRACTION;
  float availH = h - menuH;

  zoom = std::min(availW / canvasWidth, availH / canvasHeight) * 0.9f;
  pan.x = (availW - canvasWidth * zoom) * 0.5f;
  pan.y = menuH + (availH - canvasHeight * zoom) * 0.5f;
}

void updateCamera(ImVec2 mouse) {
  ImGuiIO &io = ImGui::GetIO();
  if (io.WantCaptureMouse)
    return;

  // Zoom toward the cursor (keeps the canvas point under the mouse fixed)
  if (io.MouseWheel != 0.0f) {
    ImVec2 before = screenToCanvas(mouse);
    zoom = std::clamp(zoom * powf(1.1f, io.MouseWheel), 0.5f, 64.0f);
    pan.x = mouse.x - before.x * zoom;
    pan.y = mouse.y - before.y * zoom;
  }

  // Pan with middle mouse
  if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
    pan.x += io.MouseDelta.x;
    pan.y += io.MouseDelta.y;
  }
}

//=============================================================================
// SELECTION: lift pixels -> transform with matrices -> commit
//=============================================================================

// Draw the floating buffer onto canvasData using INVERSE mapping:
// for each destination pixel, ask "which source pixel lands here?"
// (forward mapping would leave holes when rotating/scaling)
void stampFloating() {
  Mat3 inv = sel.transform.inverse();

  // Bounding box of the transformed rectangle, clamped to the canvas
  ImVec2 cs[4] = {sel.transform.apply({0.0f, 0.0f}),
                  sel.transform.apply({(float)sel.w, 0.0f}),
                  sel.transform.apply({(float)sel.w, (float)sel.h}),
                  sel.transform.apply({0.0f, (float)sel.h})};
  float minX = cs[0].x, maxX = cs[0].x, minY = cs[0].y, maxY = cs[0].y;
  for (int i = 1; i < 4; i++) {
    minX = std::min(minX, cs[i].x);
    maxX = std::max(maxX, cs[i].x);
    minY = std::min(minY, cs[i].y);
    maxY = std::max(maxY, cs[i].y);
  }
  int xs = std::max(0, (int)floorf(minX));
  int xe = std::min(canvasWidth - 1, (int)ceilf(maxX));
  int ys = std::max(0, (int)floorf(minY));
  int ye = std::min(canvasHeight - 1, (int)ceilf(maxY));

  for (int y = ys; y <= ye; y++) {
    for (int x = xs; x <= xe; x++) {
      ImVec2 src = inv.apply({x + 0.5f, y + 0.5f}); // sample at pixel center
      int sx = (int)floorf(src.x), sy = (int)floorf(src.y);
      if (sx < 0 || sx >= sel.w || sy < 0 || sy >= sel.h)
        continue;

      int si = (sy * sel.w + sx) * CH;
      if (sel.pixels[si + 3] == 0)
        continue; // transparent pixels don't overwrite what's underneath

      int di = (y * canvasWidth + x) * CH;
      for (int k = 0; k < CH; k++)
        canvasData[di + k] = sel.pixels[si + k];
    }
  }
}

// canvas = what's underneath + floating layer on top
void recomposite() {
  if (!sel.lifted)
    return;
  canvasData = sel.base;
  stampFloating();
}

// Copy pixels out of the canvas, leave a hole, and remember the canvas without
// them
void liftSelection() {
  if (!sel.exists || sel.lifted)
    return;

  int x0 = (int)lroundf(sel.transform.m[0][2]); // still a pure translate(x0,y0)
  int y0 = (int)lroundf(sel.transform.m[1][2]);

  sel.pixels.assign(sel.w * sel.h * CH, 0);
  for (int y = 0; y < sel.h; y++) {
    for (int x = 0; x < sel.w; x++) {
      int ci = ((y0 + y) * canvasWidth + (x0 + x)) * CH;
      int fi = (y * sel.w + x) * CH;
      for (int k = 0; k < CH; k++) {
        sel.pixels[fi + k] = canvasData[ci + k];
        canvasData[ci + k] = 0; // the hole is transparent
      }
    }
  }
  sel.base = canvasData;
  sel.lifted = true;
  recomposite(); // draws the floating copy back in place, so nothing flickers
}

// The composite is already in canvasData, so committing = forgetting the layer
void commitSelection() {
  sel = Selection{};
  selState = SelState::None;
}

// Transform about the selection's center: translate pivot to origin,
// apply M, translate back. New transform is applied AFTER the existing one.
void transformSelection(const Mat3 &M) {
  if (!sel.exists)
    return;
  liftSelection();
  ImVec2 p = sel.transform.apply({sel.w * 0.5f, sel.h * 0.5f});
  Mat3 about = Mat3::translate(p.x, p.y) * M * Mat3::translate(-p.x, -p.y);
  sel.transform = about * sel.transform;
  recomposite();
}

// Erase the selected area (leaves it transparent)
void deleteSelection() {
  if (!sel.exists)
    return;
  liftSelection();
  canvasData = sel.base; // the hole, without the floating copy
  commitSelection();
}

// Is this canvas point inside the (rotated/scaled) selection?
bool pointInSelection(ImVec2 c) {
  if (!sel.exists)
    return false;
  ImVec2 p = sel.transform.inverse().apply(c); // canvas -> floating space
  return p.x >= 0 && p.x < sel.w && p.y >= 0 && p.y < sel.h;
}

// Mouse logic for the Select tool (see SelState)
void handleSelectInput(bool down, bool wasDown, ImVec2 c) {
  bool pressed = down && !wasDown;
  bool released = !down && wasDown;
  int cx = std::clamp((int)floorf(c.x), 0, canvasWidth - 1);
  int cy = std::clamp((int)floorf(c.y), 0, canvasHeight - 1);

  if (pressed) {
    if (pointInSelection(c)) { // grab the selection to move it
      liftSelection();
      selState = SelState::Moving;
      dragStart = c;
      transformAtDragStart = sel.transform;
    } else { // start a new selection
      commitSelection();
      selState = SelState::Marquee;
      mStartX = mEndX = cx;
      mStartY = mEndY = cy;
    }
  } else if (down) {
    if (selState == SelState::Marquee) {
      mEndX = cx;
      mEndY = cy;
    } else if (selState == SelState::Moving) {
      float dx = roundf(c.x - dragStart.x); // whole pixels = lossless move
      float dy = roundf(c.y - dragStart.y);
      sel.transform = Mat3::translate(dx, dy) * transformAtDragStart;
      recomposite();
    }
  } else if (released) {
    if (selState == SelState::Marquee) {
      if (mStartX == mEndX && mStartY == mEndY) {
        selState = SelState::None; // plain click = deselect
      } else {
        int x0 = std::min(mStartX, mEndX), x1 = std::max(mStartX, mEndX);
        int y0 = std::min(mStartY, mEndY), y1 = std::max(mStartY, mEndY);
        sel.exists = true;
        sel.w = x1 - x0 + 1;
        sel.h = y1 - y0 + 1;
        sel.transform = Mat3::translate((float)x0, (float)y0);
        selState = SelState::Ready;
      }
    } else if (selState == SelState::Moving) {
      selState = SelState::Ready;
    }
  }
}

//=============================================================================
// TOOLS
//=============================================================================

// Switch tool: commits any selection and cancels a half-drawn shape
void setTool(Tool t) {
  if (ds.tool == t)
    return;
  commitSelection();        // drop any floating selection into the canvas
  ds.hasFirstPoint = false; // cancel a half-finished shape
  ds.tool = t;
}

// Line / Circle / Rectangle / Triangle: first click stores a point,
// second click draws the shape
void handleTwoClick(bool pressed, int cx, int cy) {
  if (!pressed)
    return;

  if (!ds.hasFirstPoint) {
    ds.startX = cx;
    ds.startY = cy;
    ds.hasFirstPoint = true;
    return;
  }
  ds.hasFirstPoint = false;

  switch (ds.tool) {
  // LINE DRAWING INPUT
  case Tool::Line:
    drawLine(ds.startX, ds.startY, cx, cy, ds.color);
    break;
  // CIRCLE DRAWING INPUT
  case Tool::Circle: {
    // Parse and calculation of the radius
    int r =
        (int)lroundf(hypotf((float)(cx - ds.startX), (float)(cy - ds.startY)));
    bresCircle(ds.startX, ds.startY, r, ds.color);
    break;
  }
  // RECTANGLE DRAWING INPUT
  case Tool::Rectangle:
    drawRectangle(ds.startX, ds.startY, cx, cy, ds.color);
    break;
  // TRIANGLE DRAWING INPUT
  case Tool::Triangle:
    drawTriangle(ds.startX, ds.startY, cx, cy, ds.color);
    break;
  default:
    break;
  }
}

//=============================================================================
// EXPORT
//=============================================================================

// Saves the canvas as a PNG. flattenOnWhite = false keeps transparency,
// true blends transparent areas onto white.
bool exportPNG(const char *path, bool flattenOnWhite) {
#if HAVE_STB
  commitSelection(); // flatten any floating selection first

  if (!flattenOnWhite) {
    return stbi_write_png(path, canvasWidth, canvasHeight, CH,
                          canvasData.data(), canvasWidth * CH) != 0;
  }

  std::vector<unsigned char> rgb(canvasWidth * canvasHeight * 3);
  for (int i = 0; i < canvasWidth * canvasHeight; i++) {
    float a = canvasData[i * 4 + 3] / 255.0f;
    for (int k = 0; k < 3; k++)
      rgb[i * 3 + k] =
          (unsigned char)(canvasData[i * 4 + k] * a + 255.0f * (1.0f - a));
  }
  return stbi_write_png(path, canvasWidth, canvasHeight, 3, rgb.data(),
                        canvasWidth * 3) != 0;
#else
  (void)path;
  (void)flattenOnWhite;
  std::cout << "PNG export disabled: put stb_image_write.h next to main.cpp"
            << std::endl;
  return false;
#endif
}

//=============================================================================
// INPUT
//=============================================================================

// process all input: query GLFW whether relevant keys are pressed/released this
// frame and react accordingly
void processInput(GLFWwindow *window) {
  ImGuiIO &io = ImGui::GetIO();

  if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
    glfwSetWindowShouldClose(window, true);

  double dXpos, dYpos;
  glfwGetCursorPos(window, &dXpos, &dYpos);
  ImVec2 mouse = {(float)dXpos, (float)dYpos};
  updateCamera(mouse);

  if (glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS)
    fitCanvasToWindow(window);

  if (glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS)
    commitSelection();
  if (glfwGetKey(window, GLFW_KEY_DELETE) == GLFW_PRESS)
    deleteSelection();

  // CURSOR POSITION IN GRID LOGIC
  ImVec2 c = screenToCanvas(mouse);
  int cellX = (int)floorf(c.x); // canvas pixel under the cursor
  int cellY = (int)floorf(c.y);

  // Left and Right click variables, currently only using left click
  static bool lastLeft = false;
  static bool dragOwned = false; // did this click start on the canvas?

  bool down = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  bool pressed = down && !lastLeft;

  // A click that starts on the UI panel never draws, and a drag that started on
  // the canvas keeps working (and sees its release) even over the panel
  if (pressed)
    dragOwned = !io.WantCaptureMouse;

  if (dragOwned) {
    switch (ds.tool) {
    case Tool::Free:
      // FREE DRAWING INPUT AND LOGIC (doesnt require a function)
      if (pressed) {
        ds.lastX = cellX;
        ds.lastY = cellY;
        plot(cellX, cellY, ds.color);
      } else if (down) {
        drawLine(ds.lastX, ds.lastY, cellX, cellY, ds.color);
        ds.lastX = cellX;
        ds.lastY = cellY;
      }
      break;

    case Tool::Line:
    case Tool::Circle:
    case Tool::Rectangle:
    case Tool::Triangle:
      handleTwoClick(pressed, cellX, cellY);
      break;

    case Tool::Fill:
      // FILL DRAWING INPUT
      if (pressed && inCanvas(cellX, cellY))
        floodFill(cellX, cellY, getColor(cellX, cellY), ds.color);
      break;

    case Tool::Select:
      handleSelectInput(down, lastLeft, c);
      break;
    }
  }

  if (!down)
    dragOwned = false;
  lastLeft = down;
}

//=============================================================================
// UI
//=============================================================================

// Top menu bar, returns its height
float mainMenuBar() {
  float menuHeight = 0.0f;

  if (ImGui::BeginMainMenuBar()) // Opens the global horizontal bar
  {
    menuHeight = ImGui::GetWindowSize().y;

    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New")) {
        commitSelection();
        ds.hasFirstPoint = false;
        std::fill(canvasData.begin(), canvasData.end(), 0);
      }
      if (ImGui::MenuItem("Export PNG (transparent)")) {
        std::cout << (exportPNG("canvas.png", false) ? "Saved canvas.png"
                                                     : "Export failed")
                  << std::endl;
      }
      if (ImGui::MenuItem("Export PNG (white background)")) {
        std::cout << (exportPNG("canvas_white.png", true)
                          ? "Saved canvas_white.png"
                          : "Export failed")
                  << std::endl;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z")) {
        /* not implemented yet */
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar(); // Closes the global horizontal bar
  }
  return menuHeight;
}

// Dear ImGui custom menu for the program
void imGuiMenu() {
  //---------------------------------------
  // My ImGUI window!!
  //---------------------------------------
  float menuBarHeight = mainMenuBar();
  ImVec2 displaySize = ImGui::GetIO().DisplaySize;
  float panelW = displaySize.x * PANEL_FRACTION;

  ImGui::SetNextWindowSize(ImVec2(panelW, displaySize.y - menuBarHeight));
  ImGui::SetNextWindowPos(ImVec2(displaySize.x - panelW, menuBarHeight));
  ImGui::Begin("Paintlike UI", NULL,
               ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
  // ImGui::Text("Color Picker and shapes v1");

  ImVec2 fullWidth = ImVec2(-1.0f, 0.0f);

  // ---- Tools -------------------------------------------------------------
  struct ToolEntry {
    Tool tool;
    const char *label;
  };
  static const ToolEntry tools[] = {
      {Tool::Free, "Free Drawing"}, {Tool::Line, "Line"},
      {Tool::Circle, "Circle"},     {Tool::Rectangle, "Rectangle"},
      {Tool::Triangle, "Triangle"}, {Tool::Fill, "Fill"},
      {Tool::Select, "Select"}};

  ImGui::SeparatorText("Drawing Options");
  for (const ToolEntry &t : tools)
    if (ImGui::RadioButton(t.label, ds.tool == t.tool))
      setTool(t.tool);

  // ---- Selection transforms ---------------------------------------------
  if (ds.tool == Tool::Select) {
    const float DEG = 3.14159265f / 180.0f;

    ImGui::SeparatorText("Selection");
    ImGui::TextWrapped("Drag to select, drag inside to move. Enter commits, "
                       "Delete erases.");

    ImGui::BeginDisabled(!sel.exists); // greyed out until a selection exists
    if (ImGui::Button("Rotate +15", fullWidth))
      transformSelection(Mat3::rotate(15.0f * DEG));
    if (ImGui::Button("Rotate -15", fullWidth))
      transformSelection(Mat3::rotate(-15.0f * DEG));
    if (ImGui::Button("Scale x1.25", fullWidth))
      transformSelection(Mat3::scale(1.25f, 1.25f));
    if (ImGui::Button("Scale x0.8", fullWidth))
      transformSelection(Mat3::scale(0.8f, 0.8f));
    if (ImGui::Button("Flip H", fullWidth))
      transformSelection(Mat3::scale(-1.0f, 1.0f)); // reflection
    if (ImGui::Button("Flip V", fullWidth))
      transformSelection(Mat3::scale(1.0f, -1.0f));
    if (ImGui::Button("Shear X", fullWidth))
      transformSelection(Mat3::shear(0.2f, 0.0f));
    if (ImGui::Button("Commit (Enter)", fullWidth))
      commitSelection();
    ImGui::EndDisabled();
  }

  // ---- Color ---------------------------------------------------------------
  float w = ImGui::GetContentRegionAvail().x;
  // ImGui::ColorPicker3("Select your color!!!", (float*)&color,
  // ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_NoAlpha);
  ImGui::SeparatorText("Color Options");
  ImGui::SetNextItemWidth(w);
  ImGui::ColorPicker3(
      "", (float *)&ds.color,
      ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview |
          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);

  ImGui::End();
}

// Things drawn on top of the canvas that are NOT part of it (border, marker,
// selection outline), so they never end up in the exported PNG
void drawOverlays() {
  ImDrawList *dl = ImGui::GetBackgroundDrawList();
  ImU32 selColor = IM_COL32(0, 150, 255, 255);

  // Canvas border, so the edges are visible at any zoom
  dl->AddRect(canvasToScreen({0.0f, 0.0f}),
              canvasToScreen({(float)canvasWidth, (float)canvasHeight}),
              IM_COL32(0, 0, 0, 255));

  // Marker for the first click of two-click shapes
  if (ds.hasFirstPoint) {
    ImVec2 p = canvasToScreen({ds.startX + 0.5f, ds.startY + 0.5f});
    dl->AddCircleFilled(p, 4.0f, selColor);
  }

  // Marquee while dragging, or the (transformed) selection outline
  if (selState == SelState::Marquee) {
    int x0 = std::min(mStartX, mEndX), x1 = std::max(mStartX, mEndX) + 1;
    int y0 = std::min(mStartY, mEndY), y1 = std::max(mStartY, mEndY) + 1;
    dl->AddRect(canvasToScreen({(float)x0, (float)y0}),
                canvasToScreen({(float)x1, (float)y1}), selColor);
  } else if (sel.exists) {
    // Same matrix as the pixels, so the outline rotates and scales with them
    ImVec2 a = canvasToScreen(sel.transform.apply({0.0f, 0.0f}));
    ImVec2 b = canvasToScreen(sel.transform.apply({(float)sel.w, 0.0f}));
    ImVec2 c =
        canvasToScreen(sel.transform.apply({(float)sel.w, (float)sel.h}));
    ImVec2 d = canvasToScreen(sel.transform.apply({0.0f, (float)sel.h}));
    dl->AddQuad(a, b, c, d, selColor);
  }
}

//=============================================================================
// GLFW / GL HELPERS
//=============================================================================

// glfw: whenever the window size changed (by OS or user resize) this callback
// function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  (void)window;
  // make sure the viewport matches the new window dimensions; note that width
  // and height will be significantly larger than specified on retina displays.
  glViewport(0, 0, width, height);
}

// Compiles a shader and prints the error log if it fails
GLuint compileShader(GLenum type, const char *source) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &source, NULL);
  glCompileShader(s);

  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof(log), NULL, log);
    std::cout << "Shader compile error:\n" << log << std::endl;
  }
  return s;
}

//=============================================================================
// MAIN
//=============================================================================
int main() {
  // glfw: initialize and configure
  // ------------------------------
  if (!glfwInit()) {
    std::cout << "Failed to initialize GLFW" << std::endl;
    return -1;
  }
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
  glfwSwapInterval(1); // vsync
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

  // glad: load all OpenGL function pointers
  // ---------------------------------------
  if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
    std::cout << "Failed to initialize GLAD" << std::endl;
    return -1;
  }

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

  // Build and compile shaders
  GLuint vs = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
  GLuint shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vs);
  glAttachShader(shaderProgram, fs);
  glLinkProgram(shaderProgram);
  GLint linked = 0;
  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &linked);
  if (!linked)
    std::cout << "Shader program failed to link" << std::endl;
  // Clean up individual shader objects after linking
  glDeleteShader(vs);
  glDeleteShader(fs);

  // Uniform locations (camera values sent to the vertex shader)
  GLint zoomLoc = glGetUniformLocation(shaderProgram, "uZoom");
  GLint panLoc = glGetUniformLocation(shaderProgram, "uPan");
  GLint viewportLoc = glGetUniformLocation(shaderProgram, "uViewport");

  // SETTING UP GEOMETRY (Quad, VBO, VAO)
  // Quad in CANVAS pixel coordinates, the camera is applied in the shader
  float W = (float)canvasWidth, H = (float)canvasHeight;
  float quadVertices[] = {
      // pos      // uv
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
  glGenTextures(1, &textureID);            // ID and vector/pointer of textures
  glBindTexture(GL_TEXTURE_2D, textureID); // We bind the texture to GPU VRAM

  // Set texture filtering (GL_NEAREST for crisp textures)
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  createCanvas(canvasWidth, canvasHeight);

  bool needFit = true; // fit once, after the first frame has a menu bar height

  // RENDER LOOP
  // -----------
  while (!glfwWindowShouldClose(window)) {
    // glfw: poll IO events (keys pressed/released, mouse moved etc.)
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    // ImGui::ShowDemoWindow(); // Show demo window! :)

    if (needFit) {
      fitCanvasToWindow(window);
      needFit = false;
    }

    imGuiMenu();

    // input
    processInput(window);
    drawOverlays();

    // render
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);

    glClearColor(0.18f, 0.18f, 0.18f, 1.0f); // dark gray around the canvas
    glClear(GL_COLOR_BUFFER_BIT);

    // We append our little programs
    glUseProgram(shaderProgram);
    glUniform1f(zoomLoc, zoom);
    glUniform2f(panLoc, pan.x, pan.y);
    glUniform2f(viewportLoc, (float)winW, (float)winH);

    glBindTexture(GL_TEXTURE_2D, textureID);
    // Upload the canvas to the GPU every frame
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, canvasWidth, canvasHeight, GL_RGBA,
                    GL_UNSIGNED_BYTE, canvasData.data());
    glBindVertexArray(VAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    // glfw: swap buffers
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  glDeleteVertexArrays(1, &VAO);
  glDeleteBuffers(1, &VBO);
  glDeleteTextures(1, &textureID);
  glDeleteProgram(shaderProgram);

  // glfw: terminate, clearing all previously allocated GLFW resources.
  glfwTerminate();
  return 0;
}
