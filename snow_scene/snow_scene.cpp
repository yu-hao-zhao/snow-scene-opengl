#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <string>
#include <vector>

// =========================
// 全局常量与基础数据结构
// =========================
constexpr unsigned int SCR_WIDTH = 1280;
constexpr unsigned int SCR_HEIGHT = 720;
constexpr int MAX_POINT_LIGHTS = 8;
constexpr int SNOW_PARTICLE_COUNT = 720;
constexpr int STAR_COUNT = 180;
constexpr float PI = 3.14159265359f;

// 通用网格结构：既可表示立方体，也可表示球体
struct Mesh {
    unsigned int vao = 0;
    unsigned int vbo = 0;
    unsigned int ebo = 0;
    GLsizei vertexCount = 0;
    GLsizei indexCount = 0;
    bool indexed = false;
};

// 点光源参数
struct PointLightData {
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 0.0f;
};

// 雪花粒子参数
struct SnowParticle {
    glm::vec3 position{0.0f};
    float speed = 1.0f;
    float size = 4.0f;
    float drift = 0.0f;
    float phase = 0.0f;
};

// 星星粒子参数
struct StarParticle {
    glm::vec3 position{0.0f};
    float size = 2.0f;
    float phase = 0.0f;
};

// 可交互树桩的状态
struct StumpState {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};
    glm::vec3 scale{1.0f};
};

// =========================
// 全局运行状态
// =========================
GLFWwindow* gWindow = nullptr;
int gFramebufferWidth = static_cast<int>(SCR_WIDTH);
int gFramebufferHeight = static_cast<int>(SCR_HEIGHT);

// 固定展示视角：仅允许平移，不允许自由转头
glm::vec3 gCameraPos(0.0f, 4.6f, 13.2f);
glm::vec3 gCameraFront = glm::normalize(glm::vec3(0.0f, -0.24f, -1.0f));
glm::vec3 gCameraUp(0.0f, 1.0f, 0.0f);

// 昼夜与降雪过渡控制量
bool gNightTarget = false;
float gNightBlend = 0.0f;
bool gSnowTarget = true;
float gSnowAmount = 1.0f;

// 树桩初始状态
const StumpState kInitialStump{
    glm::vec3(1.8f, -0.45f, 1.6f),
    glm::vec3(0.0f, 0.0f, 0.0f),
    glm::vec3(0.90f, 0.80f, 0.90f)
};

StumpState gStump = kInitialStump;
bool gLeftMouseDown = false;
double gLastCursorX = 0.0;
double gLastCursorY = 0.0;

std::mt19937 gRng(20260416u);
std::vector<SnowParticle> gSnowParticles;
std::vector<StarParticle> gStars;

// 窗口尺寸变化时同步更新 OpenGL 视口
void framebuffer_size_callback(GLFWwindow*, int width, int height) {
    gFramebufferWidth = std::max(width, 1);
    gFramebufferHeight = std::max(height, 1);
    glViewport(0, 0, gFramebufferWidth, gFramebufferHeight);
}

// 当前实验采用固定朝向相机，因此这里只保留一个统一观察方向
void updateCameraFront() {
    gCameraFront = glm::normalize(glm::vec3(0.0f, -0.24f, -1.0f));
}

// 判断 Ctrl 是否按下，用于触发树桩旋转
bool isCtrlPressed() {
    if (gWindow == nullptr) return false;
    return glfwGetKey(gWindow, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
           glfwGetKey(gWindow, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
}

// 将树桩位置和缩放限制在合理范围内，避免拖出场景
void clampStump() {
    gStump.position.x = std::clamp(gStump.position.x, -8.0f, 8.0f);
    gStump.position.z = std::clamp(gStump.position.z, -8.0f, 7.0f);
    gStump.position.y = kInitialStump.position.y;

    gStump.scale.x = std::clamp(gStump.scale.x, 0.35f, 2.80f);
    gStump.scale.y = std::clamp(gStump.scale.y, 0.35f, 3.00f);
    gStump.scale.z = std::clamp(gStump.scale.z, 0.35f, 2.80f);
}

// 限制固定视角相机的上下/左右平移范围
void clampCameraPosition() {
    gCameraPos.x = std::clamp(gCameraPos.x, -7.5f, 7.5f);
    gCameraPos.y = std::clamp(gCameraPos.y, 2.4f, 7.2f);
    gCameraPos.z = 13.2f;
}

// 恢复树桩初始状态
void resetStump() {
    gStump = kInitialStump;
}

// 鼠标左键拖动：平移树桩；按住 Ctrl 时不响应拖动
void cursor_position_callback(GLFWwindow*, double xpos, double ypos) {
    if (!gLeftMouseDown) return;

    const float dx = static_cast<float>(xpos - gLastCursorX);
    const float dy = static_cast<float>(ypos - gLastCursorY);
    gLastCursorX = xpos;
    gLastCursorY = ypos;

    if (isCtrlPressed()) return;

    gStump.position.x += dx * 0.015f;
    gStump.position.z += dy * 0.018f;
    clampStump();
}

// 鼠标左键开始/结束拖拽，右键恢复树桩
void mouse_button_callback(GLFWwindow* window, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            gLeftMouseDown = true;
            glfwGetCursorPos(window, &gLastCursorX, &gLastCursorY);
        } else if (action == GLFW_RELEASE) {
            gLeftMouseDown = false;
        }
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        resetStump();
    }
}

// 生成指定区间内的随机浮点数
float randomRange(float minValue, float maxValue) {
    std::uniform_real_distribution<float> dist(minValue, maxValue);
    return dist(gRng);
}

// 重置单个雪花粒子，使其重新从空中落下
void resetSnowParticle(SnowParticle& particle, bool randomHeight) {
    particle.position.x = randomRange(-15.0f, 15.0f);
    particle.position.z = randomRange(-15.0f, 15.0f);
    particle.position.y = randomHeight ? randomRange(-0.5f, 18.0f) : randomRange(11.0f, 18.5f);
    particle.speed = randomRange(1.4f, 4.0f);
    particle.size = randomRange(4.0f, 7.5f);
    particle.drift = randomRange(-0.45f, 0.45f);
    particle.phase = randomRange(0.0f, 2.0f * PI);
}

// 初始化全部雪花粒子
void initializeSnow() {
    gSnowParticles.resize(SNOW_PARTICLE_COUNT);
    for (auto& particle : gSnowParticles) {
        resetSnowParticle(particle, true);
    }
}

// 初始化夜空星星
void initializeStars() {
    gStars.resize(STAR_COUNT);
    for (auto& star : gStars) {
        star.position.x = randomRange(-28.0f, 28.0f);
        star.position.y = randomRange(7.5f, 18.0f);
        star.position.z = randomRange(-32.0f, -12.0f);
        star.size = randomRange(2.0f, 4.2f);
        star.phase = randomRange(0.0f, 2.0f * PI);
    }
}

// 统一处理键盘输入：相机平移、昼夜切换、降雪切换、树桩缩放/旋转
void processInput(GLFWwindow* window, float deltaTime) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, true);
    }

    // 固定视角下，W/S 控制上下，A/D 控制左右
    const float moveSpeed = 4.5f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) gCameraPos.y += moveSpeed;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) gCameraPos.y -= moveSpeed;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) gCameraPos.x -= moveSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) gCameraPos.x += moveSpeed;
    clampCameraPosition();
    updateCameraFront();

    const float stretchSpeed = 1.2f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        gStump.scale.y += stretchSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        gStump.scale.y -= stretchSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        gStump.scale.x += stretchSpeed;
        gStump.scale.z += stretchSpeed;
    }
    if (glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        gStump.scale.x -= stretchSpeed;
        gStump.scale.z -= stretchSpeed;
    }
    if (isCtrlPressed()) {
        gStump.rotation.y += 65.0f * deltaTime;
    }
    clampStump();

    // 使用“按下一次切换一次”的方式，避免长按时重复触发
    static bool nLast = false;
    static bool fLast = false;
    static bool rLast = false;

    const bool nNow = glfwGetKey(window, GLFW_KEY_N) == GLFW_PRESS;
    const bool fNow = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
    const bool rNow = glfwGetKey(window, GLFW_KEY_R) == GLFW_PRESS;

    if (nNow && !nLast) gNightTarget = !gNightTarget;
    if (fNow && !fLast) gSnowTarget = !gSnowTarget;
    if (rNow && !rLast) resetStump();

    nLast = nNow;
    fLast = fNow;
    rLast = rNow;
}

// =========================
// Shader 与 Uniform 工具
// =========================
unsigned int compileShader(unsigned int type, const char* source) {
    const unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[1024]{};
        glGetShaderInfoLog(shader, 1024, nullptr, infoLog);
        std::cerr << "Shader compile error:\n" << infoLog << '\n';
    }
    return shader;
}

unsigned int createProgram(const char* vertexSource, const char* fragmentSource) {
    const unsigned int vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const unsigned int fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

    const unsigned int program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[1024]{};
        glGetProgramInfoLog(program, 1024, nullptr, infoLog);
        std::cerr << "Program link error:\n" << infoLog << '\n';
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

void setMat4(unsigned int program, const std::string& name, const glm::mat4& value) {
    glUniformMatrix4fv(glGetUniformLocation(program, name.c_str()), 1, GL_FALSE, glm::value_ptr(value));
}

void setVec3(unsigned int program, const std::string& name, const glm::vec3& value) {
    glUniform3fv(glGetUniformLocation(program, name.c_str()), 1, glm::value_ptr(value));
}

void setFloat(unsigned int program, const std::string& name, float value) {
    glUniform1f(glGetUniformLocation(program, name.c_str()), value);
}

void setInt(unsigned int program, const std::string& name, int value) {
    glUniform1i(glGetUniformLocation(program, name.c_str()), value);
}

// =========================
// 基础网格构建
// =========================
// 构建立方体网格：用于房屋、地面、树桩、围栏等对象
Mesh createCubeMesh() {
    const float vertices[] = {
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
         0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  0.0f,  0.0f, 1.0f,

        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

         0.5f,  0.5f,  0.5f, 1.0f,  0.0f,  0.0f,
         0.5f,  0.5f, -0.5f, 1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,
         0.5f, -0.5f, -0.5f, 1.0f,  0.0f,  0.0f,
         0.5f, -0.5f,  0.5f, 1.0f,  0.0f,  0.0f,
         0.5f,  0.5f,  0.5f, 1.0f,  0.0f,  0.0f,

        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f,  0.0f,
         0.5f, -0.5f, -0.5f, 0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f, 0.0f, -1.0f,  0.0f,
         0.5f, -0.5f,  0.5f, 0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f,  0.5f, 0.0f, -1.0f,  0.0f,
        -0.5f, -0.5f, -0.5f, 0.0f, -1.0f,  0.0f,

        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,  0.0f,
         0.5f,  0.5f, -0.5f, 0.0f, 1.0f,  0.0f,
         0.5f,  0.5f,  0.5f, 0.0f, 1.0f,  0.0f,
         0.5f,  0.5f,  0.5f, 0.0f, 1.0f,  0.0f,
        -0.5f,  0.5f,  0.5f, 0.0f, 1.0f,  0.0f,
        -0.5f,  0.5f, -0.5f, 0.0f, 1.0f,  0.0f
    };

    Mesh mesh;
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    mesh.vertexCount = 36;
    mesh.indexed = false;
    return mesh;
}

// 构建球体网格：用于雪人、树冠、雪山、灌木、太阳/月亮等对象
Mesh createSphereMesh(int sectors, int stacks) {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    for (int i = 0; i <= stacks; ++i) {
        const float stackAngle = PI / 2.0f - static_cast<float>(i) * PI / static_cast<float>(stacks);
        const float xy = std::cos(stackAngle);
        const float y = std::sin(stackAngle);

        for (int j = 0; j <= sectors; ++j) {
            const float sectorAngle = 2.0f * PI * static_cast<float>(j) / static_cast<float>(sectors);
            const float x = xy * std::cos(sectorAngle);
            const float z = xy * std::sin(sectorAngle);

            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
            vertices.push_back(x);
            vertices.push_back(y);
            vertices.push_back(z);
        }
    }

    for (int i = 0; i < stacks; ++i) {
        const int k1 = i * (sectors + 1);
        const int k2 = k1 + sectors + 1;
        for (int j = 0; j < sectors; ++j) {
            if (i != 0) {
                indices.push_back(k1 + j);
                indices.push_back(k2 + j);
                indices.push_back(k1 + j + 1);
            }
            if (i != stacks - 1) {
                indices.push_back(k1 + j + 1);
                indices.push_back(k2 + j);
                indices.push_back(k2 + j + 1);
            }
        }
    }

    Mesh mesh;
    glGenVertexArrays(1, &mesh.vao);
    glGenBuffers(1, &mesh.vbo);
    glGenBuffers(1, &mesh.ebo);
    glBindVertexArray(mesh.vao);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)), indices.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    mesh.indexCount = static_cast<GLsizei>(indices.size());
    mesh.indexed = true;
    return mesh;
}

// 释放网格缓冲
void destroyMesh(Mesh& mesh) {
    if (mesh.ebo != 0) glDeleteBuffers(1, &mesh.ebo);
    if (mesh.vbo != 0) glDeleteBuffers(1, &mesh.vbo);
    if (mesh.vao != 0) glDeleteVertexArrays(1, &mesh.vao);
    mesh = {};
}

// 组合模型矩阵：先平移，再按 y/x/z 轴旋转
glm::mat4 composeTransform(const glm::vec3& position, const glm::vec3& rotation) {
    glm::mat4 model(1.0f);
    model = glm::translate(model, position);
    model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return model;
}

// 在已有变换基础上追加缩放
glm::mat4 scaledModel(const glm::mat4& transform, const glm::vec3& scale) {
    return glm::scale(transform, scale);
}

// 通用绘制入口：负责把模型矩阵、颜色、材质参数传给着色器并发起绘制
void drawMesh(unsigned int program, const Mesh& mesh, const glm::mat4& model,
              const glm::vec3& color, float specularStrength, float shininess, float emissiveStrength) {
    setMat4(program, "model", model);
    setVec3(program, "objectColor", color);
    setFloat(program, "specularStrength", specularStrength);
    setFloat(program, "shininess", shininess);
    setFloat(program, "emissiveStrength", emissiveStrength);

    glBindVertexArray(mesh.vao);
    if (mesh.indexed) {
        glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_INT, nullptr);
    } else {
        glDrawArrays(GL_TRIANGLES, 0, mesh.vertexCount);
    }
}

// 立方体绘制封装
void drawCube(unsigned int program, const Mesh& cubeMesh, const glm::vec3& position, const glm::vec3& rotation,
              const glm::vec3& scale, const glm::vec3& color, float specularStrength, float shininess, float emissiveStrength = 0.0f) {
    const glm::mat4 model = scaledModel(composeTransform(position, rotation), scale);
    drawMesh(program, cubeMesh, model, color, specularStrength, shininess, emissiveStrength);
}

// 球体绘制封装
void drawSphere(unsigned int program, const Mesh& sphereMesh, const glm::vec3& position, const glm::vec3& rotation,
                const glm::vec3& scale, const glm::vec3& color, float specularStrength, float shininess, float emissiveStrength = 0.0f) {
    const glm::mat4 model = scaledModel(composeTransform(position, rotation), scale);
    drawMesh(program, sphereMesh, model, color, specularStrength, shininess, emissiveStrength);
}

// 将多个点光源参数统一传入片元着色器
void setPointLights(unsigned int program, const std::vector<PointLightData>& lights) {
    const std::size_t count = std::min<std::size_t>(lights.size(), MAX_POINT_LIGHTS);
    setInt(program, "pointLightCount", static_cast<int>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const std::string prefix = "pointLights[" + std::to_string(i) + "].";
        setVec3(program, prefix + "position", lights[i].position);
        setVec3(program, prefix + "color", lights[i].color);
        setFloat(program, prefix + "intensity", lights[i].intensity);
    }
}

// =========================
// 场景对象绘制函数
// =========================
// 雪地与前景平台
void drawGround(unsigned int program, const Mesh& cubeMesh) {
    drawCube(program, cubeMesh, glm::vec3(0.0f, -1.05f, 0.0f), glm::vec3(0.0f),
             glm::vec3(32.0f, 0.40f, 32.0f), glm::vec3(0.95f, 0.97f, 1.00f), 0.10f, 16.0f);

    drawCube(program, cubeMesh, glm::vec3(0.0f, -0.82f, -6.5f), glm::vec3(0.0f),
             glm::vec3(18.0f, 0.08f, 8.0f), glm::vec3(0.88f, 0.92f, 0.98f), 0.08f, 8.0f);
}

// 远景雪山/雪丘
void drawBackgroundHills(unsigned int program, const Mesh& sphereMesh) {
    drawSphere(program, sphereMesh, glm::vec3(-14.0f, -3.0f, -17.0f), glm::vec3(0.0f),
               glm::vec3(8.5f, 4.3f, 5.8f), glm::vec3(0.79f, 0.85f, 0.94f), 0.03f, 6.0f);
    drawSphere(program, sphereMesh, glm::vec3(-2.0f, -3.2f, -18.5f), glm::vec3(0.0f),
               glm::vec3(10.2f, 4.8f, 6.3f), glm::vec3(0.83f, 0.88f, 0.96f), 0.03f, 6.0f);
    drawSphere(program, sphereMesh, glm::vec3(13.0f, -3.0f, -17.2f), glm::vec3(0.0f),
               glm::vec3(8.8f, 4.0f, 5.6f), glm::vec3(0.78f, 0.84f, 0.93f), 0.03f, 6.0f);

    drawSphere(program, sphereMesh, glm::vec3(-12.5f, -0.2f, -16.3f), glm::vec3(0.0f),
               glm::vec3(5.0f, 0.8f, 2.8f), glm::vec3(0.95f, 0.97f, 1.0f), 0.02f, 4.0f);
    drawSphere(program, sphereMesh, glm::vec3(0.0f, -0.1f, -18.0f), glm::vec3(0.0f),
               glm::vec3(6.8f, 0.9f, 3.0f), glm::vec3(0.95f, 0.97f, 1.0f), 0.02f, 4.0f);
    drawSphere(program, sphereMesh, glm::vec3(12.0f, -0.15f, -16.6f), glm::vec3(0.0f),
               glm::vec3(5.5f, 0.8f, 2.7f), glm::vec3(0.95f, 0.97f, 1.0f), 0.02f, 4.0f);
}

// 灌木与灌木顶部积雪
void drawBush(unsigned int program, const Mesh& sphereMesh, const glm::vec3& position, const glm::vec3& scale) {
    drawSphere(program, sphereMesh, position, glm::vec3(0.0f),
               scale, glm::vec3(0.18f, 0.38f, 0.18f), 0.10f, 12.0f);
    drawSphere(program, sphereMesh, position + glm::vec3(0.0f, scale.y * 0.52f, 0.0f), glm::vec3(0.0f),
               glm::vec3(scale.x * 0.72f, scale.y * 0.32f, scale.z * 0.72f),
                glm::vec3(0.97f, 0.98f, 1.0f), 0.03f, 4.0f, 0.03f);
}

// 前院围栏
void drawYardFence(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh) {
    auto drawPost = [&](const glm::vec3& pos) {
        drawCube(program, cubeMesh, pos + glm::vec3(0.0f, 0.72f, 0.0f), glm::vec3(0.0f),
                 glm::vec3(0.12f, 1.45f, 0.12f), glm::vec3(0.52f, 0.30f, 0.16f), 0.08f, 8.0f);
        drawSphere(program, sphereMesh, pos + glm::vec3(0.0f, 1.50f, 0.0f), glm::vec3(0.0f),
                   glm::vec3(0.12f, 0.08f, 0.12f), glm::vec3(0.97f, 0.98f, 1.0f), 0.02f, 4.0f);
    };

    for (int i = 0; i < 9; ++i) {
        const float x = -9.3f + 1.0f * static_cast<float>(i);
        if (x > -5.7f && x < -4.3f) continue;
        drawPost(glm::vec3(x, -0.82f, -2.55f));
    }
    for (int i = 0; i < 8; ++i) {
        const float z = -3.4f - 1.0f * static_cast<float>(i);
        drawPost(glm::vec3(-9.3f, -0.82f, z));
        drawPost(glm::vec3(-0.9f, -0.82f, z));
    }

    auto drawRail = [&](const glm::vec3& pos, const glm::vec3& scale) {
        drawCube(program, cubeMesh, pos, glm::vec3(0.0f),
                 scale, glm::vec3(0.56f, 0.32f, 0.18f), 0.05f, 6.0f);
    };

    drawRail(glm::vec3(-7.7f, 0.20f, -2.55f), glm::vec3(2.5f, 0.08f, 0.08f));
    drawRail(glm::vec3(-2.5f, 0.20f, -2.55f), glm::vec3(2.5f, 0.08f, 0.08f));
    drawRail(glm::vec3(-7.7f, 0.65f, -2.55f), glm::vec3(2.5f, 0.08f, 0.08f));
    drawRail(glm::vec3(-2.5f, 0.65f, -2.55f), glm::vec3(2.5f, 0.08f, 0.08f));

    for (int i = 0; i < 7; ++i) {
        const float z = -3.9f - 1.0f * static_cast<float>(i);
        drawRail(glm::vec3(-9.3f, 0.20f, z), glm::vec3(0.08f, 0.08f, 0.52f));
        drawRail(glm::vec3(-9.3f, 0.65f, z), glm::vec3(0.08f, 0.08f, 0.52f));
        drawRail(glm::vec3(-0.9f, 0.20f, z), glm::vec3(0.08f, 0.08f, 0.52f));
        drawRail(glm::vec3(-0.9f, 0.65f, z), glm::vec3(0.08f, 0.08f, 0.52f));
    }
}

// 门前石板路与雪地脚印
void drawPathAndFootprints(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh) {
    const std::vector<glm::vec3> steppingStones = {
        {-5.0f, -0.77f, -3.40f},
        {-4.2f, -0.78f, -2.95f},
        {-3.3f, -0.79f, -2.45f},
        {-2.3f, -0.80f, -1.95f},
        {-1.2f, -0.81f, -1.25f},
        {-0.1f, -0.82f, -0.55f}
    };
    for (std::size_t i = 0; i < steppingStones.size(); ++i) {
        drawCube(program, cubeMesh, steppingStones[i], glm::vec3(0.0f, 12.0f * static_cast<float>(i), 0.0f),
                 glm::vec3(0.58f, 0.05f, 0.42f), glm::vec3(0.65f, 0.70f, 0.74f), 0.06f, 6.0f);
    }

    for (int i = 0; i < 8; ++i) {
        const float t = static_cast<float>(i) / 7.0f;
        const float x = glm::mix(1.2f, 3.0f, t) + ((i % 2 == 0) ? -0.12f : 0.12f);
        const float z = glm::mix(-0.1f, -3.0f, t);
        drawSphere(program, sphereMesh, glm::vec3(x, -0.78f, z), glm::vec3(0.0f),
                   glm::vec3(0.16f, 0.04f, 0.10f), glm::vec3(0.80f, 0.85f, 0.92f), 0.01f, 4.0f);
    }
}

// 路灯模型，夜晚通过 emissive 与点光源共同增强照明
void drawLampPost(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh,
                  const glm::vec3& groundPoint, float lampGlow) {
    drawCube(program, cubeMesh, groundPoint + glm::vec3(0.0f, 1.45f, 0.0f), glm::vec3(0.0f),
             glm::vec3(0.16f, 2.90f, 0.16f), glm::vec3(0.20f, 0.22f, 0.26f), 0.35f, 40.0f);
    drawCube(program, cubeMesh, groundPoint + glm::vec3(0.0f, 3.05f, 0.0f), glm::vec3(0.0f),
             glm::vec3(0.55f, 0.10f, 0.55f), glm::vec3(0.16f, 0.18f, 0.22f), 0.35f, 32.0f);
    drawCube(program, cubeMesh, groundPoint + glm::vec3(0.0f, 2.65f, 0.0f), glm::vec3(0.0f),
             glm::vec3(0.36f, 0.70f, 0.36f), glm::vec3(0.15f, 0.17f, 0.21f), 0.25f, 24.0f);
    drawSphere(program, sphereMesh, groundPoint + glm::vec3(0.0f, 2.65f, 0.0f), glm::vec3(0.0f),
               glm::vec3(0.18f), glm::vec3(1.0f, 0.86f, 0.55f), 0.02f, 4.0f, 0.20f + 1.60f * lampGlow);
    drawSphere(program, sphereMesh, groundPoint + glm::vec3(0.0f, 3.20f, 0.0f), glm::vec3(0.0f),
               glm::vec3(0.18f, 0.08f, 0.18f), glm::vec3(0.97f, 0.98f, 1.0f), 0.02f, 4.0f, 0.03f);
}

// 树木：主干 + 三层树冠 + 左右摆动的小枝
void drawTree(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh,
              const glm::vec3& groundPoint, float timeValue, float phase) {
    drawCube(program, cubeMesh, groundPoint + glm::vec3(0.0f, 1.15f, 0.0f), glm::vec3(0.0f),
             glm::vec3(0.45f, 2.30f, 0.45f), glm::vec3(0.42f, 0.24f, 0.12f), 0.18f, 18.0f);

    const float sway = std::sin(timeValue * 1.7f + phase) * 6.0f;
    for (int i = 0; i < 3; ++i) {
        const float scaleXZ = 1.85f - 0.28f * static_cast<float>(i);
        const float height = 2.10f + 0.78f * static_cast<float>(i);
        const float localSway = sway * (0.70f + 0.20f * static_cast<float>(i));

        drawSphere(program, sphereMesh, groundPoint + glm::vec3(0.0f, height, 0.0f),
                   glm::vec3(0.0f, 0.0f, localSway),
                   glm::vec3(scaleXZ, 0.72f, scaleXZ),
                   glm::vec3(0.14f, 0.42f, 0.18f), 0.25f, 30.0f);

        drawSphere(program, sphereMesh, groundPoint + glm::vec3(0.08f, height + 0.32f, 0.10f),
                   glm::vec3(0.0f, 0.0f, localSway),
                   glm::vec3(scaleXZ * 0.52f, 0.18f, scaleXZ * 0.52f),
                   glm::vec3(0.96f, 0.97f, 1.00f), 0.05f, 8.0f);
    }

    for (int sign : {-1, 1}) {
        const glm::vec3 rotation(0.0f, 0.0f, static_cast<float>(sign) * (28.0f + sway));
        drawCube(program, cubeMesh, groundPoint + glm::vec3(0.58f * static_cast<float>(sign), 1.60f, 0.0f),
                 rotation, glm::vec3(1.10f, 0.10f, 0.10f), glm::vec3(0.40f, 0.22f, 0.12f), 0.10f, 12.0f);
    }
}

// 房屋：主体、屋顶积雪、门窗、门灯、烟囱与烟雾
void drawHouse(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh, float timeValue, float nightBlend) {
    drawCube(program, cubeMesh, glm::vec3(-5.0f, 0.55f, -6.5f), glm::vec3(0.0f),
             glm::vec3(4.60f, 2.80f, 4.00f), glm::vec3(0.80f, 0.84f, 0.88f), 0.18f, 24.0f);

    drawCube(program, cubeMesh, glm::vec3(-6.05f, 2.20f, -6.5f), glm::vec3(0.0f, 0.0f, 24.0f),
             glm::vec3(2.70f, 0.28f, 4.35f), glm::vec3(0.48f, 0.18f, 0.15f), 0.18f, 20.0f);
    drawCube(program, cubeMesh, glm::vec3(-3.95f, 2.20f, -6.5f), glm::vec3(0.0f, 0.0f, -24.0f),
             glm::vec3(2.70f, 0.28f, 4.35f), glm::vec3(0.48f, 0.18f, 0.15f), 0.18f, 20.0f);
    drawCube(program, cubeMesh, glm::vec3(-6.05f, 2.43f, -6.5f), glm::vec3(0.0f, 0.0f, 24.0f),
             glm::vec3(2.55f, 0.12f, 4.18f), glm::vec3(0.97f, 0.98f, 1.00f), 0.04f, 6.0f);
    drawCube(program, cubeMesh, glm::vec3(-3.95f, 2.43f, -6.5f), glm::vec3(0.0f, 0.0f, -24.0f),
             glm::vec3(2.55f, 0.12f, 4.18f), glm::vec3(0.97f, 0.98f, 1.00f), 0.04f, 6.0f);

    drawCube(program, cubeMesh, glm::vec3(-5.0f, -0.20f, -4.46f), glm::vec3(0.0f),
             glm::vec3(0.90f, 1.50f, 0.18f), glm::vec3(0.35f, 0.20f, 0.10f), 0.08f, 10.0f);
    drawCube(program, cubeMesh, glm::vec3(-5.0f, -0.73f, -3.86f), glm::vec3(0.0f),
             glm::vec3(1.80f, 0.08f, 0.90f), glm::vec3(0.63f, 0.66f, 0.70f), 0.06f, 6.0f);

    const float windowGlow = 0.03f + 0.34f * nightBlend;
    drawCube(program, cubeMesh, glm::vec3(-6.30f, 0.55f, -4.46f), glm::vec3(0.0f),
             glm::vec3(0.90f, 0.90f, 0.16f), glm::vec3(0.92f, 0.74f, 0.36f), 0.04f, 4.0f, windowGlow);
    drawCube(program, cubeMesh, glm::vec3(-3.70f, 0.55f, -4.46f), glm::vec3(0.0f),
             glm::vec3(0.90f, 0.90f, 0.16f), glm::vec3(0.92f, 0.74f, 0.36f), 0.04f, 4.0f, windowGlow);
    drawSphere(program, sphereMesh, glm::vec3(-5.0f, 1.28f, -4.18f), glm::vec3(0.0f),
               glm::vec3(0.12f), glm::vec3(0.96f, 0.78f, 0.42f), 0.02f, 4.0f, 0.05f + 0.40f * nightBlend);

    drawCube(program, cubeMesh, glm::vec3(-3.75f, 3.10f, -7.20f), glm::vec3(0.0f),
             glm::vec3(0.42f, 1.20f, 0.42f), glm::vec3(0.45f, 0.18f, 0.15f), 0.10f, 12.0f);

    const glm::vec3 chimneyTop(-3.75f, 3.95f, -7.20f);
    for (int i = 0; i < 4; ++i) {
        const float t = std::fmod(timeValue * 0.16f + 0.22f * static_cast<float>(i), 1.0f);
        const glm::vec3 smokePos = chimneyTop +
            glm::vec3(0.18f * std::sin(timeValue * 1.5f + static_cast<float>(i)),
                      0.45f + 2.10f * t,
                      0.12f * std::cos(timeValue * 1.2f + static_cast<float>(i)));
        const float smokeSize = 0.18f + 0.11f * t;
        drawSphere(program, sphereMesh, smokePos, glm::vec3(0.0f),
                   glm::vec3(smokeSize), glm::vec3(0.70f, 0.72f, 0.76f), 0.02f, 4.0f, 0.10f);
    }
}

// 雪人：三个雪球 + 帽子 + 围巾 + 树枝手臂
void drawSnowman(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh) {
    const glm::vec3 snowColor(0.96f, 0.97f, 1.00f);

    drawSphere(program, sphereMesh, glm::vec3(3.8f, -0.20f, -4.2f), glm::vec3(0.0f),
               glm::vec3(1.30f), snowColor, 0.20f, 28.0f);
    drawSphere(program, sphereMesh, glm::vec3(3.8f, 0.82f, -4.2f), glm::vec3(0.0f),
               glm::vec3(0.95f), snowColor, 0.20f, 28.0f);
    drawSphere(program, sphereMesh, glm::vec3(3.8f, 1.55f, -4.2f), glm::vec3(0.0f),
               glm::vec3(0.65f), snowColor, 0.20f, 28.0f);

    drawCube(program, cubeMesh, glm::vec3(4.16f, 1.48f, -4.2f), glm::vec3(0.0f, 0.0f, 90.0f),
             glm::vec3(0.40f, 0.10f, 0.10f), glm::vec3(0.95f, 0.46f, 0.10f), 0.04f, 6.0f);

    drawSphere(program, sphereMesh, glm::vec3(4.00f, 1.63f, -3.95f), glm::vec3(0.0f),
               glm::vec3(0.06f), glm::vec3(0.06f, 0.06f, 0.08f), 0.10f, 20.0f);
    drawSphere(program, sphereMesh, glm::vec3(4.00f, 1.48f, -4.45f), glm::vec3(0.0f),
               glm::vec3(0.05f), glm::vec3(0.06f, 0.06f, 0.08f), 0.10f, 20.0f);
    drawSphere(program, sphereMesh, glm::vec3(4.00f, 1.48f, -3.95f), glm::vec3(0.0f),
               glm::vec3(0.05f), glm::vec3(0.06f, 0.06f, 0.08f), 0.10f, 20.0f);

    drawSphere(program, sphereMesh, glm::vec3(4.18f, 0.95f, -4.2f), glm::vec3(0.0f),
               glm::vec3(0.08f), glm::vec3(0.08f, 0.08f, 0.10f), 0.12f, 18.0f);
    drawSphere(program, sphereMesh, glm::vec3(4.28f, 0.62f, -4.2f), glm::vec3(0.0f),
               glm::vec3(0.08f), glm::vec3(0.08f, 0.08f, 0.10f), 0.12f, 18.0f);

    drawCube(program, cubeMesh, glm::vec3(3.8f, 1.98f, -4.2f), glm::vec3(0.0f),
             glm::vec3(0.72f, 0.18f, 0.72f), glm::vec3(0.08f, 0.08f, 0.10f), 0.10f, 14.0f);
    drawCube(program, cubeMesh, glm::vec3(3.8f, 2.30f, -4.2f), glm::vec3(0.0f),
             glm::vec3(0.46f, 0.48f, 0.46f), glm::vec3(0.08f, 0.08f, 0.10f), 0.10f, 14.0f);

    drawCube(program, cubeMesh, glm::vec3(3.0f, 1.00f, -4.2f), glm::vec3(0.0f, 0.0f, 28.0f),
             glm::vec3(1.20f, 0.06f, 0.06f), glm::vec3(0.44f, 0.24f, 0.12f), 0.08f, 8.0f);
    drawCube(program, cubeMesh, glm::vec3(4.6f, 1.00f, -4.2f), glm::vec3(0.0f, 0.0f, -28.0f),
             glm::vec3(1.20f, 0.06f, 0.06f), glm::vec3(0.44f, 0.24f, 0.12f), 0.08f, 8.0f);

    drawCube(program, cubeMesh, glm::vec3(3.8f, 1.12f, -4.86f), glm::vec3(0.0f),
             glm::vec3(0.92f, 0.16f, 0.22f), glm::vec3(0.78f, 0.12f, 0.14f), 0.08f, 10.0f);
}

// 交互树桩：主体由立方体表示，顶部覆盖一层积雪
void drawStump(unsigned int program, const Mesh& cubeMesh, const Mesh& sphereMesh) {
    const glm::mat4 stumpTransform = composeTransform(gStump.position, gStump.rotation);
    drawMesh(program, cubeMesh, scaledModel(stumpTransform, gStump.scale),
             glm::vec3(0.45f, 0.26f, 0.12f), 0.12f, 16.0f, 0.0f);

    const glm::mat4 snowCapTransform = glm::translate(stumpTransform, glm::vec3(0.0f, gStump.scale.y * 0.55f, 0.0f));
    drawMesh(program, sphereMesh, scaledModel(snowCapTransform, glm::vec3(gStump.scale.x * 0.55f, gStump.scale.y * 0.18f, gStump.scale.z * 0.55f)),
              glm::vec3(0.97f, 0.98f, 1.00f), 0.04f, 6.0f, 0.05f);
}

int main() {
    // 1. 初始化 GLFW 与 OpenGL 上下文
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW.\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    gWindow = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Snow Scene - Winter Landscape", nullptr, nullptr);
    if (gWindow == nullptr) {
        std::cerr << "Failed to create GLFW window.\n";
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(gWindow);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(gWindow, framebuffer_size_callback);
    glfwSetCursorPosCallback(gWindow, cursor_position_callback);
    glfwSetMouseButtonCallback(gWindow, mouse_button_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD.\n";
        glfwDestroyWindow(gWindow);
        glfwTerminate();
        return -1;
    }

    // 2. 注册回调并初始化相机、粒子数据
    updateCameraFront();
    initializeSnow();
    initializeStars();

    // 3. 开启深度测试、透明混合和点精灵尺寸控制
    glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);

    std::cout
        << "===== 雪中即景 控制说明 =====\n"
        << "W/S         : 视角上移 / 下移\n"
        << "A/D         : 视角左移 / 右移\n"
        << "N           : 白天 / 黑夜切换\n"
        << "F           : 下雪 / 停雪切换\n"
        << "鼠标左键拖动 : 平移树桩\n"
        << "Ctrl        : 旋转树桩\n"
        << "方向键       : 拉伸树桩\n"
        << "鼠标右键或 R : 树桩恢复初始状态\n"
        << "Esc         : 退出\n";

    const char* litVertexShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in vec3 aNormal;

        out vec3 FragPos;
        out vec3 Normal;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            vec4 worldPos = model * vec4(aPos, 1.0);
            FragPos = worldPos.xyz;
            Normal = mat3(transpose(inverse(model))) * aNormal;
            gl_Position = projection * view * worldPos;
        }
    )";

    const char* litFragmentShader = R"(
        #version 330 core
        struct DirLight {
            vec3 direction;
            vec3 ambient;
            vec3 diffuse;
            vec3 specular;
        };

        struct PointLight {
            vec3 position;
            vec3 color;
            float intensity;
        };

        #define MAX_POINT_LIGHTS 8

        in vec3 FragPos;
        in vec3 Normal;
        out vec4 FragColor;

        uniform vec3 viewPos;
        uniform vec3 objectColor;
        uniform float specularStrength;
        uniform float shininess;
        uniform float emissiveStrength;
        uniform DirLight dirLight;
        uniform PointLight pointLights[MAX_POINT_LIGHTS];
        uniform int pointLightCount;

        vec3 calcDirLight(vec3 normal, vec3 viewDir) {
            vec3 lightDir = normalize(-dirLight.direction);
            float diff = max(dot(normal, lightDir), 0.0);
            vec3 halfDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfDir), 0.0), shininess) * specularStrength;
            vec3 ambient = dirLight.ambient * objectColor;
            vec3 diffuse = dirLight.diffuse * diff * objectColor;
            vec3 specular = dirLight.specular * spec;
            return ambient + diffuse + specular;
        }

        vec3 calcPointLight(PointLight light, vec3 normal, vec3 viewDir) {
            if (light.intensity <= 0.0001) return vec3(0.0);
            vec3 lightDir = normalize(light.position - FragPos);
            float diff = max(dot(normal, lightDir), 0.0);
            vec3 halfDir = normalize(lightDir + viewDir);
            float spec = pow(max(dot(normal, halfDir), 0.0), shininess) * specularStrength;
            float distance = length(light.position - FragPos);
            float attenuation = light.intensity / (1.0 + 0.09 * distance + 0.032 * distance * distance);
            vec3 ambient = 0.05 * objectColor * light.color;
            vec3 diffuse = diff * objectColor * light.color;
            vec3 specular = spec * light.color;
            return (ambient + diffuse + specular) * attenuation;
        }

        void main() {
            vec3 normal = normalize(Normal);
            vec3 viewDir = normalize(viewPos - FragPos);

            vec3 result = calcDirLight(normal, viewDir);
            for (int i = 0; i < pointLightCount; ++i) {
                result += calcPointLight(pointLights[i], normal, viewDir);
            }

            result += objectColor * emissiveStrength;
            FragColor = vec4(result, 1.0);
        }
    )";

    const char* snowVertexShader = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;
        layout (location = 1) in float aSize;
        layout (location = 2) in float aAlphaScale;

        out float AlphaScale;

        uniform mat4 view;
        uniform mat4 projection;

        void main() {
            vec4 clipPos = projection * view * vec4(aPos, 1.0);
            gl_Position = clipPos;
            gl_PointSize = clamp(aSize * (180.0 / max(clipPos.w, 0.2)), 1.5, 12.0);
            AlphaScale = aAlphaScale;
        }
    )";

    const char* snowFragmentShader = R"(
        #version 330 core
        in float AlphaScale;
        out vec4 FragColor;

        uniform vec3 particleColor;
        uniform float baseAlpha;

        void main() {
            vec2 center = gl_PointCoord * 2.0 - 1.0;
            float dist = dot(center, center);
            if (dist > 1.0) discard;
            float alpha = (1.0 - dist) * baseAlpha * AlphaScale;
            FragColor = vec4(particleColor, alpha);
        }
    )";

    // 4. 编译着色器程序
    const unsigned int litProgram = createProgram(litVertexShader, litFragmentShader);
    const unsigned int snowProgram = createProgram(snowVertexShader, snowFragmentShader);

    // 5. 创建场景基础网格
    Mesh cubeMesh = createCubeMesh();
    Mesh sphereMesh = createSphereMesh(36, 18);

    // 6. 创建雪花与星星粒子缓冲
    unsigned int snowVAO = 0;
    unsigned int snowVBO = 0;
    unsigned int starVAO = 0;
    unsigned int starVBO = 0;
    glGenVertexArrays(1, &snowVAO);
    glGenBuffers(1, &snowVBO);
    glBindVertexArray(snowVAO);
    glBindBuffer(GL_ARRAY_BUFFER, snowVBO);
    glBufferData(GL_ARRAY_BUFFER, SNOW_PARTICLE_COUNT * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glGenVertexArrays(1, &starVAO);
    glGenBuffers(1, &starVBO);
    glBindVertexArray(starVAO);
    glBindBuffer(GL_ARRAY_BUFFER, starVBO);
    glBufferData(GL_ARRAY_BUFFER, STAR_COUNT * 5 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    std::vector<float> snowVertexData(static_cast<std::size_t>(SNOW_PARTICLE_COUNT) * 5U, 0.0f);
    std::vector<float> starVertexData(static_cast<std::size_t>(STAR_COUNT) * 5U, 0.0f);

    float lastFrame = 0.0f;
    while (!glfwWindowShouldClose(gWindow)) {
        const float currentFrame = static_cast<float>(glfwGetTime());
        const float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // 7. 输入处理与状态更新
        processInput(gWindow, deltaTime);

        // 昼夜平滑过渡
        const float nightStep = gNightTarget ? 1.0f : -1.0f;
        gNightBlend = std::clamp(gNightBlend + nightStep * deltaTime * 0.55f, 0.0f, 1.0f);

        // 下雪与停雪平滑过渡
        const float snowStep = gSnowTarget ? 1.0f : -1.0f;
        gSnowAmount = std::clamp(gSnowAmount + snowStep * deltaTime * 0.70f, 0.0f, 1.0f);

        // 8. 更新雪花粒子
        for (std::size_t i = 0; i < gSnowParticles.size(); ++i) {
            auto& particle = gSnowParticles[i];
            particle.position.y -= particle.speed * deltaTime * (0.75f + 0.85f * gSnowAmount);
            particle.position.x += std::sin(currentFrame * 0.85f + particle.phase) * particle.drift * deltaTime;
            particle.position.z += particle.drift * 0.20f * deltaTime;

            if (particle.position.y < -0.90f ||
                std::abs(particle.position.x) > 16.5f ||
                std::abs(particle.position.z) > 16.5f) {
                resetSnowParticle(particle, false);
            }

            snowVertexData[i * 5 + 0] = particle.position.x;
            snowVertexData[i * 5 + 1] = particle.position.y;
            snowVertexData[i * 5 + 2] = particle.position.z;
            snowVertexData[i * 5 + 3] = particle.size;
            snowVertexData[i * 5 + 4] = 1.0f;
        }

        glBindBuffer(GL_ARRAY_BUFFER, snowVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(snowVertexData.size() * sizeof(float)), snowVertexData.data());

        // 9. 更新星星闪烁强度
        for (std::size_t i = 0; i < gStars.size(); ++i) {
            const auto& star = gStars[i];
            const float twinkle = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(currentFrame * 1.8f + star.phase));
            starVertexData[i * 5 + 0] = star.position.x;
            starVertexData[i * 5 + 1] = star.position.y;
            starVertexData[i * 5 + 2] = star.position.z;
            starVertexData[i * 5 + 3] = star.size;
            starVertexData[i * 5 + 4] = twinkle;
        }

        glBindBuffer(GL_ARRAY_BUFFER, starVBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(starVertexData.size() * sizeof(float)), starVertexData.data());

        // 10. 计算本帧视图/投影矩阵与场景光照参数
        const glm::vec3 skyDay(0.56f, 0.78f, 0.97f);
        const glm::vec3 skyNight(0.03f, 0.05f, 0.12f);
        const glm::vec3 skyColor = glm::mix(skyDay, skyNight, gNightBlend);
        glClearColor(skyColor.r, skyColor.g, skyColor.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        const glm::mat4 view = glm::lookAt(gCameraPos, gCameraPos + gCameraFront, gCameraUp);
        const glm::mat4 projection = glm::perspective(glm::radians(45.0f),
            static_cast<float>(gFramebufferWidth) / static_cast<float>(gFramebufferHeight), 0.1f, 100.0f);

        const glm::vec3 sunPosition = glm::vec3(9.5f, 10.5f, -18.0f);
        const glm::vec3 moonPosition = glm::vec3(-10.0f, 9.0f, -18.0f);
        const glm::vec3 skyOrbPosition = glm::mix(sunPosition, moonPosition, gNightBlend);
        const glm::vec3 skyOrbColor = glm::mix(glm::vec3(1.00f, 0.88f, 0.45f), glm::vec3(0.78f, 0.86f, 1.00f), gNightBlend);
        const float lampGlowA = (0.88f + 0.12f * std::sin(currentFrame * 4.0f + 0.2f)) * gNightBlend;
        const float lampGlowB = (0.84f + 0.16f * std::sin(currentFrame * 4.3f + 1.1f)) * gNightBlend;

        std::vector<PointLightData> pointLights = {
            { glm::vec3(-6.30f, 0.55f, -4.05f), glm::vec3(0.96f, 0.78f, 0.42f), 0.48f * gNightBlend },
            { glm::vec3(-3.70f, 0.55f, -4.05f), glm::vec3(0.96f, 0.78f, 0.42f), 0.48f * gNightBlend },
            { glm::vec3(-5.00f, 1.28f, -4.18f), glm::vec3(0.98f, 0.80f, 0.46f), 0.32f * gNightBlend },
            { glm::vec3(-1.60f, 1.80f, -0.60f), glm::vec3(1.00f, 0.86f, 0.58f), 2.20f * lampGlowA },
            { glm::vec3(5.60f, 1.80f, 0.80f), glm::vec3(1.00f, 0.86f, 0.58f), 2.00f * lampGlowB },
            { moonPosition, glm::vec3(0.62f, 0.72f, 1.00f), 0.28f * gNightBlend }
        };

        // 11. 第一层：先绘制夜空星星粒子
        glDepthMask(GL_FALSE);
        glUseProgram(snowProgram);
        setMat4(snowProgram, "view", view);
        setMat4(snowProgram, "projection", projection);
        setVec3(snowProgram, "particleColor", glm::vec3(0.92f, 0.95f, 1.00f));
        setFloat(snowProgram, "baseAlpha", 0.90f * gNightBlend);
        glBindVertexArray(starVAO);
        glDrawArrays(GL_POINTS, 0, STAR_COUNT);
        glDepthMask(GL_TRUE);

        // 12. 第二层：绘制三维场景主体
        glUseProgram(litProgram);
        setMat4(litProgram, "view", view);
        setMat4(litProgram, "projection", projection);
        setVec3(litProgram, "viewPos", gCameraPos);
        setVec3(litProgram, "dirLight.direction", glm::mix(glm::vec3(-0.25f, -1.00f, -0.20f), glm::vec3(0.28f, -1.00f, -0.10f), gNightBlend));
        setVec3(litProgram, "dirLight.ambient", glm::mix(glm::vec3(0.34f, 0.36f, 0.40f), glm::vec3(0.04f, 0.05f, 0.07f), gNightBlend));
        setVec3(litProgram, "dirLight.diffuse", glm::mix(glm::vec3(0.82f, 0.82f, 0.76f), glm::vec3(0.18f, 0.21f, 0.30f), gNightBlend));
        setVec3(litProgram, "dirLight.specular", glm::mix(glm::vec3(0.22f, 0.22f, 0.20f), glm::vec3(0.10f, 0.12f, 0.18f), gNightBlend));
        setPointLights(litProgram, pointLights);

        drawGround(litProgram, cubeMesh);
        drawBackgroundHills(litProgram, sphereMesh);
        drawPathAndFootprints(litProgram, cubeMesh, sphereMesh);
        drawYardFence(litProgram, cubeMesh, sphereMesh);
        drawHouse(litProgram, cubeMesh, sphereMesh, currentFrame, gNightBlend);
        drawSnowman(litProgram, cubeMesh, sphereMesh);
        drawBush(litProgram, sphereMesh, glm::vec3(-7.2f, -0.35f, -3.25f), glm::vec3(0.90f, 0.60f, 0.75f));
        drawBush(litProgram, sphereMesh, glm::vec3(-2.7f, -0.34f, -3.10f), glm::vec3(0.85f, 0.55f, 0.72f));
        drawBush(litProgram, sphereMesh, glm::vec3(1.25f, -0.40f, 1.35f), glm::vec3(0.75f, 0.45f, 0.62f));

        drawTree(litProgram, cubeMesh, sphereMesh, glm::vec3(-9.0f, -0.85f, -8.5f), currentFrame, 0.0f);
        drawTree(litProgram, cubeMesh, sphereMesh, glm::vec3(-1.8f, -0.85f, -8.2f), currentFrame, 1.2f);
        drawTree(litProgram, cubeMesh, sphereMesh, glm::vec3(7.5f, -0.85f, -7.0f), currentFrame, 2.2f);
        drawTree(litProgram, cubeMesh, sphereMesh, glm::vec3(10.5f, -0.85f, -2.8f), currentFrame, 0.7f);
        drawLampPost(litProgram, cubeMesh, sphereMesh, glm::vec3(-1.60f, -0.85f, -0.60f), lampGlowA);
        drawLampPost(litProgram, cubeMesh, sphereMesh, glm::vec3(5.60f, -0.85f, 0.80f), lampGlowB);

        drawStump(litProgram, cubeMesh, sphereMesh);

        drawSphere(litProgram, sphereMesh, skyOrbPosition, glm::vec3(0.0f),
                   glm::vec3(1.10f), skyOrbColor, 0.0f, 2.0f, 1.60f);
        drawSphere(litProgram, sphereMesh, skyOrbPosition, glm::vec3(0.0f),
                   glm::vec3(1.75f, 1.75f, 0.28f), glm::mix(glm::vec3(1.0f, 0.92f, 0.60f), glm::vec3(0.70f, 0.78f, 1.0f), gNightBlend),
                   0.0f, 2.0f, 0.10f + 0.18f * gNightBlend);

        // 13. 第三层：叠加下雪粒子
        glDepthMask(GL_FALSE);
        glUseProgram(snowProgram);
        setMat4(snowProgram, "view", view);
        setMat4(snowProgram, "projection", projection);
        setVec3(snowProgram, "particleColor", glm::mix(glm::vec3(1.0f, 1.0f, 1.0f), glm::vec3(0.90f, 0.94f, 1.0f), gNightBlend));
        setFloat(snowProgram, "baseAlpha", 0.95f * gSnowAmount);
        glBindVertexArray(snowVAO);
        glDrawArrays(GL_POINTS, 0, SNOW_PARTICLE_COUNT);
        glDepthMask(GL_TRUE);

        glfwSwapBuffers(gWindow);
        glfwPollEvents();
    }

    // 14. 资源释放
    glDeleteBuffers(1, &snowVBO);
    glDeleteVertexArrays(1, &snowVAO);
    glDeleteBuffers(1, &starVBO);
    glDeleteVertexArrays(1, &starVAO);
    destroyMesh(cubeMesh);
    destroyMesh(sphereMesh);
    glDeleteProgram(litProgram);
    glDeleteProgram(snowProgram);
    glfwDestroyWindow(gWindow);
    glfwTerminate();
    return 0;
}
