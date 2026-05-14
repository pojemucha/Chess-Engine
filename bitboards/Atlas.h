/**
 * @file Atlas.h
 * @brief High-performance move generation using Magic Bitboards.
 * 
 * This module precomputes attack patterns for all chess pieces during compile-time.
 * For sliding pieces (Rooks and Bishops), it employs the Magic Bitboard technique,
 * which uses hashing to map board occupancy to move masks.
 */

#pragma once

#include <cstdint>

/**
 * @brief Basic type aliases for chess-related data.
 */
using Board = std::uint64_t;     ///< 64-bit bitboard representation
using Square = unsigned short;   ///< Square index from 0 (a1) to 63 (h8)

namespace MyChess {

    /**
     * @namespace Math
     * @brief Low-level bit manipulation and pseudo-random number generation.
     */
    namespace Math {
        
        /**
         * @brief Counts the number of set bits (1s) in a bitboard.
         * Implementation of Brian Kernighan’s algorithm.
         */
        inline constexpr Square count_bits(Board b) noexcept {
            Square count = 0;
            while (b) {
                b &= (b - 1);
                count++;
            }
            return count;
        }

        /**
         * @brief 64-bit Xorshift Pseudo-Random Number Generator.
         * 
         * Used to generate candidates for magic numbers.
         */
        inline constexpr Board next_random(Board& state) noexcept {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            return state * 2685821657736338717ULL;
        }
    }

    /**
     * @enum Color
     * @brief Basic colors in chess.
     */
    enum Color : Square {WHITE, BLACK, COLOR_NB};
    
    /**
     * @enum Piece
     * @brief Detailed piece types including colors.
     */
    enum Piece : Square {
        WHITE_PAWN, WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN, WHITE_KING,
        BLACK_PAWN, BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN, BLACK_KING,
        PIECE_NB, EMPTY
    };

    constexpr Square CELL_NB = 64U;
    
    /**
     * @struct Atlas
     * @brief A singleton-like structure containing precomputed move masks.
     */
    struct Atlas {
    private:
        Board bishop_occupancy_masks[CELL_NB];
        Board rook_occupancy_masks[CELL_NB];
        Board bishop_shifts[CELL_NB];
        Board rook_shifts[CELL_NB];
        Board bishop_magic_numbers[CELL_NB] = {2450538752448727367ULL, 4625284787974750212ULL, 493144996749574145ULL, 290491268547036418ULL, 5777010858190768200ULL, 442566659396346912ULL, 2139134369992010ULL, 363384626846632149ULL, 4612328416822698265ULL, 191002813873394216ULL, 144330984479801494ULL, 5787415835201507396ULL, 5801414826400522247ULL, 10955008327004192774ULL, 360503560452190218ULL, 602357043412599808ULL, 596938933167729668ULL, 6100126812107965651ULL, 10020509179801897120ULL, 9333712443966474368ULL, 867787388096086568ULL, 775041370116169729ULL, 1298197916783153233ULL, 789819889044113441ULL, 108666938045239330ULL, 6413974694098109440ULL, 1470443167009505376ULL, 10682542718900637700ULL, 297519325865066564ULL, 5999640229207605392ULL, 757028186856624137ULL, 18167233880539652ULL, 5806339074560630874ULL, 74310700665153060ULL, 3693515262910661157ULL, 463889487693611072ULL, 234189655892492544ULL, 279241048255922306ULL, 1765976847688139273ULL, 14502512217267766020ULL, 3533087173282629640ULL, 576674093060857924ULL, 6030690470961716482ULL, 5514801368310876164ULL, 3463277188718011393ULL, 4917957765755868290ULL, 11905550667806608387ULL, 678381132835719312ULL, 9259845124083689253ULL, 9954164656526475424ULL, 15331188680875312896ULL, 6415386491326039552ULL, 2542431798488077441ULL, 5651525471060168ULL, 15006007479027384328ULL, 76563539059671552ULL, 36418028843307033ULL, 2884644118206111784ULL, 14060242581007763470ULL, 4937705521124902913ULL, 5675252550608642564ULL, 380563411584960016ULL, 1451294192763439362ULL, 9530760372546502929ULL};
        Board rook_magic_numbers[CELL_NB] = {72076835495674400ULL, 9277432842822950916ULL, 2341906996384501760ULL, 216181646928708128ULL, 5332266563045169440ULL, 4647719215640477767ULL, 6052838483570590723ULL, 1188961436328575232ULL, 1300555143850459810ULL, 10062733907140616ULL, 10261733570628296704ULL, 725361152557580544ULL, 5066687254661200ULL, 14261211243711037952ULL, 1306888342637641984ULL, 1266074462703190209ULL, 17017005673407320162ULL, 3107488415813206088ULL, 468445830039535632ULL, 2454462896713768972ULL, 9368332750580371468ULL, 1162078242279002116ULL, 1170975485573202192ULL, 621863985470128404ULL, 11259350914295169568ULL, 4845873478225691008ULL, 5770378455844667905ULL, 12129604611662151896ULL, 288530156279431203ULL, 5767422350115229768ULL, 576478361669862242ULL, 1922196472837719041ULL, 1459171526115852672ULL, 5796142341165162496ULL, 1235253072742060036ULL, 2319406588999246024ULL, 144678449716397216ULL, 8142791802441714688ULL, 38036810261073928ULL, 15461855892655317569ULL, 9525183590841745412ULL, 14231445195538497568ULL, 9387830389646688272ULL, 2686433461878980632ULL, 5514095137906884616ULL, 1876312214232760336ULL, 6377114900938620936ULL, 5189277091862020101ULL, 4616331868613182976ULL, 604643444010287744ULL, 81700861307388416ULL, 11601870980607134976ULL, 2452852121087803520ULL, 2060538271385649280ULL, 797139367700530176ULL, 2306336146203083264ULL, 4973241728481108482ULL, 1225078055800701186ULL, 13921190018846081058ULL, 9587741936097537ULL, 686237230306427970ULL, 2964213400785911825ULL, 5188155846039700996ULL, 595060091628110086ULL};
        Board pawn_moves[COLOR_NB][CELL_NB];
        Board knight_moves[CELL_NB];
        Board bishop_moves[CELL_NB][512];
        Board rook_moves[CELL_NB][4096];
        Board king_moves[CELL_NB];

    public:
        Board z_pieces[12][64];
        Board z_side;
        Board z_castling[16];
        Board z_ep[8];
        /**
         * @brief Constexpr constructor that populates all tables at compile-time.
         */
        inline constexpr Atlas() noexcept : pawn_moves{}, knight_moves{}, bishop_moves{}, rook_moves{}, king_moves{}, bishop_occupancy_masks{}, rook_occupancy_masks{}, bishop_shifts{}, rook_shifts{}, bishop_magic_numbers{}, rook_magic_numbers{}, z_pieces{}, z_side{}, z_castling{}, z_ep{} {
            constexpr Board not_a_file    = 0xFEFEFEFEFEFEFEFEULL;
            constexpr Board not_h_file    = 0x7F7F7F7F7F7F7F7FULL;
            constexpr Board not_ab_file   = 0xFCFCFCFCFCFCFCFCULL;
            constexpr Board not_gh_file   = 0x3F3F3F3F3F3F3F3FULL;
            for (Square cell = 0; cell < CELL_NB; cell++) {
                // WHITE PAWN MOVES
                pawn_moves[WHITE][cell] |= ((1ULL << cell) << 9) & not_a_file; // 1 RIGHT, 1 UP
                pawn_moves[WHITE][cell] |= ((1ULL << cell) << 7) & not_h_file; // 1 LEFT, 1 UP
                // BLACK PAWN MOVES
                pawn_moves[BLACK][cell] |= ((1ULL << cell) >> 7) & not_a_file; // 1 RIGHT, 1 DOWN
                pawn_moves[BLACK][cell] |= ((1ULL << cell) >> 9) & not_h_file; // 1 LEFT, 1 DOWN
                // KNIGHT MOVES
                knight_moves[cell] |= ((1ULL << cell) << 17) & not_a_file;     // 1 RIGHT, 2 UP
                knight_moves[cell] |= ((1ULL << cell) << 15) & not_h_file;     // 1 LEFT, 2 UP
                knight_moves[cell] |= ((1ULL << cell) << 10) & not_ab_file;    // 2 RIGHT, 1 UP
                knight_moves[cell] |= ((1ULL << cell) << 6)  & not_gh_file;    // 2 LEFT, 1 UP
                knight_moves[cell] |= ((1ULL << cell) >> 6)  & not_ab_file;    // 2 RIGHT, 1 DOWN
                knight_moves[cell] |= ((1ULL << cell) >> 10) & not_gh_file;    // 2 LEFT, 1 DOWN
                knight_moves[cell] |= ((1ULL << cell) >> 15) & not_a_file;     // 1 RIGHT, 2 DOWN
                knight_moves[cell] |= ((1ULL << cell) >> 17) & not_h_file;     // 1 LEFT, 2 DOWN
                // KING MOVES
                king_moves[cell] |= ((1ULL << cell) << 9) & not_a_file;        // 1 RIGHT, 1 UP
                king_moves[cell] |=  (1ULL << cell) << 8;                      // 1 UP
                king_moves[cell] |= ((1ULL << cell) << 7) & not_h_file;        // 1 LEFT, 1 UP
                king_moves[cell] |= ((1ULL << cell) << 1) & not_a_file;        // 1 RIGHT
                king_moves[cell] |= ((1ULL << cell) >> 1) & not_h_file;        // 1 LEFT
                king_moves[cell] |= ((1ULL << cell) >> 7) & not_a_file;        // 1 RIGHT, 1 DOWN
                king_moves[cell] |=  (1ULL << cell) >> 8;                      // 1 DOWN
                king_moves[cell] |= ((1ULL << cell) >> 9) & not_h_file;        // 1 LEFT, 1 DOWN
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
                    for (Square r = rank + 1, f = file - 1; r < 7 && f > 1; ++r, --f)
                        bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // LEFT, UP
                }

                if (rank > 0) {
                    for (Square r = rank - 1, f = file + 1; r > 1 && f < 7; --r, ++f)
                        bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // RIGHT, DOWN
                }

                if (rank > 0 && file > 0) {
                    for (Square r = rank - 1, f = file - 1; r > 1 && f > 1; --r, --f)
                        bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));     // LEFT, DOWN
                }
                // ROOK SHIFTS
                rook_shifts[cell] = (64 - Math::count_bits(rook_occupancy_masks[cell]));
                // BISHOP SHIFTS
                bishop_shifts[cell] = (64 - Math::count_bits(bishop_occupancy_masks[cell]));
                // ROOK MOVES
                Board subset = 0;
                do {
                    Square index = (subset * rook_magic_numbers[cell]) >> rook_shifts[cell];
                    rook_moves[cell][index] = slow_rook_attacks(cell, subset);
                    subset = (subset - rook_occupancy_masks[cell]) & rook_occupancy_masks[cell];
                } while (subset != 0);
                // BISHOP MOVES
                subset = 0;
                do {
                    Square index = (subset * bishop_magic_numbers[cell]) >> bishop_shifts[cell];
                    bishop_moves[cell][index] = slow_bishop_attacks(cell, subset);
                    subset = (subset - bishop_occupancy_masks[cell]) & bishop_occupancy_masks[cell];
                } while (subset != 0);
            }
            generate_Zobrish_hash();
        }

         /**
         * @brief Retrieves precomputed pawn attack mask.
         * @param color The color of the pawn (WHITE or BLACK).
         * @param cell The square where the pawn is located.
         * @return A bitboard representing all possible capture squares.
         */
        inline Board get_pawn_attacks(const Color color, const Square cell) const noexcept {
            return pawn_moves[color][cell];
        }

        /**
         * @brief Retrieves precomputed knight move mask.
         * @param cell The square where the knight is located.
         * @return A bitboard representing all squares the knight can jump to.
         */
        inline Board get_knight_attacks(const Square cell) const noexcept {
            return knight_moves[cell];
        }
        
        /**
         * @brief Calculates bishop attacks based on current board occupancy.
         * @param cell The square where the bishop is located.
         * @param occupancy The bitboard of all pieces currently on the board.
         * @return A bitboard of all squares the bishop can move to or capture on.
         */
        inline Board get_bishop_attacks(const Square cell, const Board occupancy) const noexcept {
            Square index = ((occupancy & bishop_occupancy_masks[cell]) * bishop_magic_numbers[cell]) >> bishop_shifts[cell];
            return bishop_moves[cell][index];
        }

        /**
         * @brief Calculates rook attacks based on current board occupancy.
         * @param cell The square where the rook is located.
         * @param occupancy The bitboard of all pieces currently on the board.
         * @return A bitboard of all squares the rook can move to or capture on.
         */
        inline Board get_rook_attacks(const Square cell, const Board occupancy) const noexcept { 
            Square index = ((occupancy & rook_occupancy_masks[cell]) * rook_magic_numbers[cell]) >> rook_shifts[cell];
            return rook_moves[cell][index];
        }

        /**
         * @brief Calculates queen attacks by combining rook and bishop patterns.
         * @param cell The square where the queen is located.
         * @param occupancy The bitboard of all pieces currently on the board.
         * @return A bitboard of all legal queen moves in the current position.
         */
        inline Board get_queen_attacks(const Square cell, const Board occupancy) const noexcept {
            return get_rook_attacks(cell, occupancy) | get_bishop_attacks(cell, occupancy);
        }

        /**
         * @brief Retrieves precomputed king move mask.
         * @param cell The square where the king is located.
         * @return A bitboard representing all 8 adjacent squares.
         */
        inline Board get_king_attacks(const Square cell) const noexcept {
            return king_moves[cell];
        }

    private:
        /**
         * @brief Calculates Rook attacks by scanning the board in four directions.
         * @param cell The square index of the Rook.
         * @param blockers Bitboard of all pieces that can block the Rook's path.
         * @return A bitboard of all squares the Rook can attack.
         * @note This is a slow reference function used only during precomputation.
         */
        inline constexpr Board slow_rook_attacks(const Square cell, const Board blockers) noexcept {
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
        inline constexpr Board slow_bishop_attacks(const Square cell, const Board blockers) noexcept {
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

        inline constexpr void generate_Zobrish_hash() noexcept {
            Board seed = 0xFA13BC4123AA12E6ULL;
            for (Square i = 0; i < PIECE_NB; i++)
                for (short j = 0; j < CELL_NB; j++)
                    z_pieces[i][j] = Math::next_random(seed);
            z_side = Math::next_random(seed);
            for (Square i = 0; i < 16; i++) z_castling[i] = Math::next_random(seed);
            for (Square i = 0; i < 8; i++) z_ep[i] = Math::next_random(seed);
        } 
    };

    /**
     * @brief The global instance of Atlas, computed at compile-time.
     */
    inline constexpr Atlas atlas;
}
