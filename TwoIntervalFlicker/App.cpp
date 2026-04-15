#include "app.h"
#include <iostream>
#include "Utils.h"

static const std::string VERT_SRC = R"(
#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord    = aTexCoord;
}
)";

static const std::string FRAG_SRC = R"(
#version 460 core
in  vec2      TexCoord;
out vec4      FragColor;

uniform sampler2D uTexture;

void main() {
    FragColor = texture(uTexture, TexCoord);
}
)";


static const float QUAD_VERTS[] = {
    // pos          // uv
    -1.0f,  1.0f,   0.0f, 1.0f,   // top-left
    -1.0f, -1.0f,   0.0f, 0.0f,   // bottom-left
     1.0f, -1.0f,   1.0f, 0.0f,   // bottom-right

    -1.0f,  1.0f,   0.0f, 1.0f,   // top-left
     1.0f, -1.0f,   1.0f, 0.0f,   // bottom-right
     1.0f,  1.0f,   1.0f, 1.0f,   // top-right
};


App::App(int width, int height, const std::string& title) : m_width(width), m_height(height), m_title(title) { }

App::~App() {
    if (m_texOrig) glDeleteTextures(1, &m_texOrig);
    if (m_texDec) glDeleteTextures(1, &m_texDec);
    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool App::init(const std::string& configPath) {
    if (!m_config.load(configPath))   return false;
    if (m_config.trials.empty()) {
        Utils::FatalError("[App] No trials in config.");
        return false;
    }

    // randomize order of trials + flickers
    Utils::ShuffleTrials(m_config.trials);
    Utils::ShuffleFlickers(m_config.trials);

    if (!glfwInit()) {
        Utils::FatalError("[App] Failed to initialize GLFW");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_window = glfwCreateWindow(m_width, m_height, m_title.c_str(), nullptr, nullptr); // windowed mode for debug

    // to do: finish for full screen 
    // 
    //GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    //const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    //m_window = glfwCreateWindow(mode->width, mode->height, m_title.c_str(), monitor, nullptr);
    if (!m_window) {
       
        glfwTerminate(); 
        Utils::FatalError("[App] Failed to create GLFW window");
        return false;
    }

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        Utils::FatalError("[App] Failed to initialize GLAD");
        return false;
    }

    glViewport(0, 0, m_width, m_height);

    //build shader
    if (!m_shader.load(VERT_SRC, FRAG_SRC)) return false;

    //quad geometry building
    if (!initQuad()) return false;



    //allocate texture slots
    glGenTextures(1, &m_texOrig);
    glGenTextures(1, &m_texDec);
    glGenTextures(1, &m_texStart);
    glGenTextures(1, &m_texWaitResponse);

    loadInstructionsTextures();

    m_texture = m_texStart;
    m_phase = TrialPhase::StartInstructions;
    //// load the first image
    //m_config.trials[m_trialIndex].flickerIndex == 0 ? m_phase = TrialPhase::ShowFlicker : TrialPhase::ShowOriginal;
    //// initial load of the first 2 images in trial
    //loadTextures(m_config.trials[m_trialIndex].L_orig, m_config.trials[m_trialIndex].L_dec);
    //// flicker will start first frame on original 

    m_phaseStart = glfwGetTime();


    return true;
}

// quad init

bool App::initQuad() {
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);

    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);

    // layout(location = 0): vec2 position  — bytes 0..7
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // layout(location = 1): vec2 texcoord  — bytes 8..15
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    return true;
}

// main loop

void App::run() {
    while (!glfwWindowShouldClose(m_window) && m_phase != TrialPhase::Done) {
        update();
        render();
        glfwSwapBuffers(m_window);
        glfwPollEvents();
    }

    printResults();
}

// update loop

void App::update() {
    const double now = glfwGetTime();
    const double elapsed = now - m_phaseStart;
    const ImagePaths& img = m_config.trials[m_trialIndex];

    // i think there should be a better way to do this. image texture should only be set once per phase.

    // show original, no flicker
    if ((m_phase == TrialPhase::ShowOriginal)) {
        m_texture = m_texOrig;
        //loadTexture(img.L_orig);
        if (elapsed >= timeoutDuration) {
             advancePhase();
             return;
        }    
    }

    if (m_phase == TrialPhase::ShowWaitScreen) {
        if (elapsed >= waitTimeoutDuration) {
            advancePhase();
            return;
        }
    }

    // flicker phase
    if (m_phase == TrialPhase::ShowFlicker ) {
        if (elapsed >= timeoutDuration) {
            advancePhase();
            return;
        }
        const double flickerInterval = 1.0 / flickerRate; // seconds per swap
        if (now - m_flickerLast >= flickerInterval) {
            m_flickerLast = now;
            m_flickerOnOrig = !m_flickerOnOrig;
            m_texture = m_flickerOnOrig ? m_texOrig : m_texDec;
        }

    }
}

// rendering

void App::render() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_phase == TrialPhase::ShowOriginal || m_phase == TrialPhase::ShowFlicker)
    {
        renderTexture();
    }

    else if (m_phase == TrialPhase::StartInstructions)
    {
        m_texture = m_texStart;
        renderTexture();
    }

    else if (m_phase == TrialPhase::WaitForResponse)
    {
        m_texture = m_texWaitResponse;
        renderTexture();
    }

    // between trials: black screen
}

// renderTexture — bind shader + quad + texture, draw 6 verts

void App::renderTexture() {
    m_shader.use();
    m_shader.setInt("uTexture", 0); // sampler uses texture unit 0

    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, m_texture);

    

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// phases

void App::advancePhase() {
    const double now = glfwGetTime();

    if (m_phase == TrialPhase::ShowOriginal) {
        if (m_config.trials[m_trialIndex].flickerIndex == 0) { // this is the second image shown, wait for response is next
            m_phase = TrialPhase::WaitForResponse;
            m_phaseStart = now;
            m_responseStart = now;
        }
        else { // this is the first image, show wait screen next
            m_phase = TrialPhase::ShowWaitScreen;
            m_phaseStart = now;
        }

    }
    else if (m_phase == TrialPhase::ShowWaitScreen) {
        if (m_config.trials[m_trialIndex].flickerIndex == 0) { // flicker has already happened, next phase is show original
            m_phase = TrialPhase::ShowOriginal;
            m_phaseStart = now;
        }
        else { // flicker is next to happen
            m_phase = TrialPhase::ShowFlicker;    
            m_phaseStart = now;
        }
        //load the next 2 images in the trial
        loadTextures(m_config.trials[m_trialIndex].L_orig, m_config.trials[m_trialIndex].L_dec);
    }
    else if (m_phase == TrialPhase::ShowFlicker) {
        if (m_config.trials[m_trialIndex].flickerIndex == 0) { // this is the first image, show wait screen next
            m_phase = TrialPhase::ShowWaitScreen;
            m_phaseStart = now;
        }
        else { // this is the second image shown, wait for response is next
            m_phase = TrialPhase::WaitForResponse;
            m_phaseStart = now;
            m_responseStart = now;
        }

        
    }
}

void App::recordResponse(int key) {
    if (m_phase != TrialPhase::WaitForResponse) return;

    TrialResult result;
    result.imageName = m_config.trials[m_trialIndex].name;
    result.responseKey = key;
    result.reactionTime = glfwGetTime() - m_responseStart; // record reaction time?
    m_results.push_back(result);

    // to do: write responses to csv
    m_trialIndex++;
    if (m_trialIndex >= static_cast<int>(m_config.trials.size())) {
        m_phase = TrialPhase::Done;
    }
    else {
        m_config.trials[m_trialIndex].flickerIndex == 0 ? m_phase = TrialPhase::ShowFlicker : TrialPhase::ShowOriginal;
        m_phaseStart = glfwGetTime();
        loadTextures(m_config.trials[m_trialIndex].L_orig, m_config.trials[m_trialIndex].L_dec);
    }
}

void App::loadInstructionsTextures() {
   

    cv::Mat responseImg = cv::imread("responsescreen_L.ppm", cv::IMREAD_COLOR);
    if (responseImg.empty()) {
        Utils::FatalError("[App] Failed to load image: responsescreen_L.ppm");
        return;
    }

    cv::cvtColor(responseImg, responseImg, cv::COLOR_BGR2RGB);
    cv::flip(responseImg, responseImg, 0);  // OpenGL origin is bottom-left

    glBindTexture(GL_TEXTURE_2D, m_texWaitResponse);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
        responseImg.cols, responseImg.rows,
        0, GL_RGB, GL_UNSIGNED_BYTE, responseImg.data);

    glBindTexture(GL_TEXTURE_2D, 0);

    // start instructions
    cv::Mat startImg = cv::imread("startscreen_L.ppm", cv::IMREAD_COLOR);
    if (startImg.empty()) {
        Utils::FatalError("[App] Failed to load image: startscreen_L.ppm");
        return;
    }

    cv::cvtColor(startImg, startImg, cv::COLOR_BGR2RGB);
    cv::flip(startImg, startImg, 0);  // OpenGL origin is bottom-left

    glBindTexture(GL_TEXTURE_2D, m_texStart);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
        startImg.cols, startImg.rows,
        0, GL_RGB, GL_UNSIGNED_BYTE, startImg.data);

    glBindTexture(GL_TEXTURE_2D, 0);

}

// loads the original and dec textures 
void App::loadTextures(const fs::path& origImagePath, const fs::path& decImagePath) {
    if (origImagePath.empty() || decImagePath.empty()) {
        Utils::FatalError("[App] Image path is empty.");
        return;
    }

    cv::Mat imgOrig = cv::imread(origImagePath.string(), cv::IMREAD_COLOR);
    if (imgOrig.empty()) {
        Utils::FatalError("[App] Failed to load image: " + origImagePath.string());
        return;
    }

    cv::cvtColor(imgOrig, imgOrig, cv::COLOR_BGR2RGB);
    cv::flip(imgOrig, imgOrig, 0);  // OpenGL origin is bottom-left

    glBindTexture(GL_TEXTURE_2D, m_texOrig);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
        imgOrig.cols, imgOrig.rows,
        0, GL_RGB, GL_UNSIGNED_BYTE, imgOrig.data);

    glBindTexture(GL_TEXTURE_2D, 0);


    cv::Mat imgDec = cv::imread(decImagePath.string(), cv::IMREAD_COLOR);
    if (imgDec.empty()) {
        Utils::FatalError("[App] Failed to load image: " + decImagePath.string());
        return;
    }

    cv::cvtColor(imgDec, imgDec, cv::COLOR_BGR2RGB);
    cv::flip(imgDec, imgDec, 0);  // OpenGL origin is bottom-left

    glBindTexture(GL_TEXTURE_2D, m_texDec);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
        imgDec.cols, imgDec.rows,
        0, GL_RGB, GL_UNSIGNED_BYTE, imgDec.data);

    glBindTexture(GL_TEXTURE_2D, 0);
}

// results (to do: csv)

void App::printResults() const {
    //std::cout << "--- Results ---";
    //for (const auto& r : m_results) {
    //    std::cout << "  " << r.imageName
    //        << "  " << (r.responseKey == GLFW_KEY_LEFT ? "LEFT " : "RIGHT")
    //        << "  RT: " << r.reactionTime << "s";
    //}
}

// GLFW callbacks

void App::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void App::keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (action != GLFW_PRESS) return;
    App* app = static_cast<App*>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, true);
        return;
    }

    if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT)
        app->recordResponse(key);

    if (key == GLFW_KEY_ENTER && action == GLFW_PRESS)
    {
        if (app->m_phase == TrialPhase::StartInstructions)
        {
            app->m_trialIndex = 0;

            const auto& img = app->m_config.trials[0];
            app->loadTextures(img.L_orig, img.L_dec);


            app->m_phase = (img.flickerIndex == 0) ? TrialPhase::ShowFlicker : TrialPhase::ShowOriginal;
            app->m_phaseStart = glfwGetTime();
        }
    }

}