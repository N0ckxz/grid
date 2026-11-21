# Raster Drawing Application (OpenGL & C++)

A lightweight 2D pixel-art and raster graphics editor built in C++17 using OpenGL 4.6, GLFW, and Dear ImGui.

---

## Media Showcase

### Video Demonstration (Spanish)
[![Watch Demonstration Video](https://img.youtube.com/vi/2QBIYWBG42o/maxresdefault.jpg)](https://youtu.be/2QBIYWBG42o)

*[Watch Demonstration Video on YouTube](https://youtu.be/2QBIYWBG42o)*

### Screenshots
| Showcase #2 | Showcase #2 |
| :---: | :---: |
| ![Workspace Screenshot](https://github.com/user-attachments/assets/9cbd5dcf-f8e4-47de-86a4-c2a9b8ca379e) | ![Selection Transforms Screenshot](https://github.com/user-attachments/assets/3000744b-689b-4cbd-88f3-54aab847becb) |

---

## How It Works

### 1. Architecture & Rendering Pipeline
- **CPU Framebuffer:** The canvas is stored in system memory as a flat `std::vector<unsigned char>` representing an RGBA grid (default resolution `192 x 108`, 4 bytes per pixel). All drawing primitives modify this buffer directly.
- **GPU Texture Streaming:** On every render pass, `glTexSubImage2D` streams the updated CPU buffer to an OpenGL `GL_RGBA8` texture configured with `GL_NEAREST` filtering to preserve crisp pixel boundaries at high zoom levels.
- **GLSL Shaders:**
  - **Vertex Shader:** Maps canvas-space quad vertices into screen space using uniform `uZoom` and `uPan` values, then converts them to Normalized Device Coordinates (NDC).
  - **Fragment Shader:** Procedurally generates an 8x8 checkerboard pattern in screen coordinates (`gl_FragCoord`) and alpha-blends the canvas texture over it, ensuring transparency is visualized without polluting the underlying pixel buffer.

### 2. Rasterization Algorithms
- **Lines (`drawLine`):** Implements Bresenham's line algorithm split into low-slope (`plotLineLow`) and high-slope (`plotLineHigh`) octant handlers for integer-only pixel plotting.
- **Circles (`bresCircle`):** Uses the midpoint/Bresenham circle algorithm with 8-way octant symmetry (`plotCircleOctants`).
- **Polygons (`drawRectangle`, `drawTriangle`):** Constructs axis-aligned rectangles and bounding-box-fitted isosceles triangles by composing Bresenham line segments.
- **Flood Fill (`floodFill`):** Executes a 4-directional Breadth-First Search (BFS) using `std::queue`, matching contiguous RGBA values (treating all alpha values below `0.01` as equivalent transparency).

### 3. Selection & 2D Affine Transformations
- **Floating Buffer Model:** When a marquee selection is moved or transformed, `liftSelection()` copies the selected pixels into a temporary floating buffer and clears the underlying region in `canvasData` (storing the unmodified background in `sel.base`).
- **Homogeneous Matrices (`Mat3`):** Transformations—translation, scaling, rotation, and horizontal shear—are represented as $3 \times 3$ affine matrices concatenated around the selection's center pivot.
- **Inverse Mapping (`stampFloating`):** To prevent holes or aliasing artifacts during rotation and scaling, the renderer iterates over the destination bounding box on the canvas and applies the inverse transformation matrix (`sel.transform.inverse()`) to sample the nearest source pixel from the floating buffer.

### 4. User Interface & Export
- **Dear ImGui Overlay:** Renders a monochrome docked side panel, top menu bar, keybind reference modal (`F1`), and non-destructive vector overlays (`ImGui::GetBackgroundDrawList()`) for canvas borders, two-click anchor markers, and transformed selection bounding quads.
- **PNG Export (`exportPNG`):** Uses `stb_image_write.h` (when available at compile time) to flatten any active selection and export the canvas either with an alpha channel (`canvas.png`) or composited onto an opaque white background (`canvas_white.png`).

---

## Controls & Keybinds

| Input / Key | Action |
| :--- | :--- |
| **Left Click / Drag** | Draw with the active tool or define selection marquee |
| **Scroll Wheel** | Zoom toward the cursor (`0.5x` to `64.0x`) |
| **Middle Mouse Drag** | Pan the canvas |
| **F** | Fit and center the canvas within the viewport |
| **F1** | Toggle the Keybinds help window |
| **Enter** | Commit floating selection pixels to the canvas |
| **Delete** | Erase the currently selected pixels |
| **Esc** | Close the application |
