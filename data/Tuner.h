#pragma once

#include <vector>
#include <cmath>
#include <iostream>
#include <memory>
#include <fstream>
#include <string>

#include "../bitboards/Position.h"

namespace MyChess {

    struct TunerEntry {
        std::unique_ptr<Position> pos;
        double result;

        TunerEntry(const std::string& fen, double res) 
            : pos(std::make_unique<Position>(fen)), result(res) {}
    };

    class Tuner {
    private:
        std::vector<TunerEntry> entries;
        double K = 0.05;
        double learning_rate = 0.00001;

        inline double sigmoid(double eval, double k) const noexcept {
            return 1.0 / (1.0 + std::pow(10, -k * eval / 400));
        }

        double calculate_error(double k) {
            if (entries.empty()) return 0.0;
            double total_error = 0.0;

            for (const auto& entry : entries) {
                short eval = entry.pos->evaluate();
                double P = sigmoid(eval, k);
                total_error += std::pow(entry.result - P, 2.0);
            }
            
            return total_error / entries.size();
        }
    
    public:
        void add_entry(const std::string& fen, double result) {
            if (fen.empty()) return;
            entries.emplace_back(fen, result);
        }

        bool open_dataset(const std::string& filepath) {
            // Convert backslashes to forward slashes for cross-platform compatibility
            std::string normalized_path = filepath;
            for (auto& c : normalized_path) {
                if (c == '\\') c = '/';
            }
            
            std::ifstream file(normalized_path);
            if (!file.is_open()) {
                // Try with original path in case it's already correct
                file.open(filepath);
                if (!file.is_open()) {
                    std::cout << "[Tuner] Failed to open dataset: " << filepath << std::endl;
                    std::cout << "[Tuner] Tried: " << normalized_path << std::endl;
                    return false;
                }
            }
            entries.clear();
            std::string line;
            size_t count = 0;
            while (std::getline(file, line)) {
                if (line.empty()) continue;

                line.erase(std::remove(line.begin(), line.end(), '\"'), line.end());
                line.erase(std::remove(line.begin(), line.end(), ';'), line.end());

                size_t last_space = line.find_last_of(" \t");
                if (last_space != std::string::npos) {
                    std::string fen = line.substr(0, last_space);
                    std::string res_str = line.substr(last_space + 1);
                    double result = 0.5;
                    if (res_str == "1.0" || res_str == "1-0" || res_str == "1") result = 1.0;
                    else if (res_str == "0.0" || res_str == "0-1" || res_str == "0") result = 0.0;
                    else if (res_str == "0.5" || res_str == "1/2-1/2") result = 0.5;
                    else {
                        try { result = std::stod(res_str); } catch (...) { continue; }
                    }
                    entries.emplace_back(fen, result);
                    count++;
                }
            }
            std::cout << "[Tuner] Loaded " << count << " entries from " << filepath << std::endl;
            return true;
        }

        double find_best_K(double start = 0.5, double end = 2.0, double step = 0.01) {
            if (entries.empty()) return K;
            double best_error = 1e18;
            double best_k = K;
            for (double test_k = start; test_k < end; test_k += step) {
                double error = calculate_error(test_k);
                if (error < best_error) {
                    best_error = error;
                    best_k = test_k;
                }
            }
            K = best_k;
            std::cout << "[Tuner] Best K = " << K << " | error = " << best_error << std::endl;
            return K;
        }

        void train(int iterations = 100, double lr = 1.0) {
            if (entries.empty()) {
                std::cout << "[Tuner] Dataset is empty" << std::endl;
                return;
            }
            find_best_K();
            std::cout << "================================" << std::endl;
            std::cout << "[Tuner] Training started" << std::endl;
            std::cout << "Positions: " << entries.size() << std::endl;
            std::cout << "Epochs: " << iterations << std::endl;
            std::cout << "Learning rate: " << lr << std::endl;
            std::cout << "================================" << std::endl;
            for (int iter = 0; iter < iterations; iter++) {
                double total_error = 0.0;

                for (auto& entry : entries) {
                    short eval = entry.pos->evaluate();
                    double P = sigmoid(eval, K);
                    total_error += std::pow(entry.result - P, 2.0);

                    double error_diff = (P - entry.result);
                    double gradient = error_diff * (P * (1.0 - P)) * (K * std::log(10.0) / 400);
                    double adjustment = lr * gradient * 100000000; // Scale adjustment for more noticeable changes
                    entry.pos->update_weights(adjustment);
                }

                if ((iter + 1) % 10 == 0 || iter == 0) {
                    std::cout << "Epoch [" << iter + 1 << "/" << iterations << "] | MSE = " << (total_error / entries.size()) << std::endl;
                }
            }
            std::cout << "================================" << std::endl;
            std::cout << "[Tuner] Training finished" << std::endl;
            std::cout << "================================" << std::endl;
        }
    };
}