/**
 * @file WeightsIO.h
 * @brief Binary serialization and deserialization for engine evaluation parameters.
 * 
 * Provides utilities to save tuned weights (Piece Values and Piece-Square Tables)
 * to disk and load them safely with file validation.
 */

#pragma once

#include <fstream>
#include <iostream>
#include <cstring>
#include <cstdint>

#include "EvalWeights.h"

namespace MyChess {

    /**
     * @class WeightsIO
     * @brief Static utility class handling robust file I/O for neural/tuned weights.
     */
    class WeightsIO {
    private:
        /// @brief Magic signature "MCWT" (MyChess WeighTs) to validate file format.
        static constexpr std::uint32_t FILE_MAGIC = 0x4D435754; 
        
        /// @brief Format version to prevent loading outdated/incompatible weight structures.
        static constexpr std::uint32_t FILE_VERSION = 2;

        /**
         * @struct FileHeader
         * @brief Metadata prepended to every weights file to ensure binary integrity.
         */
        struct FileHeader {
            std::uint32_t magic;
            std::uint32_t version;
        };

    public:
        /**
         * @brief Serializes the current evaluation weights into a binary file.
         * @param filename Path to the output file (e.g., "weights.bin").
         * @return True if all data was successfully written.
         */
        static bool save_weights(const std::string& filename) {
            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "[WeightsIO] Error: Failed to open " << filename << " for writing." << std::endl;
                return false;
            }

            // 1. Write the validation header
            FileHeader header = { FILE_MAGIC, FILE_VERSION };
            file.write(reinterpret_cast<const char*>(&header), sizeof(FileHeader));

            // 2. Write the parameter arrays
            file.write(reinterpret_cast<const char*>(weight), sizeof(weight));
            file.write(reinterpret_cast<const char*>(PST_Midgame), sizeof(PST_Midgame));
            file.write(reinterpret_cast<const char*>(PST_Endgame), sizeof(PST_Endgame));

            if (!file.good()) {
                std::cerr << "[WeightsIO] Error: File stream corrupted during writing." << std::endl;
                return false;
            }

            file.close();
            std::cout << "[WeightsIO] Evaluation weights successfully saved to " << filename << std::endl;
            return true;
        }

        /**
         * @brief Deserializes evaluation weights from a binary file.
         * Validates the file header before modifying the engine's current parameters.
         * @param filename Path to the input file.
         * @return True if the file was loaded and passed validation.
         */
        static bool load_weights(const std::string& filename) {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                std::cout << "[WeightsIO] Notice: No custom weights found (" << filename 
                          << "). Using built-in defaults." << std::endl;
                return false;
            }

            // 1. Read and validate the header
            FileHeader header;
            file.read(reinterpret_cast<char*>(&header), sizeof(FileHeader));

            if (header.magic != FILE_MAGIC) {
                std::cerr << "[WeightsIO] Critical Error: Invalid file format in " << filename 
                          << ". Expected MyChess weights file." << std::endl;
                return false;
            }
            if (header.version != FILE_VERSION) {
                std::cerr << "[WeightsIO] Critical Error: Unsupported weights version (" 
                          << header.version << "). Expected version " << FILE_VERSION << "." << std::endl;
                return false;
            }

            // 2. Load the parameter arrays
            file.read(reinterpret_cast<char*>(weight), sizeof(weight));
            file.read(reinterpret_cast<char*>(PST_Midgame), sizeof(PST_Midgame));
            file.read(reinterpret_cast<char*>(PST_Endgame), sizeof(PST_Endgame));

            // 3. Verify that all bytes were successfully read
            if (!file.good()) {
                std::cerr << "[WeightsIO] Critical Error: Premature EOF or corrupted data in " << filename << std::endl;
                return false;
            }

            file.close();
            std::cout << "[WeightsIO] Custom evaluation weights loaded successfully from " << filename << std::endl;
            return true;
        }

        /**
         * @brief Resets all evaluation parameters (Weights and PSTs) to their initial state.
         * Dynamically generates symmetrical values for Black pieces.
         */
        static void reset_to_defaults() {
            // Reset piece weights symmetrically
            for (int i = 0; i < 6; i++) {
                weight[i] = DEFAULT_WEIGHT[i];
                weight[i + 6] = -DEFAULT_WEIGHT[i];
            }

            // Copy White PST defaults
            for (int piece = 0; piece < 6; piece++) {
                std::memcpy(PST_Midgame[piece], DEFAULT_PST_MIDGAME[piece], 64 * sizeof(double));
                std::memcpy(PST_Endgame[piece], DEFAULT_PST_ENDGAME[piece], 64 * sizeof(double));
            }

            // Symmetrically generate Black PSTs (mirror vertically and invert evaluation)
            for (short piece = 0; piece < 6; piece++) {
                for (Square cell = 0; cell < 64; cell++) {
                    Square mirrored_cell = cell ^ 56;
                    PST_Midgame[piece + 6][mirrored_cell] = -PST_Midgame[piece][cell];
                    PST_Endgame[piece + 6][mirrored_cell] = -PST_Endgame[piece][cell];
                }
            }
            std::cout << "[WeightsIO] All weights and PSTs have been reset to defaults." << std::endl;
        }

        /**
         * @brief Beautifully prints the current piece weights and 8x8 PST tables to the console.
         */
        static void print_weights() {
            const std::string piece_names[6] = { "PAWN", "KNIGHT", "BISHOP", "ROOK", "QUEEN", "KING" };
            
            std::cout << "\n=================================================" << std::endl;
            std::cout << "             CURRENT ENGINE WEIGHTS              " << std::endl;
            std::cout << "=================================================" << std::endl;
            
            std::cout << "\n--- PIECE VALUES ---" << std::endl;
            for (int i = 0; i < 6; i++) {
                std::cout << piece_names[i] << ": " << weight[i] << std::endl;
            }

            std::cout << "\n--- PST MIDGAME TABLES (WHITE) ---" << std::endl;
            for (int piece = 0; piece < 6; piece++) {
                std::cout << "\nPiece: " << piece_names[piece] << " (Midgame)" << std::endl;
                for (int rank = 7; rank >= 0; rank--) {
                    for (int file = 0; file < 8; file++) {
                        int cell = rank * 8 + file;
                        std::printf("%6.1f ", PST_Midgame[piece][cell]);
                    }
                    std::cout << std::endl;
                }
            }

            std::cout << "\n--- PST ENDGAME TABLES (WHITE) ---" << std::endl;
            for (int piece = 0; piece < 6; piece++) {
                std::cout << "\nPiece: " << piece_names[piece] << " (Endgame)" << std::endl;
                for (int rank = 7; rank >= 0; rank--) {
                    for (int file = 0; file < 8; file++) {
                        int cell = rank * 8 + file;
                        std::printf("%6.1f ", PST_Endgame[piece][cell]);
                    }
                    std::cout << std::endl;
                }
            }
            std::cout << "=================================================\n" << std::endl;
        }
    };
}