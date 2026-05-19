#pragma once

#include <fstream>
#include <iostream>
#include <string>
#include "EvalWeights.h"

namespace MyChess {

    class WeightsIO {
    public:
        static bool save_weights(const std::string& filename) {
            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "[WeightsIO] Failed to save weights to " << filename << std::endl;
                return false;
            }

            // Сохраняем веса фигур
            file.write(reinterpret_cast<const char*>(weight), sizeof(weight));

            // Сохраняем PST для миттельшпиля
            file.write(reinterpret_cast<const char*>(PST_Midgame), sizeof(PST_Midgame));

            // Сохраняем PST для эндшпиля
            file.write(reinterpret_cast<const char*>(PST_Endgame), sizeof(PST_Endgame));

            file.close();
            std::cout << "[WeightsIO] Weights saved to " << filename << std::endl;
            return true;
        }

        static bool load_weights(const std::string& filename) {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                std::cout << "[WeightsIO] No saved weights found, using defaults from EvalWeights.h" << std::endl;
                return false;
            }

            // Загружаем веса фигур
            file.read(reinterpret_cast<char*>(weight), sizeof(weight));

            // Загружаем PST для миттельшпиля
            file.read(reinterpret_cast<char*>(PST_Midgame), sizeof(PST_Midgame));

            // Загружаем PST для эндшпиля
            file.read(reinterpret_cast<char*>(PST_Endgame), sizeof(PST_Endgame));

            file.close();
            std::cout << "[WeightsIO] Weights loaded from " << filename << std::endl;
            return true;
        }

        static void reset_to_defaults() {
            // Значения по умолчанию из EvalWeights.h
            short default_weight[12] = {100, 320, 330, 500, 900, 0, -100, -320, -330, -500, -900, 0};
            for (int i = 0; i < 12; i++) {
                weight[i] = default_weight[i];
            }

            std::cout << "[WeightsIO] Weights reset to defaults" << std::endl;
        }
    };
}
