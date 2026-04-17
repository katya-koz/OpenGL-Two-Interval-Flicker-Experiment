#include "app.h"
#include <iostream>
#include "Utils.h"
#include "csv.h"
#include <mmsystem.h>
#include <Windows.h>

#pragma comment(lib, "winmm.lib")

// SHADERS -> to be moved into their own files.

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

static const std::string CROSSHAIR_VERT_SRC = R"(
#version 460 core
layout (location = 0) in vec2 aPos;

uniform float uAspect;

void main() {
    gl_Position = vec4(aPos.x / uAspect, aPos.y, 0.0, 1.0);
}
)";

static const std::string CROSSHAIR_FRAG_SRC = R"(
#version 460 core
out vec4 FragColor;

uniform vec4 uColor;

void main() {
    FragColor = uColor;
}
)";

// masks off the center based off of defined degrees. feathers it out
static const std::string FOVEAL_FRAG_SRC = R"(
    #version 460 core

    in vec2 TexCoord;
    out vec4 FragColor;

    uniform sampler2D uTexture;
    uniform vec2 uResolution;

    uniform float uCenterRadiusPx;
    const float featherPx = 80.0;

    void main()
    {
        vec2 fragPos = TexCoord * uResolution;
        vec2 center = uResolution * 0.5;

        float dist = distance(fragPos, center);
        float alpha = smoothstep(
            uCenterRadiusPx,
            uCenterRadiusPx + featherPx,
            dist
        );

        vec4 color = texture(uTexture, TexCoord);

        FragColor = vec4(color.rgb, alpha);
    }
)";
// Local flicker shader (its easiest to mask off the rest of the image)
static const std::string LOCAL_FRAG_SRC = R"(
    #version 460 core

    in vec2 TexCoord;
    out vec4 FragColor;

    uniform sampler2D uTexture;
    uniform vec2 uResolution;   // screen size in pixels
    uniform vec2 uRectPos;      // top left corner in pixels
    uniform vec2 uRectSize;     // width + height in pixels

    void main() {
        // convert TexCoord (0..1) to pixel position
        vec2 pixelPos = TexCoord * uResolution;

        // discard anything outside the rectangle
        if (pixelPos.x < uRectPos.x || pixelPos.x > uRectPos.x + uRectSize.x ||
            pixelPos.y < uRectPos.y || pixelPos.y > uRectPos.y + uRectSize.y) {
            discard;
        }

        FragColor = texture(uTexture, TexCoord);
    }
)";


float t = 0.001f; // thickness (represents half the thickness)
float s = 0.03f; // size (distance from center to edge of crosshair)

// crosshair to be used as fixation point. horizontal line on left screen, right has veritcal lines. correctly converging eyes will align the cross hairs
float crosshair[] = {
    // horizontal rectangle
    -s, -t,
     s, -t,
     s,  t,

    -s, -t,
     s,  t,
    -s,  t,

    // vertical rectangle
    -t, -s,
     t, -s,
     t,  s,

    -t, -s,
     t,  s,
    -t,  s,
};


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

    GLuint textures[] = { m_texOrig_L, m_texDec_L, m_texStart_L, m_texWaitResponse_L, m_texOrig_R, m_texDec_R, m_texStart_R, m_texWaitResponse_R }; // textures are batch deleted
    glDeleteTextures(8, textures);

    if (m_quadVAO) glDeleteVertexArrays(1, &m_quadVAO);
    if (m_quadVBO) glDeleteBuffers(1, &m_quadVBO);
    if (m_crosshairVAO) glDeleteVertexArrays(1, &m_crosshairVAO);
    if (m_crosshairVBO) glDeleteBuffers(1, &m_crosshairVBO);

    glfwDestroyWindow(m_window);
    glfwTerminate();
}

bool App::initCrosshair() {
    glGenVertexArrays(1, &m_crosshairVAO);
    glGenBuffers(1, &m_crosshairVBO);

    glBindVertexArray(m_crosshairVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_crosshairVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(crosshair), crosshair, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);

    return m_crosshairShader.load(CROSSHAIR_VERT_SRC, CROSSHAIR_FRAG_SRC);
}

bool App::loadShaders() {
    if (!m_shader.load(VERT_SRC, FRAG_SRC)) return false; // all variants use this shader (full image)
    if (m_variant == 1) {
        if (!m_fovealShader.load(VERT_SRC, FOVEAL_FRAG_SRC)) return false;
    }
    else if (m_variant == 2) {
        if (!m_localShader.load(VERT_SRC, LOCAL_FRAG_SRC)) return false;
    }

    return true;
}

void App::initWindow() {
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

    glfwMakeContextCurrent(m_window);
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);
    glfwSetKeyCallback(m_window, keyCallback);

}

void App::initGame() {
    m_trialIndex = 0;
    m_phase = (m_config.trials[0].flickerIndex == 0) ? TrialPhase::ShowFlicker : TrialPhase::ShowOriginal;
    m_phaseStart = glfwGetTime();
}

bool App::init(const std::string& configPath) {
    // config init
    if (!m_config.load(configPath))   return false;
    if (m_config.trials.empty()) {
        Utils::FatalError("[App] No trials in config.");
        return false;
    }

    timeoutDuration = m_config.imageTime;
    flickerRate = m_config.flickerRate;
    waitTimeoutDuration = m_config.waitTime;

    // randomize order of trials + flickers
    Utils::ShuffleTrials(m_config.trials);
    Utils::ShuffleFlickers(m_config.trials);

    // init glfw
    if (!glfwInit()) {
        Utils::FatalError("[App] Failed to initialize GLFW");
        return false;
    }

    initWindow();

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        Utils::FatalError("[App] Failed to initialize GLAD");
        return false;
    }

    glViewport(0, 0, m_width, m_height);
    glfwSwapInterval(1);

   // std::string FOVEAL_FRAG_SRC = Utils::ReadFile("shaders/foveal_fragment.glsl"); // fix util to orngaize shaders better :3
    //build shaders
    if (!loadShaders()) return false;

    //quad geometry building
    if (!initQuad()) return false;
    if (!initCrosshair()) return false;

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
    loadTextures(m_config.trials[0]); // loads the first 2 images in the trial

    std::string variantName;
    updateImageShader(); // used for non flicker condition, will load no matter the variant
    updateFixationShader();
    // variants
    switch (m_variant) {
        case 0:
            variantName = "Full Image";
            break;
        case 1:
            variantName = "Peripheral Crop";
            updateFovealFlickerShader();
            break;
        case 2:
            variantName = "Local Flicker";
            updateLocalFlickerShader();
            break;
    }

    // load the csv
    m_csv.init(m_config.participantID, m_config.participantAge, m_config.participantGender, "Flicker Paradigm", variantName, { "Index", "Image", "Viewing Mode", "Answer", "Actual", "Reaction Time (s)" }, m_config.outputDirectory.string());

    //init phase
    m_phase = TrialPhase::StartInstructions;
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
    using clock = std::chrono::high_resolution_clock;

    const double targetFrameTime = 1.0 / m_config.targetFPS;
    auto nextFrameTime = clock::now();

    while (!glfwWindowShouldClose(m_window) && m_phase != TrialPhase::Done) {
        update();
        render();
        glfwSwapBuffers(m_window);
        glfwPollEvents();

        nextFrameTime += std::chrono::duration_cast<clock::duration>(
            std::chrono::duration<double>(targetFrameTime)
        );
        std::this_thread::sleep_until(nextFrameTime);
    }
}

// update loop

void App::update() {
    const double now = glfwGetTime();
    const double elapsed = now - m_phaseStart;
    const ImagePaths& img = m_config.trials[m_trialIndex];

    // show original, no flicker
    if ((m_phase == TrialPhase::ShowOriginal)) {
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
            m_flickerShow = !m_flickerShow;
        }

    }
}


// rendering


void App::render() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (m_phase == TrialPhase::ShowOriginal)
    {
        renderTexture(m_texOrig_L, m_texOrig_R); // renders just the orignal texture
    }
    else if (m_phase == TrialPhase::ShowFlicker)
    {
        if (m_variant == 0) {
            renderTexture(m_texOrig_L, m_texOrig_R); // renders original texture
            if(m_flickerShow) renderTexture(m_texDec_L, m_texDec_R); // with dec over top
        }
        else if (m_variant == 1) { // foveal
           
            renderTexture(m_texOrig_L, m_texOrig_R); // renders original texture
            if (m_flickerShow) renderFovealTexture(m_texDec_L, m_texDec_R); // with dec over top (foveal mask)
        }
        else if (m_variant == 2) {
            renderTexture(m_texOrig_L, m_texOrig_R);
            if (m_flickerShow) renderLocalTexture(m_texDec_L, m_texDec_R); // with dec over top (local flicker mask)
        }
        
    }
    else if (m_phase == TrialPhase::StartInstructions)
    {
        renderTexture(m_texStart_L, m_texStart_R);
    }

    else if (m_phase == TrialPhase::WaitForResponse)
    {
        renderTexture(m_texWaitResponse_L, m_texWaitResponse_R);
    }

    // between trials: grey screen
    else if (m_phase == TrialPhase::ShowWaitScreen) {
        glClearColor(0.39f, 0.39f, 0.39f, 1.0f); // grey
        glClear(GL_COLOR_BUFFER_BIT);
    }

    // render the fixation point (cross hair)
    renderCrosshair();
}

// renderTexture — bind shader + quad + texture, draw 6 verts
void App::renderFovealTexture(GLuint texL, GLuint texR) {
    m_fovealShader.use();
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_quadVAO);

    // left monitor
    glViewport(0, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, texL);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // right monitor (assuming same width as left)
    glViewport(m_width, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, texR);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void App::renderLocalTexture(GLuint texL, GLuint texR) {
    m_localShader.use();

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_quadVAO);

    // left monitor
    glViewport(0, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, texL);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // right monitor (assuming same width as left)
    glViewport(m_width, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, texR);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void App::renderTexture(GLuint texL, GLuint texR) {
    m_shader.use();

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_quadVAO);

    // left monitor
    glViewport(0, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, texL);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // right monitor (assuming same width as left)
    glViewport(m_width, 0, m_width, m_height);
    glBindTexture(GL_TEXTURE_2D, texR);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}
void App::renderCrosshair() {
    m_crosshairShader.use();

    glBindVertexArray(m_crosshairVAO);
    // render horizontal cross hair on left
    glViewport(0, 0, m_width, m_height);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    // render vertical cross hair on right
    glViewport(m_width, 0, m_width, m_height);
    glDrawArrays(GL_TRIANGLES, 6, 12);

    glBindVertexArray(0);
}

// phases

void App::advancePhase() {
    if (m_phase == TrialPhase::ShowOriginal) {
        if (m_config.trials[m_trialIndex].flickerIndex == 0) { // this is the second image shown, wait for response is next
            m_phase = TrialPhase::WaitForResponse;
            
            if ((m_trialIndex + 1) < m_config.trials.size()) { 
                if (m_variant == 2) { // update local flicker upon start of new trial
                    updateLocalFlickerShader();
                }   
                //load the next 2 images (L, R) in the trial
                loadTextures(m_config.trials[m_trialIndex + 1]);
            }
        }
        else { // this is the first image, show wait screen next
            m_phase = TrialPhase::ShowWaitScreen;
        }
    }
    else if (m_phase == TrialPhase::ShowWaitScreen) {
       
        if (m_config.trials[m_trialIndex].flickerIndex == 0) { // flicker has already happened, next phase is show original
            m_phase = TrialPhase::ShowOriginal;
        }
        else { // flicker is next to happen
            m_phase = TrialPhase::ShowFlicker;  
        }
        
    }
    else if (m_phase == TrialPhase::ShowFlicker) {
        if (m_config.trials[m_trialIndex].flickerIndex == 0) { // this is the first image, show wait screen next
            m_phase = TrialPhase::ShowWaitScreen;
        }
        else { // this is the second image shown, wait for response is next
            m_phase = TrialPhase::WaitForResponse;
            if ((m_trialIndex + 1) < m_config.trials.size()) {

                if (m_variant == 2) { // update local flicker upon start of new trial
                    updateLocalFlickerShader();
                }
                //load the next 2 images (L, R) in the trial
                loadTextures(m_config.trials[m_trialIndex + 1]);
            }

        } 
    }
    m_phaseStart = glfwGetTime();
    m_responseStart = m_phaseStart;
}

void App::recordResponse(int key) {
    if (m_phase != TrialPhase::WaitForResponse) return;

    TrialResult result;
    result.imageName = m_config.trials[m_trialIndex].name;
    result.answer = key == GLFW_KEY_LEFT ? 0 : 1;
    result.actual = m_config.trials[m_trialIndex].flickerIndex; 
    result.index = m_trialIndex;

    result.answer == result.actual ? PlaySound(TEXT("sounds/Success.wav"), NULL, SND_FILENAME | SND_ASYNC) : PlaySound(TEXT("sounds/error.wav"), NULL, SND_FILENAME | SND_ASYNC);

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

// helper to load texture into id TODO: HDR support. auto switch monitor into hdr mode?
//void App::loadTexture(const std::string& path, GLuint textureID) {
//    //std::thread t([&]() {
//    //    
//    //    
//    //});
//    cv::Mat img = cv::imread(path, cv::IMREAD_ANYDEPTH | cv::IMREAD_COLOR);
//
//    if (img.empty()) {
//        Utils::FatalError("[App] Failed to load image: " + path);
//        return;
//    }
//
//   // img.convertTo(img, CV_32FC3, 1.0 / 255.0);
//    //cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
//    cv::flip(img, img, 0);
//
//
//    glBindTexture(GL_TEXTURE_2D, textureID);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
//    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img.cols, img.rows, 0, GL_RGB, GL_FLOAT, img.data);
//    glBindTexture(GL_TEXTURE_2D, 0);
//
//    img.release();
//}

void App::loadTexture(const std::string& path, GLuint textureID)
{
    cv::Mat src = cv::imread(path, cv::IMREAD_UNCHANGED);
    
    if (src.empty())
        throw std::runtime_error("Failed to load image");

    cv::Mat img;
    bool isHDR = false;

    if (src.depth() == CV_8U)
    {
        src.convertTo(img, CV_32F, 1.0 / 255.0);
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        cv::flip(img, img, 0);
    }
    else if (src.depth() == CV_16U)
    {
        isHDR = true;
        src.convertTo(img, CV_32F, 1.0 / 65535.0);
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        cv::flip(img, img, 0);
    }
    else
    {
        isHDR = true;
        src.convertTo(img, CV_32F);
        cv::cvtColor(img, img, cv::COLOR_BGR2RGB);
        cv::flip(img, img, 0);
    }

    

    if (!img.isContinuous()) img = img.clone();

    glBindTexture(GL_TEXTURE_2D, textureID);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (!isHDR)
    {
        std::wstring msg = L"This image is not HDR:  " + std::wstring(path.begin(), path.end()) + L"\n";
        OutputDebugStringW(msg.c_str());
        cv::Mat upload;
        img.convertTo(upload, CV_8UC3, 255.0f);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, upload.cols, upload.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, upload.data);
    }
    else
    {
        std::wstring msg = L"This image IS HDR:  " + std::wstring(path.begin(), path.end()) + L"\n";
        OutputDebugStringW(msg.c_str());
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, img.cols, img.rows, 0, GL_RGB, GL_FLOAT, img.data);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
}

/// <summary>
/// Update variables within the local flicker shader texture
/// </summary>
void App::updateLocalFlickerShader() {
    auto [x, y, w, h] = Utils::randomizeQuad(m_width, m_height); // randomize size and shape parameters
    m_localShader.use();
    m_localShader.setVec2("uResolution", m_width, m_height);
    m_localShader.setVec2("uRectPos", x, y);
    m_localShader.setVec2("uRectSize", w, h);
    m_localShader.use();
    m_localShader.setBool("uMirror", true);
}

/// <summary>
/// Update variables within the regular image shader texture
/// </summary>
void App::updateImageShader() {
    m_shader.use();
    m_shader.setInt("uTexture", 0);
    m_shader.setBool("uMirror", true); // mirror 
}

/// <summary>
/// Update variables within the foveal flicker shader texture
/// </summary>
void App::updateFovealFlickerShader() {
    float fovealRadiusPx;
    m_fovealShader.use();
    m_fovealShader.setInt("uTexture", 0);
    m_fovealShader.setBool("uMirror", true); // mirror 
    m_fovealShader.setVec2("uResolution", (float)m_width, (float)m_height);
    fovealRadiusPx = Utils::fovealRadiusFromPixelsPerDegree(m_config.fovealWidth, m_config.pixelsPerDegree);
    m_fovealShader.setFloat("uCenterRadiusPx", (float)fovealRadiusPx);
}

/// <summary>
/// Update variables within the foveal flicker shader texture
/// </summary>
void App::updateFixationShader() {
    m_crosshairShader.use();
    m_crosshairShader.setVec4("uColor", 1.0f, 1.0f, 1.0f, 1.0f);
    m_crosshairShader.setFloat("uAspect", (float)m_width / (float)m_height);
}

// GLFW callbacks
void App::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

// key presses
// add gamepad support
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
            app->initGame();
            GLuint textures[] = {app->m_texStart_L,  app->m_texStart_R }; // delete start textures. not needed anymore.
            glDeleteTextures(2, textures);
        }
    }

}