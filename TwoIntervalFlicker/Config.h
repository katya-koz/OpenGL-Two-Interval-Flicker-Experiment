#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

// holds the 4 permuations per image
struct ImagePaths {
    std::string name; 
    fs::path L_orig; // <name>_L_orig.<ext>
    fs::path L_dec; // <name>_L_dec.<ext>
    fs::path R_orig; // <name>_R_orig.<ext>
    fs::path R_dec; // <name>_R_dec.<ext>
    int flickerIndex = 0; // this tracks whether the first or the second image will be flickered.
};

struct Config {
    std::string participantID;
    fs::path imageDirectory;
    std::vector<ImagePaths> trials;

    // load and parse json config file
    bool load(const std::string& configPath);

private:
    // searches the provided imageDirectory for a file named "<name><suffix>.*" (any extension).
    //returns the matched path (or empty if not found)
    fs::path findImage(const std::string& name, const std::string& suffix) const;
};
