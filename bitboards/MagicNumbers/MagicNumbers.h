#pragma once

#include <iostream>
#include <cstdint>

#include "../Atlas.h"

namespace MyChess {
    /**
     * @brief Calculates Rook attacks by scanning the board in four directions.
     * @param cell The square index of the Rook.
     * @param blockers Bitboard of all pieces that can block the Rook's path.
     * @return A bitboard of all squares the Rook can attack.
     * @note This is a slow reference function used only during precomputation.
     */
    inline Board slow_rook_attacks(const Square cell, const Board blockers) noexcept {
        Board attacks = 0;
        int rank = cell / 8, file = cell % 8;
        // UP
        for (int i = rank + 1; i < 8; i++) {
            attacks |= (1ULL << (i * 8 + file));    
            if (blockers & (1ULL << (i * 8 + file))) break;
        }
        // DOWN
        if (rank > 0) {
            for (int i = rank - 1; i >= 0; i--) {
                attacks |= (1ULL << (i * 8 + file));    
                if (blockers & (1ULL << (i * 8 + file)) || i == 0) break;
            } 
        }
        // RIGHT
        for (int i = file + 1; i < 8; i++) {
            attacks |= (1ULL << (rank * 8 + i));     
            if (blockers & (1ULL << (rank * 8 + i))) break;
        } 
        // LEFT
        for (int i = file - 1; i >= 0; i--) {
            attacks |= (1ULL << (rank * 8 + i));  
            if (blockers & (1ULL << (rank * 8 + i)) || i == 0) break;
        }    
        
        return attacks;
    }

    /**
     * @brief Calculates Bishop attacks by scanning the four diagonals.
     * @param cell The square index of the Bishop.
     * @param blockers Bitboard of all pieces that can block the Bishop's path.
     * @return A bitboard of all squares the Bishop can attack.
     * @note This is a slow reference function used only during precomputation.
     */
    inline Board slow_bishop_attacks(const Square cell, const Board blockers) noexcept {
        Board attacks = 0;
        int rank = cell / 8, file = cell % 8; 
        // RIGHT, UP
        for (int r = rank + 1, f = file + 1; r < 8 && f < 8; r++, f++) {
            attacks |= (1ULL << (r * 8 + f));
            if (blockers & (1ULL << (r * 8 + f))) break;
        }
        // LEFT, UP
        for (int r = rank + 1, f = file - 1; r < 8 && f >= 0; r++, f--) {
            attacks |= (1ULL << (r * 8 + f)); 
            if (blockers & (1ULL << (r * 8 + f)) || f == 0) break;
        }     
        // RIGHT, DOWN
        for (int r = rank - 1, f = file + 1; r >= 0 && f < 8; r--, f++) {
            attacks |= (1ULL << (r * 8 + f));     
            if (blockers & (1ULL << (r * 8 + f)) || r == 0) break;
        }    
        // LEFT, DOWN
        for (int r = rank - 1, f = file - 1; r >= 0 && f >= 0; r--, f--) {
            attacks |= (1ULL << (r * 8 + f));     
            if (blockers & (1ULL << (r * 8 + f)) || r == 0 || f == 0) break;
        }    
    
        return attacks;
    }  
    /**
     * @brief Finds a Magic Number for a specific square using a trial-and-error approach.
     * 
     * This function generates random candidates and checks for index collisions.
     * A collision is only acceptable if both blockers map to the same attack mask.
     * @param cell The square index being processed.
     * @param occupancy_mask The relevant occupancy bits for this square.
     * @param is_rook True for Rooks, False for Bishops.
     * @return A valid 64-bit Magic Number.
     */
    inline Board find_magic(const Square cell, const Board occupancy_mask, const bool is_rook = true) noexcept {
        Board blockers_cache[4096]{}, attacks_cache[4096]{}, verify[4096]{};
        Square bits = Math::count_bits(occupancy_mask);
        Square num_subsets = 0;
        Board subset = 0;
        do {
            blockers_cache[num_subsets] = subset;
            attacks_cache[num_subsets] = is_rook ? slow_rook_attacks(cell, subset) : slow_bishop_attacks(cell, subset);
            num_subsets++;
            subset = (subset - occupancy_mask) & occupancy_mask;
        } while (subset != 0);
        Board seed = cell * 12 + 777;
        int attempt = 0;
        while (true) {
            bool fail = false;
            attempt++;
            for (Square i = 0; i < 4096; i++) verify[i] = 0ULL;
            Board candidate_magic = Math::next_random(seed) & Math::next_random(seed);
            for (Square i = 0; i < num_subsets; i++) {
                Square index = (blockers_cache[i] * candidate_magic) >> (64 - bits);
                if (verify[index] == 0ULL) {
                    verify[index] = attacks_cache[i] + 1;
                } else if (verify[index] != attacks_cache[i] + 1) {
                    fail = true;
                    break;
                }
            } 
            if (!fail) return candidate_magic;
        }
    }
}