#pragma once
#include <Windows.h>
#include <string>
#include <algorithm>
#include <random>

namespace Utils
{

	static void FatalError(const std::string& message)
	{
		const auto result = MessageBoxA(
			nullptr,
			message.c_str(),
			"Fatal Error",
			MB_OK | MB_ICONERROR | MB_TOPMOST
		);

		if (result == IDOK)
		{
			exit(1);
		}
	}

	static void ShuffleTrials(std::vector<ImagePaths>& trials)
	{
		// shuffle the order of the flickers
		std::random_device rd;
		std::mt19937 gen(rd());

		std::shuffle(trials.begin(), trials.end(), gen);
	}

	static void ShuffleFlickers(std::vector<ImagePaths>& trials)
	{
		// iterate through the trials, randomize the flicker to be shown either first or second
		std::random_device rd;
		static std::mt19937 gen(rd());
		static std::uniform_int_distribution<int> dist(0, 1);

		for (auto& n : trials) {
			n.flickerIndex = dist(gen);
		}
		return;
	}

}