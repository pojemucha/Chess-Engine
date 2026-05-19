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

#if defined(_MSC_VER)
    #include <intrin.h>
    #pragma intrinsic(_BitScanForward64)
#endif

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
            #if defined(_MSC_VER)
                return static_cast<Square>(__popcnt64(b));
            #elif defined(__GNUC__) || defined(__clang__)
                return static_cast<Square>(__builtin_popcountll(b));
            #else
                Square count = 0;
                while (b) {
                    b &= (b - 1);
                    count++;
                }
                return count;
            #endif
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
        static constexpr Board bishop_magic_numbers[CELL_NB] = { 6391417146458800228ULL, 11963883965405937664ULL, 1481841035125522800ULL, 290491268547036418ULL, 5777010858190768200ULL, 2779285314304213824ULL, 54118169351095876ULL, 9223796461244858504ULL, 4612328416822698265ULL, 191002813873394216ULL, 144330984479801494ULL, 617006772594417924ULL, 2619982299376681364ULL, 10955008327004192774ULL, 3096800404848936ULL, 602357043412599808ULL, 16194953075321505024ULL, 74330439462175440ULL, 5585595244912714241ULL, 613662736852844676ULL, 10138152201102372ULL, 13989873763538305568ULL, 1298197916783153233ULL, 13839845391254980202ULL, 108666938045239330ULL, 2380438848259752962ULL, 2459009377077036513ULL, 1154047680129860098ULL, 297519325865066564ULL, 9396760762112804880ULL, 9512732747128964140ULL, 1731078015228316168ULL, 5806339074560630874ULL, 4054440452689629188ULL, 171911151264727088ULL, 463889487693611072ULL, 9422392454755680480ULL, 279241048255922306ULL, 150928314763313664ULL, 14502512217267766020ULL, 3533087173282629640ULL, 9247297995604497536ULL, 2963932750438091008ULL, 10665126733488912388ULL, 3463277188718011393ULL, 4917957765755868290ULL, 11905550667806608387ULL, 678381132835719312ULL, 9259845124083689253ULL, 9954164656526475424ULL, 13284549077289140288ULL, 6415386491326039552ULL, 145210730484757ULL, 144361607832933018ULL, 9250450879313939101ULL, 76563539059671552ULL, 10458908650589127684ULL, 2884644118206111784ULL, 144513839460254734ULL, 11601564595374836740ULL, 4039817247670174208ULL, 380563411584960016ULL, 1451294192763439362ULL, 9530760372546502929ULL };
        static constexpr Board rook_magic_numbers[CELL_NB] = { 1188950942649974818ULL, 5818650993977511936ULL, 5548469932825530392ULL, 2954378948425097472ULL, 16789424908672696346ULL, 504480128374932480ULL, 11024816288030523648ULL, 9367491666467949186ULL, 422238843046020ULL, 11568058738887691396ULL, 6921469746579898438ULL, 725361152557580544ULL, 2469379992157945874ULL, 9298244466827984968ULL, 5629569344471624ULL, 5197716921566630018ULL, 586037499126677568ULL, 4504425334906882ULL, 2053942696845854208ULL, 2454462896713768972ULL, 371547521161888768ULL, 349170258398938112ULL, 14425218722501824648ULL, 621863985470128404ULL, 14424047057536827392ULL, 17629974086885052416ULL, 10450321679386148928ULL, 12129604611662151896ULL, 4507999821889664ULL, 2449962597491933696ULL, 12754489503979016ULL, 2411237590320804ULL, 185843859287179904ULL, 11551743077404115138ULL, 4674737513812727330ULL, 6998735211282305024ULL, 2475572798144120112ULL, 6992964355792961552ULL, 77915806284934664ULL, 74345713269473444ULL, 3220147402996645920ULL, 14231445195538497568ULL, 9387830389646688272ULL, 4647724711319502880ULL, 36592366589181984ULL, 2900881179572568136ULL, 874826530525806593ULL, 984652799416598548ULL, 5764750460770593280ULL, 3213462103914508800ULL, 312227235144668416ULL, 369964776791474432ULL, 9224164785946363136ULL, 55732062841761280ULL, 5771442573351584768ULL, 6922081015997696512ULL, 16825453435102102145ULL, 3606398289446978050ULL, 11681776822190702658ULL, 6053611981142892630ULL, 3031485637351452942ULL, 4648277908611140714ULL, 11531496601516777604ULL, 11354670553531394ULL};
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
        inline constexpr Atlas() noexcept : pawn_moves{}, knight_moves{}, bishop_moves{}, rook_moves{}, king_moves{}, bishop_occupancy_masks{}, rook_occupancy_masks{}, bishop_shifts{}, rook_shifts{}, z_pieces{}, z_side{}, z_castling{}, z_ep{} {
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
                // ROOK OCCUPANCY MASKS - Exclude the board edges (relevant occupancy)
                for (int i = (int)rank + 1; i < 7; ++i)
                    rook_occupancy_masks[cell] |= (1ULL << (i * 8 + file));    // UP
                for (int i = 1; i < (int)rank; ++i)
                    rook_occupancy_masks[cell] |= (1ULL << (i * 8 + file));    // DOWN
                for (int i = (int)file + 1; i < 7; ++i)
                    rook_occupancy_masks[cell] |= (1ULL << (rank * 8 + i));    // RIGHT
                for (int i = 1; i < (int)file; ++i)
                    rook_occupancy_masks[cell] |= (1ULL << (rank * 8 + i));    // LEFT
                // BISHOP OCCUPANCY MASKS - Exclude board edges
                // Upper-right diagonal
                for (int r = (int)rank + 1, f = (int)file + 1; r < 7 && f < 7; ++r, ++f)
                    bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));
                // Upper-left diagonal
                if ((int)file > 0) {
                    for (int r = (int)rank + 1, f = (int)file - 1; r < 7 && f > 0; ++r, --f)
                        bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));
                }
                if ((int)rank > 0) {
                    for (int r = (int)rank - 1, f = (int)file + 1; r > 0 && f < 7; --r, ++f)
                        bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));
                }
                if ((int)rank > 0 && (int)file > 0) {
                    for (int r = (int)rank - 1, f = (int)file - 1; r > 0 && f > 0; --r, --f)
                        bishop_occupancy_masks[cell] |= (1ULL << (r * 8 + f));
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

