#include "app.h"
#include <iostream>
#include "Utils.h"
#include "csv.h"

static const std::string VERT_SRC = R"(
#version 460 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
uniform bool uMirror;

out vec2 TexCoord;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    TexCoord = uMirror ? vec2(1.0 - aTexCoord.x, aTexCoord.y) : aTexCoord; 
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


App::App(int variant) { m_variant = variant; }

App::~App() {
    //if (m_texOrig) glDeleteTextures(1, &m_texOrig);
    //if (m_texDec) glDeleteTextures(1, &m_texDec);
    //if (m_texStart) glDeleteTextures(1, &m_texStart);
    //if (m_texWaitResponse) glDeleteTextures(1, &m_texWaitResponse);

    GLuint textures[] = { m_texOrig_L, m_texDec_L, m_texStart_L, m_texWaitResponse_L, m_texOrig_R, m_texDec_R, m_texStart_R, m_texWaitResponse_R }; // textures can be batch deleted
    glDeleteTextures(8, textures);

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


    GLFWmonitor* monitor = glfwGetPrimaryMonitor(); // assume the same resolution across the monitors
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    m_width = mode->width;
    m_height = mode->height;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // request float frame buffers for hdr imaging
    glfwWindowHint(GLFW_RED_BITS, 16);
    glfwWindowHint(GLFW_GREEN_BITS, 16);
    glfwWindowHint(GLFW_BLUE_BITS, 16);
    glfwWindowHint(GLFW_ALPHA_BITS, 16);

    glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);

    m_window = glfwCreateWindow(m_width * 2, m_height, "Flicker Experiment", nullptr, nullptr);


    //if (!m_window) {
    //    glfwTerminate(); 
    //    Utils::FatalError("[App] Failed to create GLFW window");
    //    return false;
    //}

    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // enable v sync (should limit frame rate to monitor's refresh rate)
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
    glGenTextures(1, &m_texOrig_L);
    glGenTextures(1, &m_texDec_L);
    glGenTextures(1, &m_texStart_L);
    glGenTextures(1, &m_texWaitResponse_L);
    glGenTextures(1, &m_texOrig_R);
    glGenTextures(1, &m_texDec_R);
    glGenTextures(1, &m_texStart_R);
    glGenTextures(1, &m_texWaitResponse_R);

    loadInstructionsTextures();

    m_texture_L = m_texStart_L;
    m_phase = TrialPhase::StartInstructions;

    std::string variantName;
    // variant
    switch (m_variant) {
        case 0:
            variantName = "Full Image";
            break;
        case 1:
            variantName = "Peripheral Crop";
            break;
        case 2:
            variantName = "Local Flicker";
            break;

    }


    // load the csv
    m_csv.init(m_config.participantID, m_config.participantAge, m_config.participantGender, "Flicker Paradigm", variantName, { "Index", "Image", "Viewing Mode", "Answer", "Actual", "Reaction Time (s)" }, m_config.outputDirectory.string());

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

    // maybe refactor this state machine at some point

    // show original, no flicker
    if ((m_phase == TrialPhase::ShowOriginal)) {
        m_texture_L = m_texOrig_L;
        m_texture_R = m_texOrig_R;
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
            m_texture_L = m_flickerOnOrig ? m_texOrig_L : m_texDec_L;
            m_texture_R = m_flickerOnOrig ? m_texOrig_R : m_texDec_R;
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
        m_texture_L = m_texStart_L;
        m_texture_R = m_texStart_R;
        renderTexture();
    }

    else if (m_phase == TrialPhase::WaitForResponse)
    {
        m_texture_L = m_texWaitResponse_L;
        m_texture_R = m_texWaitResponse_R;
        renderTexture();
    }

    // between trials: black screen
}

// renderTexture — bind shader + quad + texture, draw 6 verts

void App::renderTexture() {
    m_shader.use();
    m_shader.setInt("uTexture", 0);
    m_shader.setBool("uMirror", true); // mirror 

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_quadVAO);

    // left monitor
    glViewport(0, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, m_texture_L);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // right monitor (assuming same width as left)
    glViewport(m_width, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, m_texture_R);
    
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
        loadTextures(m_config.trials[m_trialIndex]);
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
    result.answer = key == GLFW_KEY_LEFT ? 0 : 1;
    result.actual = m_config.trials[m_trialIndex].flickerIndex; 
    result.index = m_trialIndex;

    switch (m_config.trials[m_trialIndex].viewingMode) {
        case 0:
            result.viewingMode = "Stereo";
            break;
        case 1:
            result.viewingMode = "Left";
            break;
        case 2: 
            result.viewingMode = "Right";
            break;
        default:
            result.viewingMode = "N/A";
            break;
    }

    result.reactionTime = glfwGetTime() - m_responseStart; // record reaction time?
    m_results.push_back(result);
    //headers are: { "Index", "Image", "Viewing Mode", "Answer", "Actual", "Reaction Time" }
    std::vector<std::string> resultRow = {std::to_string(result.index), result.imageName, result.viewingMode, std::to_string(result.answer), std::to_string(result.actual), std::to_string(result.reactionTime)};
    m_csv.writeRow(resultRow);
    m_trialIndex++;
    if (m_trialIndex >= static_cast<int>(m_config.trials.size())) {
        m_phase = TrialPhase::Done;
    }
    else {
        m_phase =  (m_config.trials[m_trialIndex].flickerIndex) == 0 ? TrialPhase::ShowFlicker : TrialPhase::ShowOriginal;
        m_phaseStart = glfwGetTime();
        loadTextures(m_config.trials[m_trialIndex]);
    }
}

void App::loadInstructionsTextures() {
   
    loadTexture("responsescreen_L.ppm", m_texWaitResponse_L);
    loadTexture("startscreen_L.ppm", m_texStart_L);

    loadTexture("responsescreen_R.ppm", m_texWaitResponse_R);
    loadTexture("startscreen_R.ppm", m_texStart_R);

}

// loads the original and dec textures, based on stereo settings in config
// 0 = stereo
// 1 = left only
// 2 = right only

void App::loadTextures(const ImagePaths img) {

    switch (img.viewingMode) {

    case 0: // stereo
        loadTexture(img.L_orig.string(), m_texOrig_L);
        loadTexture(img.R_orig.string(), m_texOrig_R);

        loadTexture(img.L_dec.string(), m_texDec_L);
        loadTexture(img.R_dec.string(), m_texDec_R);
        break;
    case 1: // left only
        loadTexture(img.L_orig.string(), m_texOrig_L);
        loadTexture(img.L_orig.string(), m_texOrig_R);

        loadTexture(img.L_dec.string(), m_texDec_L);
        loadTexture(img.L_dec.string(), m_texDec_R);

        break;
    case 2: // right only
        loadTexture(img.R_orig.string(), m_texOrig_L);
        loadTexture(img.R_orig.string(), m_texOrig_R);

        loadTexture(img.R_dec.string(), m_texDec_L);
        loadTexture(img.R_dec.string(), m_texDec_R);
        break;

    default:
        Utils::FatalError("[App] viewing mode is not valid. Must be 0, 1, or 2. Is : " + img.viewingMode);
    }
}

// helper to load texture into id
void App::loadTexture(const std::string& path, GLuint textureID) {
    cv::Mat img = cv::imread(path, cv::IMREAD_ANYDEPTH | cv::IMREAD_COLOR);
    

    if (img.empty()) {
        Utils::FatalError("[App] Failed to load image: " + path);
        return;
    }
    float scale = (img.depth() == CV_16U) ? 1.0f / 65535.0f : 1.0f / 255.0f; // scale image if it is 16 bits
    img.convertTo(img, CV_32FC3, scale);
    cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
    cv::flip(img, img, 0);

    GLint internalFormat = (img.depth() == CV_16U) ? GL_RGB16F : GL_RGB8;

    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, img.cols, img.rows, 0, GL_RGB, GL_FLOAT, img.data);

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
            app->loadTextures(img);

            app->m_phase = (img.flickerIndex == 0) ? TrialPhase::ShowFlicker : TrialPhase::ShowOriginal;
            app->m_phaseStart = glfwGetTime();
        }
    }

}