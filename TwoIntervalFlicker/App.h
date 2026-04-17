#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <opencv2/opencv.hpp>
#include <string>
#include "config.h"
#include "shader.h"
#include "csv.h"

// Each trial steps through these phases in order
enum class TrialPhase {
    StartInstructions, // starting instructions
    ShowOriginal,// show L_orig for timeoutDuration
    ShowFlicker, // show flicker for timeoutDuration
    WaitForResponse, // black screen — waiting for left/right arrow key
    ShowWaitScreen, // black screen between images
    Done  // all trials are complete
};

// result recorded for each trial
struct TrialResult {
    std::string imageName;
    int answer;    // first or second (0 , 1)
    int actual; // first or second ( 0, 1)
    double reactionTime;   // seconds from start of WaitForResponse phase
    int index; // order of appearance
    std::string viewingMode;
};

class App {
public:
    App(int variant);
    ~App();

    

    bool init(const std::string& configPath);
    void run();

    // can be edited in config
    //how long each image is shown before advancing ( in seconds)
    double timeoutDuration;

    //how long in between the images (black screen, in seconds)
    double waitTimeoutDuration;

    // image flicker rate in Hz — how many times per second the image swaps (e.g. 10.0 = 10 swaps/sec)
    double flickerRate;

private:
    int m_width;
    int m_height;
    int m_variant;
    
    std::string m_title;
    GLFWwindow* m_window = nullptr;
    Config m_config;

    int  m_trialIndex = 0;
    TrialPhase m_phase = TrialPhase::ShowOriginal;
    double m_phaseStart = 0.0;
    double m_responseStart = 0.0;
    double m_flickerLast = 0.0;   // glfwGetTime() of last swap
    //bool m_flickerOnOrig = true;  // true = showing orig, false = showing dec
    bool m_flickerShow; // true - dec is on; false - dec is off

    std::vector<TrialResult> m_results;

    //GLuint m_texture_L = 0; // current texture on screen (left)
    //GLuint m_texture_R = 0;

    // need to load original and decompressed in order to flicker between them (without re-loading the images in every update tick)
    GLuint m_texOrig_L = 0;
    GLuint m_texDec_L = 0;
    GLuint m_texOrig_R = 0;
    GLuint m_texDec_R = 0;
    // double up, later for stereo mode.

    // for instrucitons
    GLuint m_texStart_L = 0;
    GLuint m_texWaitResponse_L = 0;
    GLuint m_texStart_R = 0;
    GLuint m_texWaitResponse_R = 0;

    // crosshair (fixation point)
    GLuint m_crosshairVAO = 0;
    GLuint m_crosshairVBO = 0;
    Shader m_crosshairShader;


    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;
    Shader m_shader;
    Shader m_fovealShader;
    Shader m_localShader;

    //GLPBO m_pboOrig_L
    //m_pboOrig_R
    //m_pboDec_L
    //m_pboDec_R

    CSV m_csv;

    void update();
    void render();

    void renderFlickerLayer(GLuint origL, GLuint origR, GLuint decL, GLuint decR);

    bool initQuad();
    bool initCrosshair();
    void advancePhase();
    void loadInstructionsTextures(); // load the response screen and instructions
    void initGame();
    void updateFixationShader();
    void loadTextures(const ImagePaths paths);
    bool loadShaders();
    void initWindow();
    void loadTexture(const std::string& path, GLuint textureID);
    void renderTexture(GLuint texL, GLuint texR);
    void renderFovealTexture(GLuint texL, GLuint texR);
    void renderLocalTexture(GLuint texL, GLuint texR);
    void renderCrosshair();
    void recordResponse(int key);

    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);
    void updateLocalFlickerShader();
    void updateImageShader();
    void updateFovealFlickerShader();
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};