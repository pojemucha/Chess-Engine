/**
 * @file Tuner.h
 * @brief Evaluation parameter tuning using Stochastic Gradient Descent (Texel's Tuning Method).
 * 
 * This module adjusts the static evaluation weights (PSTs and piece values) by minimizing 
 * the Mean Squared Error (MSE) between the engine's evaluation and the actual game outcomes.
 */

#pragma once

#include <vector>
#include <cmath>
#include <iostream>
#include <fstream>
#include <cstring>
#include <algorithm>

#include "../bitboards/Position.h"

namespace MyChess {

    /**
     * @struct TunerEntry
     * @brief A highly compressed, cache-friendly representation of a position.
     * 
     * We only store the piece placement and pre-calculated structural bonuses.
     */
    struct TunerEntry {
        Piece board[64];      ///< Sparse array of pieces on the board.
        short phase;          ///< Pre-calculated game phase (0 to 24).
        short static_bonus;   ///< Structural evaluation (mobility, center control) that is NOT being tuned.
        Color side_to_move;   ///< The color to move.
        double result;        ///< Expected outcome: 1.0 (Win), 0.5 (Draw), 0.0 (Loss).
    };

    /**
     * @class Tuner
     * @brief Manages the dataset and executes SGD optimization for evaluation weights.
     */
    class Tuner {
    private:
        std::vector<TunerEntry> entries;
        double K = 0.05;

        /// @name Mathematical Core
        /// @{

        /**
         * @brief Maps the engine's centipawn evaluation to a win probability (0.0 to 1.0).
         */
        inline double sigmoid(double eval, double k) const noexcept {
            return 1.0 / (1.0 + std::pow(10.0, -k * eval / 400.0));
        }

        /**
         * @brief Rapidly evaluates the position using the sparse board representation.
         * Combines currently tuned PSTs/Weights with the pre-calculated structural bonus.
         */
        inline double evaluate_entry(const TunerEntry& entry) const noexcept {
            double mg_score = 0;
            double eg_score = 0;

            for (Square cell = 0; cell < 64; ++cell) {
                Piece piece = entry.board[cell];
                if (piece != EMPTY) {
                    mg_score += weight[piece] + PST_Midgame[piece][cell];
                    eg_score += weight[piece] + PST_Endgame[piece][cell];
                }
            }

            double base_score = (mg_score * entry.phase + eg_score * (24 - entry.phase)) / 24;
            double final_eval = base_score + entry.static_bonus;
            
            return final_eval;
        }

        /**
         * @brief Calculates the Mean Squared Error (MSE) across the entire dataset.
         */
        double calculate_error(double k) const {
            if (entries.empty()) return 0.0;
            double total_error = 0.0;

            for (const auto& entry : entries) {
                double eval = evaluate_entry(entry);
                double P = sigmoid(eval, k);
                total_error += std::pow(entry.result - P, 2.0);
            }
            
            return total_error / entries.size();
        }

        /// @}

    public:
        /// @name Dataset Management
        /// @{

        /**
         * @brief Parses a FEN string, extracts features, and adds it to the dataset.
         * Creates a temporary Position to extract structural evaluation exactly once.
         */
        void add_entry(const std::string& fen, double result) {
            if (fen.empty()) return;

            Position pos(fen);
            TunerEntry entry;
            entry.result = result;
            entry.side_to_move = pos.get_side_to_move();
            
            double mg_score = 0;
            double eg_score = 0;
            short  phase    = 0;

            for (Square cell = 0; cell < 64; cell++) {
                Piece piece = pos.get_piece(cell);
                entry.board[cell] = piece;
                
                if (piece != EMPTY) {
                    mg_score += weight[piece] + PST_Midgame[piece][cell];
                    eg_score += weight[piece] + PST_Endgame[piece][cell];
                    
                    switch (piece) {
                        case WHITE_QUEEN:  case BLACK_QUEEN:  phase += 4; break;
                        case WHITE_ROOK:   case BLACK_ROOK:   phase += 2; break;
                        case WHITE_BISHOP: case BLACK_BISHOP: phase += 1; break;
                        case WHITE_KNIGHT: case BLACK_KNIGHT: phase += 1; break;
                        default: break;
                    }
                }
            }

            if (phase > 24) phase = 24;
            entry.phase = phase;

            double base_score = (mg_score * phase + eg_score * (24 - phase)) / 24;
            double total_eval = pos.evaluate(); 
            double white_eval = (entry.side_to_move == WHITE) ? total_eval : -total_eval;
            
            entry.static_bonus = white_eval - base_score;

            entries.push_back(entry);
        }

        /**
         * @brief Loads a dataset from an EPD/FEN file.
         */
        bool open_dataset(const std::string& filepath) {
            std::string normalized_path = filepath;
            std::replace(normalized_path.begin(), normalized_path.end(), '\\', '/');
            
            std::ifstream file(normalized_path);
            if (!file.is_open()) {
                file.open(filepath);
                if (!file.is_open()) {
                    std::cout << "[Tuner] Failed to open dataset: " << filepath << std::endl;
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
                    
                    add_entry(fen, result);
                    count++;
                }
            }
            std::cout << "[Tuner] Loaded " << count << " entries from " << filepath << std::endl;
            return true;
        }

        /// @}

        /// @name Optimization & Training
        /// @{

        /**
         * @brief Performs a grid search to find the optimal sigmoid scaling factor 'K'.
         */
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

        /**
         * @brief Executes Batch Gradient Descent (BGD) to optimize evaluation parameters.
         * Accumulates gradients over the entire dataset before applying updates.
         * This prevents parameter explosion and guarantees stable convergence.
         * 
         * @param iterations Number of epochs to run.
         * @param learning_rate Base multiplier for weight adjustments.
         */
        void train(int iterations = 100, double learning_rate = 1500.0) {
            if (entries.empty()) {
                std::cout << "[Tuner] Dataset is empty" << std::endl;
                return;
            }
            
            find_best_K();
            
            std::cout << "================================" << std::endl;
            std::cout << "[Tuner] Training started" << std::endl;
            std::cout << "Positions: " << entries.size() << std::endl;
            std::cout << "Epochs: " << iterations << std::endl;
            std::cout << "Learning rate: " << learning_rate << std::endl;
            std::cout << "================================" << std::endl;
            
            for (int iter = 0; iter < iterations; iter++) {
                double total_error = 0.0;

                // Allocate gradient accumulators on the stack to prevent dynamic allocations
                double pst_mg_grad[6][64];
                double pst_eg_grad[6][64];
                double weight_grad[6];
                std::memset(pst_mg_grad, 0, sizeof(pst_mg_grad));
                std::memset(pst_eg_grad, 0, sizeof(pst_eg_grad));
                std::memset(weight_grad, 0, sizeof(weight_grad));

                for (const auto& entry : entries) {
                    double eval = evaluate_entry(entry);
                    double P = sigmoid(eval, K);
                    total_error += std::pow(entry.result - P, 2.0);

                    // Compute gradient of the loss function
                    double error_diff = (P - entry.result);
                    double gradient = error_diff * (P * (1.0 - P)) * (K * std::log(10.0) / 400.0);
                    
                    // The adjustment maps the double-precision gradient to integer centipawns
                    double adjustment = gradient; 
                    
                    double mg_scale = static_cast<double>(entry.phase) / 24.0;
                    double eg_scale = 1.0 - mg_scale;

                    for (Square cell = 0; cell < 64; ++cell) {
                        Piece piece = entry.board[cell];
                        if (piece != EMPTY) {
                            if (piece < BLACK_PAWN) {
                                pst_mg_grad[piece][cell] += adjustment * mg_scale;
                                pst_eg_grad[piece][cell] += adjustment * eg_scale;
                                weight_grad[piece] += adjustment;
                            } else {
                                short white_piece = piece - 6;
                                Square mirrored_cell = cell ^ 56; // Mirror vertically for black pieces
                                pst_mg_grad[white_piece][mirrored_cell] -= adjustment * mg_scale;
                                pst_eg_grad[white_piece][mirrored_cell] -= adjustment * eg_scale;
                                weight_grad[white_piece] -= adjustment;
                            }
                        }
                    }
                }
                double scale = learning_rate / entries.size();

                for (short piece = 0; piece < 6; piece++) {
                    weight[piece] -= weight_grad[piece] * scale;
                    for (Square cell = 0; cell < CELL_NB; cell++) {
                        PST_Midgame[piece][cell] -= pst_mg_grad[piece][cell] * scale;
                        PST_Endgame[piece][cell] -= pst_eg_grad[piece][cell] * scale;
                    }
                }

                for (short piece = 0; piece < 6; piece++) {
                    weight[piece + 6] = -weight[piece];
                    for (Square cell = 0; cell < 64; cell++) {
                        Square mirrored_cell = cell ^ 56; // Mirror vertically for black pieces
                        PST_Midgame[piece + 6][mirrored_cell] = -PST_Midgame[piece][cell];
                        PST_Endgame[piece + 6][mirrored_cell] = -PST_Endgame[piece][cell];
                    }
                }

                if ((iter + 1) % 10 == 0 || iter == 0) {
                    std::cout << "Epoch [" << iter + 1 << "/" << iterations  << "] | MSE = " << (total_error / entries.size()) << std::endl;
                }
            }
            
            std::cout << "================================" << std::endl;
            std::cout << "[Tuner] Training finished" << std::endl;
            std::cout << "================================" << std::endl;
        }

        /// @}
    };
}