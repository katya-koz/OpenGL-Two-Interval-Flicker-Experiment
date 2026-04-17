#include "config.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <Windows.h>
#include "Utils.h"

using json = nlohmann::json;

bool Config::load(const std::string& configPath) {
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cerr << "[Config] Could not open config file: " << configPath << "";
        return false;
    }

    json j;
    try {
        file >> j;
    }
    catch (const json::parse_error& e) {
        std::cerr << "[Config] JSON parse error: " << e.what() << "";
        return false;
    }

    participantID = j.at("Participant ID").get<std::string>();
    participantAge = std::to_string(j.at("Participant Age").get<int>());
    participantGender = j.at("Participant Gender").get<std::string>();
    imageDirectory = j.at("Image Directory").get<std::string>();

    // optional parameters
    if (j.contains("Output Directory")) {
        outputDirectory = j["Output Directory"].get<std::string>();
    }
    if (j.contains("Flicker Rate (Hz)")) {
        flickerRate = j["Flicker Rate (Hz)"].get<double>();
    }
    if (j.contains("Wait Time (s)")) {
        waitTime = j["Wait Time (s)"].get<double>();
    }
    if (j.contains("Image Time (s)")) {
        imageTime = j["Image Time (s)"].get<double>();
    }
    
    if (j.contains("Foveal Width (degrees)")) {
        fovealWidth = j["Foveal Width (degrees)"].get<float>();
    }
    if (j.contains("Pixels/Degree")) {
        pixelsPerDegree = j["Pixels/Degree"].get<float>();
    }

    if (j.contains("TargetFPS")) {
        targetFPS = j["TargetFPS"].get<float>();
    }
    //if (j.contains("Physical Screen Width (meters)")) {
    //    physicalScreenWidthMeters = j["Physical Screen Width (meters)"].get<float>();
    //}
    //if (j.contains("Physical Viewing Distance (meters)")) {
    //    viewingDistanceMeters = j["Physical Viewing Distance (meters)"].get<float>();
    //}

    if (!fs::exists(imageDirectory) || !fs::is_directory(imageDirectory)) {
        std::string msg = "[Config] Image directory not found: " + imageDirectory.string() + "";
        Utils::FatalError(msg);
        return false;
    }

    trials.clear();

    for (const auto& trial : j.at("Trials")) {
        std::string name = trial.at("Image Name").get<std::string>();
        int viewingMode = trial.at("Viewing Mode").get<int>();

        ImagePaths img;
        img.viewingMode = viewingMode;

        img.name = name;
        img.L_orig = findImage(name, "_L_orig");
        img.L_dec = findImage(name, "_L_dec");
        img.R_orig = findImage(name, "_R_orig");
        img.R_dec = findImage(name, "_R_dec");
        std::string msg;
        // Warn about any missing permutations
        auto warn = [&](const fs::path& p, const std::string& suffix) {
            if (p.empty()){
                msg = "[Config] Warning: no file found for \"" + name + suffix + ".*\"";
                Utils::FatalError(msg);
            }
        };

        warn(img.L_orig, "_L_orig");
        warn(img.L_dec, "_L_dec");
        warn(img.R_orig, "_R_orig");
        warn(img.R_dec, "_R_dec");

        trials.push_back(img);
    }

    return true;
}

fs::path Config::findImage(const std::string& name, const std::string& suffix) const {
    std::string stem = name + suffix; //e.g. "a_L_orig"

    for (const auto& entry : fs::directory_iterator(imageDirectory)) {
        if (!entry.is_regular_file()) continue;

        // Match on stem only — ignores extension, so .dds/.ppm/anything works
        if (entry.path().stem().string() == stem)
            return entry.path();
    }

    return {}; // not found
}