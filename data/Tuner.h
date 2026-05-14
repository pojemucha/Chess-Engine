#pragma once

#include <vector>
#include <cmath>
#include <iostream>

#include "../bitboards/Position.h"

namespace MyChess {

    struct TunerEntry {
        Position pos;
        double result;

        TunerEntry(const std::string& fen, double res) : pos(fen), result(res) {}
    };

    class Tuner {
    private:
        std::vector<TunerEntry> entries;
        double K = 0.05;
        double learning_rate = 0.01;

        inline double sigmoid(double eval, double k) const noexcept {
            return 1.0 / (1.0 + std::pow(10, -k * eval / 400));
        }

        double calculate_error(double k) {
            if (entries.empty()) {
                std::cout << "[Tuner] Dataset is empty" << std::endl;
                return 0.0;
            }
            double total_error = 0.0;

            for (const auto& entry : entries) {
                short eval = entry.pos.evaluate();
                double P = sigmoid(eval, k);
                total_error += std::pow(entry.result - P, 2.0);
            }
            
            return total_error / entries.size();
        }
    
    public:
        void add_entry(const std::string& fen, double result) {
            if (fen.empty()) {
                std::cout << "[Tuner] FEN is empty" << std::endl;
                return;
            }
            entries.emplace_back(fen, result);
        }

        double find_best_K(double start = 0.5, double end = 2.0, double step = 0.01) {
            if (entries.empty()) {
                std::cout << "[Tuner] Dataset is empty" << std::endl;
                return K;
            }
            double best_error = 1e18;
            double best_k = K;
            for (double test_k = start; test_k < end; test_k += step) {
                double error = calculate_error(test_k);
                if (error < best_error) {
                    best_k = test_k;
                }
            }
            K = best_k;
            std::cout << "[Tuner] Best K = " << K << " | error = " << best_error << std::endl;
            return K;
        }

        void train(int iterations = 1000, double learning_rate = 1.0) {
            if (entries.empty()) {
                std::cout << "[Tuner] Dataset is empty" << std::endl;
                return;
            }
            std::cout << "================================" << std::endl;
            std::cout << "[Tuner] Training started" << std::endl;
            std::cout << "Positions: " << entries.size() << std::endl;
            std::cout << "Epochs: " << iterations << std::endl;
            std::cout << "Learning rate: " << learning_rate << std::endl;
            std::cout << "================================" << std::endl;
            for (int iter = 0; iter < iterations; iter++) {
                double total_error = 0.0;

                for (auto& entry : entries) {
                    short eval = entry.pos.evaluate();
                    double P = sigmoid(eval, K);
                    total_error += std::pow(entry.result - P, 2.0);

                    double error_diff = (P - entry.result);
                    double gradient = error_diff * (P * (1.0 - P)) * (K * std::log(10.0) / 400);

                    double adjustment = learning_rate * gradient;

                    entry.pos.update_weights(adjustment);
                }

                std::cout << "Epoch [" << iter + 1 << "/" << iterations << "] | MSE = " << (total_error / entries.size()) << std::endl;
            }
            std::cout << "================================" << std::endl;
            std::cout << "[Tuner] Training finished" << std::endl;
            std::cout << "================================" << std::endl;
        }

        void benchmark(size_t limit = 1000) const {
            if (entries.empty()) {
                std::cout << "[Tuner] Dataset is empty" << std::endl;
                return;
            }

            size_t tested = 0;
            double total_error = 0.0;
            auto start = std::chrono::high_resolution_clock::now();
            
            for (const auto& entry : entries) {
                int eval = entry.pos.evaluate();
                double prediction = sigmoid(eval, K);
                total_error = std::pow(entry.result - prediction, 2.0);
                tested++;
                if (tested >= limit) {
                    break;
                }
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            
            std::cout << "================================" << std::endl;
            std::cout << "[Benchmark]" << std::endl;
            std::cout << "Positions: " << tested << std::endl;
            std::cout << "Avg error: " << total_error / tested << std::endl;
            std::cout << "Time: " << ms << " ms" << std::endl;
            std::cout << "================================" << std::endl;
        }
    };
}
