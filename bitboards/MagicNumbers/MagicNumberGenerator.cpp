#include <iostream>

#include "MagicNumbers.h"

int main() {
    Board bishop_occupancy_masks[64] = {0};
    Board rook_occupancy_masks[64] = {0};
    for (Square cell = 0; cell < 64; cell ++) {
        // OCCUPANCY MASKS
        Square rank = cell / 8;
        Square file = cell % 8;
        // ROOK OCCUPANCY MASKS
        for (Square i = rank + 1; i < 7; ++i)                      
            rook_occupancy_masks[cell] |= (1ULL << (i * 8 + file));    // UP
        for (Square i = 1; i < rank; ++i)                          
            rook_occupancy_masks[cell] |= (1ULL << (i * 8 + file));    // DOWN
        for (Square i = file + 1; i < 7; ++i)                      
            rook_occupancy_masks[cell] |= (1ULL << (rank * 8 + i));    // RIGHT
        for (Square i = 1; i < file; ++i)                          
            rook_occupancy_masks[cell] |= (1ULL << (rank * 8 + i));    // LEFT
        // BISHOP OCCUPANCY MASKS
        for (Square r = rank + 1, f = file + 1; r < 7 && f < 7; ++r, ++f)
            bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // RIGHT, UP
        if (file > 0) {
            for (Square r = rank + 1, f = file - 1; r < 7 && f > 0; ++r, --f)
                bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // LEFT, UP
        }
        if (rank > 0) {
            for (Square r = rank - 1, f = file + 1; r > 0 && f < 7; --r, ++f)
                bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // RIGHT, DOWN
        }
        if (rank > 0 && file > 0) {
            for (Square r = rank - 1, f = file - 1; r > 0 && f > 0; --r, --f)
                bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // LEFT, DOWN
        }
}

    std::cout << "constexpr Board bishop_magic_numbers[CELL_NB] = {";
    for(Square i = 0; i < 64; ++i) {
        std::cout << MyChess::find_magic(i, bishop_occupancy_masks[i], false) << "ULL";
        if (i != 63) std::cout << ", ";
    }
    std::cout << "};" << std::endl;

    std::cout << "constexpr Board rook_magic_numbers[CELL_NB] = {";
    for(Square i = 0; i < 64; ++i) {
        std::cout << MyChess::find_magic(i, rook_occupancy_masks[i]) << "ULL";
        if (i != 63) std::cout << ", ";
    }
    std::cout << "};" << std::endl;
}