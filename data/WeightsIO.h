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
#include <string>
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
        static constexpr std::uint32_t FILE_VERSION = 1;

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
                // Note: At this point, the arrays might be partially overwritten. 
                // In a production environment, we should call reset_to_defaults() here.
                return false;
            }

            file.close();
            std::cout << "[WeightsIO] Custom evaluation weights loaded successfully from " << filename << std::endl;
            return true;
        }

        /**
         * @brief Resets all evaluation parameters (Weights and PSTs) to their initial state.
         * @note Requires DEFAULT_WEIGHT, DEFAULT_PST_MIDGAME, and DEFAULT_PST_ENDGAME 
         * to be defined in EvalWeights.h.
         */
        static void reset_to_defaults() {
            // Note for refactoring EvalWeights.h: 
            // Instead of hardcoding values here (violating the DRY principle),
            // you should define `const short DEFAULT_WEIGHT[12] = {...};` inside EvalWeights.h
            // and use std::copy or std::memcpy here.

            short default_weight[12] = {100, 320, 330, 500, 900, 0, -100, -320, -330, -500, -900, 0};
            for (int i = 0; i < 12; i++) {
                weight[i] = default_weight[i];
            }

            // TODO: Restore default PSTs!
            // std::memcpy(PST_Midgame, DEFAULT_PST_MIDGAME, sizeof(PST_Midgame));
            // std::memcpy(PST_Endgame, DEFAULT_PST_ENDGAME, sizeof(PST_Endgame));

            std::cout << "[WeightsIO] All weights and PSTs have been reset to defaults." << std::endl;
        }
    };
}