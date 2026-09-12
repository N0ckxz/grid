#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <iostream>
#include <vector>

std::vector<unsigned char> canvasData;
GLuint textureID;

// initializing functions
void handleCanvasResize(int width, int height); //since textureID is global now, we can just use width and height
void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void drawLine(int x0, int x1, int y0, int y1, unsigned char r, unsigned char g, unsigned char b);
int plotLineLow(int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b);
int plotLineHigh(int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b);
void plot(int x, int y, unsigned char r, unsigned char g, unsigned char b);

// settings
const unsigned int SCR_WIDTH = 300;
const unsigned int SCR_HEIGHT = 300;

//Shaders
//I dont understand how this works, some day though
const char *vertexShaderSource = "#version 460 core\n"
    "layout (location = 0) in vec2 aPos;\n"
    "layout (location = 1) in vec2 aTexCoord;\n"
    "out vec2 TexCoord;\n"
    "void main()\n"
    "{\n"
    "   gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "   TexCoord = aTexCoord;\n"
    "}\0";

  const char *fragmentShaderSource = "#version 460 core\n"
    "out vec4 FragColor;\n"
    "in vec2 TexCoord;\n"
    "uniform sampler2D ourTexture;\n"
    "void main()\n"
    "{\n"
    "   FragColor = texture(ourTexture, TexCoord);\n"
    "}\n\0";

int main()
{
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
    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "LearnOpenGL", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // glad: load all OpenGL function pointers
    // ---------------------------------------
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    //Build and compile shaders
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

    //SETTING UP GEOMETRY (Quad, VBO, VAO)
float quadVertices[] = {
        // Positions   // TexCoords
        -1.0f,  1.0f,  0.0f, 1.0f, // Top-Left
        -1.0f, -1.0f,  0.0f, 0.0f, // Bottom-Left
         1.0f, -1.0f,  1.0f, 0.0f, // Bottom-Right

        -1.0f,  1.0f,  0.0f, 1.0f, // Top-Left
         1.0f, -1.0f,  1.0f, 0.0f, // Bottom-Right
         1.0f,  1.0f,  1.0f, 1.0f  // Top-Right
    };

//IMPORTANT: I dont understand any of this still, I should ask about it
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);

    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    // Position Attribute (location = 0)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // TexCoord Attribute (location = 1)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

//Setting up canvas texture

    textureID; //Texture declaration
    glGenTextures(1, &textureID); // ID and vector/pointer of textures
    glBindTexture(GL_TEXTURE_2D, textureID); //We bind the texture to GPU VRAM

    // Set texture filtering (GL_NEAREST for crisp textures)

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    handleCanvasResize(SCR_WIDTH, SCR_HEIGHT);

// RENDER LOOP
    // -----------
    while (!glfwWindowShouldClose(window))
    {
        // input
        processInput(window);

        // render

        // We append our little programs
        glUseProgram(shaderProgram);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // glfw: swap buffers and poll IO events (keys pressed/released, mouse moved etc.)
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteProgram(shaderProgram);

    // glfw: terminate, clearing all previously allocated GLFW resources.
    glfwTerminate();
    return 0;
}

// process all input: query GLFW whether relevant keys are pressed/released this frame and react accordingly
void processInput(GLFWwindow *window)
{
    if(glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    //if(window == GLFW_MOUSE_BUTTON_LEFT && )


    double xpos, ypos;
    glfwGetCursorPos(window, &xpos, &ypos);
    std::cout << "X-position: " << xpos << " |-Y position: " << ypos << "\n";
}

void handleCanvasResize(int width, int height)
{
    // we resize the vector when needed
    canvasData.resize(width * height * 3);

    // Fill the vector with 255 (White)
    std::fill(canvasData.begin(), canvasData.end(), 255);

//IMPORTANT: Vibe coded, gotta study this, either way its just a way of visualizing the grid, dont think we will use this
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int index = (y * width + x) * 3;

            // Check if we are on a grid line (e.g., every 20 pixels)
            if (x % 10 == 0 || y % 10 == 0) {
                // Dark Grey Grid Lines ⬛
                canvasData[index + 0] = 50;
                canvasData[index + 1] = 50;
                canvasData[index + 2] = 50;
            } else {
                // White Background ⬜
                canvasData[index + 0] = 255;
                canvasData[index + 1] = 255;
                canvasData[index + 2] = 255;
            }
        }
    }

    // Fix of grid not working properly, gives allignment to the grid
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // Bind and reallocate GPU texture
    // Upload the resized vector data to the GPU (vibe coded)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, canvasData.data());
}

// glfw: whenever the window size changed (by OS or user resize) this callback function executes
// ---------------------------------------------------------------------------------------------
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    // make sure the viewport matches the new window dimensions; note that width and
    // height will be significantly larger than specified on retina displays.
    glViewport(0, 0, width, height);

    handleCanvasResize(width, height);
}

// sadly, made with AI, ran out of time
void drawLine(int x0, int x1, int y0, int y1, unsigned char r, unsigned char g, unsigned char b)
{
    // Check if the line is horizontal-leaning (|slope| <= 1) or vertical-leaning (|slope| > 1)
    if (abs(y1 - y0) < abs(x1 - x0))
    {
        // Low slope: step along X
        // Ensure we always draw from left to right (x0 <= x1)
        if (x0 > x1) {
            plotLineLow(x1, y1, x0, y0, r, g, b);
        } else {
            plotLineLow(x0, y0, x1, y1, r, g, b);
        }
    }
    else
    {
        // High slope: step along Y
        // Ensure we always draw from bottom to top (y0 <= y1)
        if (y0 > y1) {
            plotLineHigh(x1, y1, x0, y0, r, g, b);
        } else {
            plotLineHigh(x0, y0, x1, y1, r, g, b);
        }
    }
}

// function for plotting in the lower parts of the octant
int plotLineLow(int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b)
{
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
    for (int x = x0; x <= x1; x++)
    {
        plot(x, y, r, g, b);

        if (D > 0) {
            y += yi;
            D += 2 * (dy - dx);
        } else {
            D += 2 * dy;
        }
    }
}

int plotLineHigh(int x0, int y0, int x1, int y1, unsigned char r, unsigned char g, unsigned char b)
{
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
    for (int y = y0; y <= y1; y++)
    {
        plot(x, y, r, g, b);

        if (D > 0) {
            x += xi;
            D += 2 * (dx - dy);
        } else {
            D += 2 * dx;
        }
    }
}

// with a little AI help*
void plot(int x, int y, unsigned char r, unsigned char g, unsigned char b)
{
    // check how the bounds are against the current resolution
    if (x < 0 || x >= SCR_WIDTH || y < 0 || y >= SCR_HEIGHT) return;

    // Calculating the offset for 3 channels
    int index = (y * SCR_WIDTH + x) * 4;

    // Updating the global canvas vector
    canvasData[index + 0] = r;
    canvasData[index + 1] = g;
    canvasData[index + 2] = b;
}