/**
 * @file Position.h
 * @brief Chess engine core containing board representation, move generation, and search algorithms.
 * 
 * Refactored to adhere to the Single Responsibility Principle (SRP):
 * - Position: Manages board state, move execution, and static evaluation.
 * - Search: Manages alpha-beta traversal, time limits, and transposition tables.
 */

#pragma once

#include <vector>
#include <string>
#include <functional>
#include <cmath>
#include <chrono>

#include "EvalWeights.h"
#include "Atlas.h"

/// @brief Represents a single chess move encoded in 16 bits (From: 6, To: 6, Flag: 4).
using Move = std::uint16_t;

namespace MyChess {

    /// @name Engine Constants
    /// @{
    namespace Constants {
        constexpr short MAX_DEPTH = 64;         ///< Maximum expected search depth (ply).
        constexpr short MAX_GAME_PLIES = 1024;  ///< Maximum game length (512 full moves) to prevent overflows.
        constexpr short VALUE_INFINITE = 32767; ///< Infinite evaluation score
        constexpr short VALUE_MATE = 32000;     ///< Score threshold for checkmate (used to detect mate in N)

        constexpr int SCORE_TT = 5000000;       ///< Bonus for moves found in the Transposition Table
        constexpr int SCORE_KILLER_1 = 900000;  ///< Bonus for 1st killer move
        constexpr int SCORE_KILLER_2 = 800000;  ///< Bonus for 2nd Killer move

        constexpr size_t TT_SIZE = 1 << 20;     ///< ~1 Million entries (~24MB) for the Transposition Table
    }
    /// @}

    namespace Math {
        /// @brief De Bruijn sequence multiplier table for fallback LSB extraction.
        constexpr Square DeBruijn_table[64] = {
             0,  1, 48,  2, 57, 49, 28,  3,
            61, 58, 50, 42, 38, 29, 17,  4,
            62, 55, 59, 36, 53, 51, 43, 22,
            45, 39, 33, 30, 24, 18, 12,  5,
            63, 47, 56, 27, 60, 41, 37, 16,
            54, 35, 52, 21, 44, 32, 23, 11,
            46, 26, 40, 15, 34, 20, 31, 10,
            25, 14, 19,  9, 13,  8,  7,  6
        };
        /**
         * @brief Finds the index of the Least Significant 1-Bit (LSB).
         * Utilizes hardware intrinsics if available, otherwise falls back to De Bruijn multiplication.
         * @param b The bitboard to process (MUST NOT be 0).
         * @return The index of the first set bit (0-63).
         */
        inline Square countr_zero(Board b) noexcept {
            #if defined(__GNUC__) || defined(__clang__)
                return static_cast<Square>(__builtin_ctzll(b));
            #elif defined(_MSC_VER)
                unsigned long index;
                _BitScanForward64(&index, b);
                return static_cast<Square>(index);
            #else
                return DeBruijn_table[((b & (~b + 1)) * 0x03f79d71b4cb0a89ULL) >> 58];
            #endif
        }
    }

    /**
     * @enum move_flags
     * @brief 4-bit flags encoding the special properties of a move.
     */
    enum move_flags : std::uint8_t {
        SILENT_MOVE, PAWN_DOUBLE_JUMP, CASTLING, EN_PASSANT, CAPTURE, 
        KNIGHT_PROMOTION, BISHOP_PROMOTION, ROOK_PROMOTION, QUEEN_PROMOTION,
        KNIGHT_PROMOTION_AND_CAPTURE, BISHOP_PROMOTION_AND_CAPTURE, 
        ROOK_PROMOTION_AND_CAPTURE, QUEEN_PROMOTION_AND_CAPTURE
    };

    /**
     * @enum TT_flags
     * @brief Transposition Table node types (Bound limits).
     */
    enum TT_flags : std::uint8_t { 
        EXACT, ///< Exact evaluation score (PV node) 
        ALPHA, ///< Upper bound (Fail-low node)
        BETA   ///< Lower bound (Fail-high node)
    };

    /// @name Move Bitwise Utilities
    /// @{

    /**
     * @brief Packs 'from', 'to' squares and a special 'flag' into a 16-bit Move.
     */
    inline Move encode_move(const Square from, const Square to, const std::uint8_t flag) noexcept {
        return (from | (to << 6) | (flag << 12));
    }

    // Bitwise decoders for the 16-bit move
    inline Square get_from(const Move move) noexcept { return move & 0x3F; }
    inline Square get_to(const Move move) noexcept { return (move >> 6) & 0x3F; }
    inline std::uint8_t get_flag(const Move move) noexcept { return move >> 12; }
    /// @}
    

    /**
     * @struct UndoInfo
     * @brief Stores irreversible state information to restore the board during unmake_move().
     */
    struct UndoInfo {
        Piece captured_piece;         ///< The piece that was captured (if any)
        std::uint8_t castling_rights; ///< Castling rights mask before the move
        Square en_passant_square;     ///< En passant target square before the move (or 64 if none)
    };

    /**
     * @struct MoveList
     * @brief A pre-allocated structure to hold generated moves.
     * Prevents dynamic memory allocations during the search tree traversal.
     */
    struct MoveList {
        Move moves[256];   ///< Maximum possible theoretical moves in chess is 218, but we allocate 256 for safety and alignment.
        int scores[256];   ///< Heuristic scores for move ordering (e.g., MVV/LVA, killer moves, history heuristic).
        Square count = 0;

        /**
         * @brief Appends a new move to the list.
         */
        void add(const Move move, const int move_score) {
            if (count < 256) {
                scores[count] = move_score;
                moves[count++] = move;
            }
        }
    };

    /**
     * @struct TTEntry
     * @brief Transposition Table entry representing a cached board position.
     */
    struct TTEntry {
        Board key;          ///< Zobrist hash key to handle collisions
        Move  best_move;    ///< Best move found in this position
        int   score;        ///< Evaluated score for the position (from the perspective of the side to move)
        short depth;        ///< Search depth at which the score was computed
        std::uint8_t flag;  ///< TT_flags (EXACT, ALPHA, or BETA)
        short phase;        ///< Game phase for evaluation scaling (0-24)
    };
    
    /**
     * @class Position
     * @brief Manages board state, move execution, and evaluation.
    */
    class Position {
    private:
        // --- Board State ---
        Board boards[PIECE_NB];                      ///< Bitboards for each specific piece type
        Board pieces[COLOR_NB];                      ///< Aggregate bitboards for White and Black pieces
        Board occupancy;                             ///< Bitboard of all pieces on the board (union of pieces[WHITE] and pieces[BLACK])
        Color side_to_move;
        std::uint8_t castling_rights;                ///< 4-bit mask for castling avalability (KQkq)
        std::uint8_t castling_mask[CELL_NB] = {
            (std::uint8_t)~0x04, 255, 255, 255, (std::uint8_t)~(0x02 | 0x04), 255, 255, (std::uint8_t)~0x02,  // rank 1
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            255, 255, 255, 255, 255, 255, 255, 255,
            (std::uint8_t)~0x10, 255, 255, 255, (std::uint8_t)~(0x08 | 0x10), 255, 255, (std::uint8_t)~0x08  // rank 8
        };
        Square en_passant_square;                    ///< Current en passant target square (64 if none)
        Piece piece_on_square[CELL_NB];              ///< Mailbox board representation (Array of 64)
        
        // --- Search State ---
        UndoInfo undo_history[Constants::MAX_GAME_PLIES]; ///< Irreversible state history stack for unmake_move()
        Square game_ply = 0;                         ///< Current search depth from root
        Square king_square[COLOR_NB];                ///< Cached king positions for quick check-detection and move generation

        // --- Evaluation & Hash ---
        double mg_score, eg_score;                    ///< Incrementally updated Midgame and Endgame scores for the current position
        Board internal_hash;                         ///< Incrementally updated Zobrist hash of the current position for transposition table lookups
        short current_phase;                         ///< Tapered evaluation phase (0 to 24) based on remaining material

        public:
            /// @brief Initializes an empty board.
            Position() {}
            
            /**
             * @brief Initializes the board from a given FEN string.
             * @param fen Forsyth-Edwards Notation string representing the position.
             */
            Position(const std::string& fen) {
                parse_FEN(fen);
            }

        private:
        /// @name State Updaters
        /// @{

        /**
         * @brief Removes a piece from the board and updates incremental evaluations.
         */
        inline void remove_piece(const Square cell, const Color color) noexcept {
            Piece removed_piece = piece_on_square[cell];
            mg_score -= weight[removed_piece] + PST_Midgame[removed_piece][cell];
            eg_score -= weight[removed_piece] + PST_Endgame[removed_piece][cell];
            switch (removed_piece) {
                case WHITE_QUEEN:  current_phase -= 4; break;
                case BLACK_QUEEN:  current_phase -= 4; break;
                case WHITE_ROOK:   current_phase -= 2; break;
                case BLACK_ROOK:   current_phase -= 2; break;
                case WHITE_BISHOP: current_phase--;    break;
                case BLACK_BISHOP: current_phase--;    break;
                case WHITE_KNIGHT: current_phase--;    break;
                case BLACK_KNIGHT: current_phase--;    break;
                default: break;
            }
            internal_hash ^= atlas.z_pieces[removed_piece][cell];
            Board mask = (1ULL << cell);
            boards[removed_piece] ^= mask;
            pieces[color]         ^= mask;
            occupancy             ^= mask;
            piece_on_square[cell] = EMPTY;
        }
        
        /**
         * @brief Places a piece on the board and updates incremental evaluations.
         */
        inline void put_piece(const Square cell, const Piece piece, const Color color) noexcept {
            mg_score += weight[piece] + PST_Midgame[piece][cell];
            eg_score += weight[piece] + PST_Endgame[piece][cell];
            switch (piece) {
                case WHITE_QUEEN:  current_phase += 4; break;
                case BLACK_QUEEN:  current_phase += 4; break;
                case WHITE_ROOK:   current_phase += 2; break;
                case BLACK_ROOK:   current_phase += 2; break;
                case WHITE_BISHOP: current_phase++;    break;
                case BLACK_BISHOP: current_phase++;    break;
                case WHITE_KNIGHT: current_phase++;    break;
                case BLACK_KNIGHT: current_phase++;    break;
                default: break;
            }
            internal_hash ^= atlas.z_pieces[piece][cell];
            Board mask = (1ULL << cell); 
            boards[piece] ^= mask;
            pieces[color] ^= mask;
            occupancy     ^= mask;
            piece_on_square[cell] = piece;
        }
        
        /**
         * @brief Moves a piece from one square to another (Quiet moves).
         */
        inline void move_piece(const Square from, const Square to) noexcept {
            Piece moving_piece = piece_on_square[from];
            mg_score += PST_Midgame[moving_piece][to] - PST_Midgame[moving_piece][from];
            eg_score += PST_Endgame[moving_piece][to] - PST_Endgame[moving_piece][from];
            internal_hash ^= atlas.z_pieces[moving_piece][from] ^ atlas.z_pieces[moving_piece][to];
            Board move_mask = (1ULL << from) | (1ULL << to);
            boards[moving_piece] ^= move_mask;
            occupancy ^= move_mask;
            pieces[side_to_move] ^= move_mask;
            piece_on_square[from] = EMPTY;
            piece_on_square[to] = moving_piece;
        }
        
        /**
         * @brief Executes a capture, removing the victim and placing the attacker.
         */
        inline void capture(const Square from, const Square to) {
            Piece from_piece = piece_on_square[from];
            Piece to_piece = piece_on_square[to];
            Board from_mask = (1ULL << from);
            Board to_mask = (1ULL << to);
            Board move_mask = from_mask | to_mask;
            Color piece_color = side_to_move;

            mg_score += PST_Midgame[from_piece][to] - PST_Midgame[from_piece][from] - PST_Midgame[to_piece][to] - weight[to_piece];
            eg_score += PST_Endgame[from_piece][to] - PST_Endgame[from_piece][from] - PST_Endgame[to_piece][to] - weight[to_piece];
            switch (to_piece) {
                case WHITE_QUEEN:  current_phase -= 4; break;
                case BLACK_QUEEN:  current_phase -= 4; break;
                case WHITE_ROOK:   current_phase -= 2; break;
                case BLACK_ROOK:   current_phase -= 2; break;
                case WHITE_BISHOP: current_phase--;    break;
                case BLACK_BISHOP: current_phase--;    break;
                case WHITE_KNIGHT: current_phase--;    break;
                case BLACK_KNIGHT: current_phase--;    break;
                default: break;
            }
            internal_hash ^= atlas.z_pieces[from_piece][from] ^ atlas.z_pieces[from_piece][to] ^ atlas.z_pieces[to_piece][to];
            boards[from_piece] ^= move_mask; 
            pieces[piece_color] ^= move_mask;
            boards[to_piece] ^= to_mask;
            pieces[(piece_color ^ 1ULL)] ^= to_mask;
            occupancy ^= from_mask;
            piece_on_square[from] = EMPTY;
            piece_on_square[to] = from_piece;
        }

        /**
         * @brief Shifts a bitboard forward by one rank relative to the current side to move.
         */
        inline Board shift_forward(Board b) const noexcept {
            return (side_to_move == WHITE) ? (b << 8) : (b >> 8);
        }

        /**
         * @brief Handles pawn move generation, automatically generating multiple underpromotions if needed.
         * @param list The move list to append the generated moves to.
         * @param from Origin square.
         * @param to Destination square.
         * @param flag Move flag (CAPTURE or SILENT_MOVE).
         */
        inline void add_pawn_move(MoveList& list, Square from, Square to, std::uint8_t flag) noexcept {
            if (to >= 56 || to <= 7) {
                if (flag == CAPTURE) {
                        Piece from_piece = piece_on_square[from];
                        Piece to_piece   = piece_on_square[to];
                        short victim_value   = std::abs(weight[to_piece]);
                        short attacker_value = std::abs(weight[from_piece]);
                        short move_score     = 10000 + (victim_value * 10) - (attacker_value / 10);
                        list.add(encode_move(from, to, QUEEN_PROMOTION_AND_CAPTURE), 20000 + move_score + weight[WHITE_QUEEN]);
                        list.add(encode_move(from, to, KNIGHT_PROMOTION_AND_CAPTURE), 15000 + move_score + weight[WHITE_KNIGHT] * 2);
                        list.add(encode_move(from, to, ROOK_PROMOTION_AND_CAPTURE), 15000 + move_score + weight[WHITE_ROOK]);
                        list.add(encode_move(from, to, BISHOP_PROMOTION_AND_CAPTURE), 15000 + move_score + weight[WHITE_BISHOP]);
                } else {
                        list.add(encode_move(from, to, QUEEN_PROMOTION), 20000 + weight[WHITE_QUEEN]);
                        list.add(encode_move(from, to, KNIGHT_PROMOTION), 15000 + weight[WHITE_KNIGHT] * 2);
                        list.add(encode_move(from, to, ROOK_PROMOTION), 15000 + weight[WHITE_ROOK]);
                        list.add(encode_move(from, to, BISHOP_PROMOTION), 15000 + weight[WHITE_BISHOP]);
                }
            } else {
                Piece from_piece = piece_on_square[from];
                Piece to_piece   = piece_on_square[to];
                short victim_value   = std::abs(weight[to_piece]);
                short attacker_value = std::abs(weight[from_piece]);
                short move_score     = 0;
                if (flag == CAPTURE) {
                    move_score = 10000 + victim_value * 10 - attacker_value / 10;
                } else {
                    double pst_delta = (current_phase * (PST_Midgame[from_piece][to] - PST_Midgame[from_piece][from]) + (24 - current_phase) * (PST_Endgame[from_piece][to] - PST_Endgame[from_piece][from])) / 24;
                    move_score = static_cast<short>((from_piece >= BLACK_PAWN ? -pst_delta : pst_delta));
                }
                list.add(encode_move(from, to, flag), move_score);
            }
        }

        /// @}

        /// @name Move Generation Sub-routines
        /// @{

        inline void generate_captures(const Square from, MoveList& list, Board board) {
            while (board) {
                Square to = Math::countr_zero(board);
                Piece from_piece = piece_on_square[from];
                Piece to_piece   = piece_on_square[to];
                short victim_value   = std::abs(weight[to_piece]);
                short attacker_value = std::abs(weight[from_piece]);
                short move_score     = 10000 + (victim_value * 10) - (attacker_value / 10);
                list.add(encode_move(from, to, CAPTURE), move_score);
                board &= (board - 1);
            }
        }

        inline void generate_silents(const Square from, MoveList& list, Board board) {
            while (board) {
                Square to = Math::countr_zero(board);
                Piece piece = piece_on_square[from];
                double pst_delta = (current_phase * (PST_Midgame[piece][to] - PST_Midgame[piece][from]) + (24 - current_phase) * (PST_Endgame[piece][to] - PST_Endgame[piece][from])) / 24;
                short move_score = static_cast<short>(piece >= BLACK_PAWN ? -pst_delta : pst_delta);
                list.add(encode_move(from, to, SILENT_MOVE), move_score);
                board &= (board - 1);
            }
        }
        
        inline void generate_knight_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board knight_board = boards[WHITE_KNIGHT + offset];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;

            while (knight_board) {
                Square from = Math::countr_zero(knight_board);
                Board attacks = atlas.get_knight_attacks(from);
                if (isCapturing) generate_captures(from, list, attacks & enemy);
                else generate_silents(from, list, attacks & empty);
                knight_board &= (knight_board - 1);
            }
        }

        inline void generate_rook_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board rook_board = boards[WHITE_ROOK + offset];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;

            while (rook_board) {
                Square from = Math::countr_zero(rook_board);
                Board attacks = atlas.get_rook_attacks(from, occupancy);
                if (isCapturing) generate_captures(from, list, attacks & enemy);
                else generate_silents(from, list, attacks & empty);
                rook_board &= (rook_board - 1);
            }
        }

        inline void generate_bishop_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board bishop_board = boards[WHITE_BISHOP + offset];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;

            while (bishop_board) {
                Square from = Math::countr_zero(bishop_board);
                Board attacks = atlas.get_bishop_attacks(from, occupancy);
                if (isCapturing) generate_captures(from, list, attacks & enemy);
                else generate_silents(from, list, attacks & empty);
                bishop_board &= (bishop_board - 1);
            }
        }

        inline void generate_queen_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board queen_board = boards[WHITE_QUEEN + offset];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;

            while (queen_board) {
                Square from = Math::countr_zero(queen_board);
                Board attacks = atlas.get_queen_attacks(from, occupancy);
                if (isCapturing) generate_captures(from, list, attacks & enemy);
                else generate_silents(from, list, attacks & empty);
                queen_board &= (queen_board - 1);
            }
        }

        inline void generate_king_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board king_board = boards[WHITE_KING + offset];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;

            Square from = Math::countr_zero(king_board);
            Board attacks = atlas.get_king_attacks(from);

            if (isCapturing) {
                generate_captures(from, list, attacks & enemy);
            } else {
                offset = (side_to_move == WHITE) ? 0 : 56;
                const Board long_path = (side_to_move == WHITE) ? (1ULL << 1) | (1ULL << 2) | (1ULL << 3) : (1ULL << 57) | (1ULL << 58) | (1ULL << 59);
                const Board short_path = (side_to_move == WHITE) ? (1ULL << 5) | (1ULL << 6) : (1ULL << 61) | (1ULL << 62);
                if ((castling_rights & (side_to_move == WHITE ? 0x02 : 0x08)) && ((empty & short_path) == short_path)) {
                    if (!(is_square_attacked(4 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(5 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(6 + offset, Color(side_to_move ^ 1ULL)))) {
                        list.add(encode_move(from, 6 + offset, CASTLING), 0);
                    }
                }
                if ((castling_rights & (side_to_move == WHITE ? 0x04 : 0x10)) && ((empty & long_path) == long_path)) {
                    if (!(is_square_attacked(2 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(3 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(4 + offset, Color(side_to_move ^ 1ULL)))) {
                        list.add(encode_move(from, 2 + offset, CASTLING), 0);
                    }
                }
                generate_silents(from, list, attacks & empty);
            } 
        }

        inline void generate_pawn_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Board pawn_board = (side_to_move == WHITE) ? boards[WHITE_PAWN] : boards[BLACK_PAWN];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;
            short direction = (side_to_move == WHITE) ? 8 : -8;

            while (pawn_board) {
                Square from = Math::countr_zero(pawn_board);
                Board attacks = atlas.get_pawn_attacks(side_to_move, from);

                if (isCapturing) {
                    Board captures = attacks & enemy;
                    while (captures) {
                        Square to = Math::countr_zero(captures);
                        add_pawn_move(list, from, to, CAPTURE);             
                        captures &= (captures - 1);
                    }
                    if (en_passant_square != 64) {
                        Board en_passant = attacks & (1ULL << en_passant_square);
                        if (en_passant) {
                            Piece from_piece = piece_on_square[from];
                            Piece to_piece   = piece_on_square[(en_passant_square ^ 8)];
                            short victim_value   = std::abs(weight[to_piece]);
                            short attacker_value = std::abs(weight[from_piece]);
                            short move_score     = 10000 + victim_value * 10 - attacker_value / 10;
                            list.add(encode_move(from, en_passant_square, EN_PASSANT), move_score);
                        }
                    }
                } else {
                    Board silents = shift_forward(1ULL << from) & empty;
                    Square offset = (side_to_move == WHITE) ? 0 : 40;
                    while (silents) {
                        Square to = from + direction;
                        if (from >= (8 + offset) && from < (16 + offset)) {
                            Board double_jump = shift_forward(1ULL << to) & empty;
                            if (double_jump) {
                                list.add(encode_move(from, to + direction, PAWN_DOUBLE_JUMP), 0);
                            }
                        }
                        add_pawn_move(list, from, to, SILENT_MOVE);
                        silents &= (silents - 1);
                    }
                }
                pawn_board &= (pawn_board - 1);
            }
        }
        /// @}

    public:
        /// @name Move Validation & Execution
        /// @{

        /**
         * @brief Determines if a specific square is attacked by a given color.
         * @param cell The square to check.
         * @param attacker_color The side that might be attacking.
         * @return True if at least one piece of attacker_color attacks the cell.
         */
        inline bool is_square_attacked(const Square cell, const Color attacker_color) noexcept {
            Board rook_attacks = atlas.get_rook_attacks(cell, occupancy);
            Board knight_attacks = atlas.get_knight_attacks(cell);
            Board bishop_attacks = atlas.get_bishop_attacks(cell, occupancy);
            Board king_attacks = atlas.get_king_attacks(cell);
            Board pawn_attacks = atlas.get_pawn_attacks(Color(attacker_color ^ 1), cell);
            Square change_color = (attacker_color == WHITE) ? 0 : 6;
                if ((rook_attacks & boards[WHITE_ROOK + change_color]) || (rook_attacks & boards[WHITE_QUEEN + change_color]) 
                || (bishop_attacks & boards[WHITE_BISHOP + change_color]) || (bishop_attacks & boards[WHITE_QUEEN + change_color]) 
                || (knight_attacks & boards[WHITE_KNIGHT + change_color]) || (pawn_attacks & boards[WHITE_PAWN + change_color]) 
                || (king_attacks & boards[WHITE_KING + change_color])) {
                    return true;
                }
            return false;
        }
        
        /**
         * @brief Executes a move on the internal board state.
         * Saves irreversible state to history and updates incremental values.
         * @param move The encoded move to execute.
         * @return False if the move is pseudo-legal but leaves the king in check (invalid).
         */
        inline bool make_move(const Move move) noexcept {
            const Square from = get_from(move);
            const Square to = get_to(move);
            const std::uint8_t flag = get_flag(move);
            const Piece moving_piece = piece_on_square[from];

            undo_history[game_ply].castling_rights = castling_rights;
            undo_history[game_ply].en_passant_square = en_passant_square;
            undo_history[game_ply].captured_piece = piece_on_square[to];

            en_passant_square = 64;

            switch (flag) {
                case SILENT_MOVE: move_piece(from, to); break;
                case PAWN_DOUBLE_JUMP:
                    move_piece(from, to);
                    en_passant_square = (side_to_move == WHITE) ? (from + 8) : (from - 8);
                    break;
                case CASTLING: {
                        Square rook_from = (to == 62) ? 63 : (to == 58) ? 56 : (to == 6) ? 7 : 0;
                        Square rook_to = (to == 62) ? 61 : (to == 58) ? 59 : (to == 6) ? 5 : 3;
                        move_piece(from, to);
                        move_piece(rook_from, rook_to);
                        break;
                    }
                case EN_PASSANT:
                    undo_history[game_ply].captured_piece = (side_to_move == WHITE) ? BLACK_PAWN : WHITE_PAWN;
                    remove_piece((to ^ 8ULL), Color(side_to_move ^ 1ULL));
                    move_piece(from, to);
                    break;
                case CAPTURE: capture(from, to); break;
                case KNIGHT_PROMOTION:
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_KNIGHT : BLACK_KNIGHT), side_to_move);
                    break;
                case BISHOP_PROMOTION:
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_BISHOP : BLACK_BISHOP), side_to_move);
                    break;
                case ROOK_PROMOTION:
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_ROOK : BLACK_ROOK), side_to_move);
                    break;
                case QUEEN_PROMOTION:
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_QUEEN : BLACK_QUEEN), side_to_move);
                    break;
                case KNIGHT_PROMOTION_AND_CAPTURE:
                    remove_piece(to, Color(side_to_move ^ 1ULL));
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_KNIGHT : BLACK_KNIGHT), side_to_move);
                    break;
                case BISHOP_PROMOTION_AND_CAPTURE:
                    remove_piece(to, Color(side_to_move ^ 1ULL));
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_BISHOP : BLACK_BISHOP), side_to_move);
                    break;
                case ROOK_PROMOTION_AND_CAPTURE:
                    remove_piece(to, Color(side_to_move ^ 1ULL));
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_ROOK : BLACK_ROOK), side_to_move);
                    break;
                case QUEEN_PROMOTION_AND_CAPTURE:
                    remove_piece(to, Color(side_to_move ^ 1ULL));
                    remove_piece(from, side_to_move);
                    put_piece(to, (side_to_move == WHITE ? WHITE_QUEEN : BLACK_QUEEN), side_to_move);
                    break;
            }

            Square offset = (side_to_move == WHITE) ? 0 : 6;
            king_square[side_to_move] = (moving_piece == (WHITE_KING + offset)) ? to : king_square[side_to_move];

            if (undo_history[game_ply].en_passant_square != 64) internal_hash ^= atlas.z_ep[undo_history[game_ply].en_passant_square & 7];
            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square & 7];
            castling_rights &= (castling_mask[from] & castling_mask[to]);
            internal_hash ^= atlas.z_castling[castling_rights] ^ atlas.z_castling[undo_history[game_ply].castling_rights];
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;
            game_ply++;

            if (is_square_attacked(king_square[Color(side_to_move ^ 1ULL)], side_to_move)) {
                unmake_move(move);
                return false;
            }

            return true;
        }

        /**
         * @brief Restores the board state to the exact moment before the last make_move().
         */
        inline void unmake_move(const Move move) noexcept {
            game_ply--;

            const Square from = get_from(move);
            const Square to = get_to(move);
            const std::uint8_t flag = get_flag(move);
            const Piece moving_piece = piece_on_square[to];

            internal_hash ^= atlas.z_side;
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_castling[castling_rights] ^ atlas.z_castling[undo_history[game_ply].castling_rights];
            castling_rights = undo_history[game_ply].castling_rights;

            const Square offset = (side_to_move == WHITE) ? 0 : 6;
            king_square[side_to_move] = (moving_piece == (WHITE_KING + offset)) ? from : king_square[side_to_move];

            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square % 8];
            en_passant_square = undo_history[game_ply].en_passant_square;
            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square % 8];

            switch (flag)
            {
            case CAPTURE:
                move_piece(to, from);
                put_piece(to, undo_history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));
                break;
            case EN_PASSANT:
                move_piece(to, from);
                put_piece((to ^ 8), (side_to_move == WHITE ? BLACK_PAWN : WHITE_PAWN), Color(side_to_move ^ 1ULL));
                break;
            case CASTLING: {
                    Square rook_from = (to == 62) ? 63 : (to == 58) ? 56 : (to == 6) ? 7 : 0;
                    Square rook_to = (to == 62) ? 61 : (to == 58) ? 59 : (to == 6) ? 5 : 3;
                    move_piece(rook_to, rook_from);
                    move_piece(to, from);
                    break;
                }
            case KNIGHT_PROMOTION:
            case BISHOP_PROMOTION:
            case ROOK_PROMOTION:
            case QUEEN_PROMOTION:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);
                break;
            case KNIGHT_PROMOTION_AND_CAPTURE:
            case BISHOP_PROMOTION_AND_CAPTURE:
            case ROOK_PROMOTION_AND_CAPTURE:
            case QUEEN_PROMOTION_AND_CAPTURE:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);
                put_piece(to, undo_history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));
                break;
            default:
                move_piece(to, from);
                break;
            }
        }

        /**
         * @brief Executes a null move (passes the turn to the opponent).
         * Used for Null Move Pruning (NMP) during search. Saves state,
         * clears the en passant square, and updates the Zobrist hash symmetrically.
         */
        inline void make_null_move() noexcept {
            undo_history[game_ply].castling_rights = castling_rights;
            undo_history[game_ply].en_passant_square = en_passant_square;
            undo_history[game_ply].captured_piece = EMPTY;

            // If an en passant square was active, we MUST remove its Zobrist contribution
            if (en_passant_square != 64) {
                internal_hash ^= atlas.z_ep[en_passant_square & 7];
                en_passant_square = 64;
            }

            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;
            game_ply++;
        }

        /**
         * @brief Restores the board state to the moment before the last make_null_move().
         */
        inline void unmake_null_move() noexcept {
            game_ply--;
            
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;

            castling_rights = undo_history[game_ply].castling_rights;

            en_passant_square = undo_history[game_ply].en_passant_square;
            // Restore the Zobrist contribution of the en passant square if it was active
            if (en_passant_square != 64) {
                internal_hash ^= atlas.z_ep[en_passant_square & 7];
            }
        }
        /// @}

        /// @name Setup & Evaluation
        /// @{

        /**
         * @brief Recalculates material and PST scores from scratch.
         * Useful after loading a position from FEN.
         */
        inline void recalculate_evaluation() noexcept {
            mg_score = 0;
            eg_score = 0;
            current_phase = 0;
            
            for (int piece = WHITE_PAWN; piece <= BLACK_KING; piece++) {
                Board piece_bb = boards[piece];
                while (piece_bb) {
                    Square cell = Math::countr_zero(piece_bb);
                    mg_score += weight[piece] + PST_Midgame[piece][cell];
                    eg_score += weight[piece] + PST_Endgame[piece][cell];
                    
                    switch (piece) {
                        case WHITE_QUEEN:  current_phase += 4; break;
                        case BLACK_QUEEN:  current_phase += 4; break;
                        case WHITE_ROOK:   current_phase += 2; break;
                        case BLACK_ROOK:   current_phase += 2; break;
                        case WHITE_BISHOP: current_phase++;    break;
                        case BLACK_BISHOP: current_phase++;    break;
                        case WHITE_KNIGHT: current_phase++;    break;
                        case BLACK_KNIGHT: current_phase++;    break;
                        default: break;
                    }
                    
                    piece_bb &= piece_bb - 1; 
                }
            }
        }
        
        /**
         * @brief Parses a Forsyth-Edwards Notation (FEN) string to initialize the board.
         */
        inline void parse_FEN(const std::string& fen) {
            for (Square i = 0; i < CELL_NB; i++) piece_on_square[i] = EMPTY;
            for (Square i = 0; i < PIECE_NB; i++) boards[i] = 0ULL;
            pieces[WHITE] = 0ULL; 
            pieces[BLACK] = 0ULL;
            occupancy = 0ULL; 
            castling_rights = 0LL;
            mg_score = 0;
            eg_score = 0;
            game_ply = 0;
            current_phase = 0;
            internal_hash = 0ULL;

            Square counter = 0;
            Square file = 0;
            int rank = 7;
            
            while(counter < fen.length() && fen[counter] != ' ') {
                if (fen[counter] == '/') {
                    rank--;
                    file = 65535;
                } else {
                    Square cell = rank * 8 + file;
                    if (cell >= 0 && cell < 64) {
                        switch (fen[counter]) {
                            case 'r': put_piece(cell, BLACK_ROOK, BLACK); break;
                            case 'n': put_piece(cell, BLACK_KNIGHT, BLACK); break;
                            case 'b': put_piece(cell, BLACK_BISHOP, BLACK); break;
                            case 'q': put_piece(cell, BLACK_QUEEN, BLACK); break;
                            case 'k': put_piece(cell, BLACK_KING, BLACK); king_square[BLACK] = cell; break;
                            case 'p': put_piece(cell, BLACK_PAWN, BLACK); break;
                            case 'R': put_piece(cell, WHITE_ROOK, WHITE); break;
                            case 'N': put_piece(cell, WHITE_KNIGHT, WHITE); break;
                            case 'B': put_piece(cell, WHITE_BISHOP, WHITE); break;
                            case 'Q': put_piece(cell, WHITE_QUEEN, WHITE); break;
                            case 'K': put_piece(cell, WHITE_KING, WHITE); king_square[WHITE] = cell; break;
                            case 'P': put_piece(cell, WHITE_PAWN, WHITE); break;
                            default:
                                if (fen[counter] >= '1' && fen[counter] <= '8') file += fen[counter] - '1' - 1;
                                break;
                        }
                    }
                }
                file++;
                counter++;
            }
            
            counter++;
            if (counter < fen.length()) {
                switch (fen[counter]) {
                    case 'w': side_to_move = WHITE; break;
                    case 'b': side_to_move = BLACK; break;
                    default: break;
                }
                if (side_to_move == BLACK) internal_hash ^= atlas.z_side;
            }
            
            counter += 2;
            while (counter < fen.length() && fen[counter] != ' ') {
                switch (fen[counter]) {
                    case 'K': castling_rights ^= (1ULL << 1); break;
                    case 'Q': castling_rights ^= (1ULL << 2); break;
                    case 'k': castling_rights ^= (1ULL << 3); break;
                    case 'q': castling_rights ^= (1ULL << 4); break;
                    default: break;
                }
                counter++;
            }
            internal_hash ^= atlas.z_castling[castling_rights];
            
            counter++;
            if (counter < fen.length()) {
                if (fen[counter] == '-') {
                    en_passant_square = 64;
                } else if (counter + 1 < fen.length()) {
                    file = fen[counter++] - 'a';
                    rank = fen[counter++] - '1';
                    en_passant_square = rank * 8 + file;
                    internal_hash ^= atlas.z_ep[file];
                }
            }   
            
            recalculate_evaluation();
        }

        /**
         * @brief Computes the static heuristic evaluation of the current position.
         * Takes into account material, PSTs, mobility, pawn structure, and center control.
         * @return Evaluation score relative to the side to move (positive is good for current player).
         */
        inline short evaluate() const noexcept {
            short phase = current_phase;
            if (phase > 24) phase = 24;
            short base_score = (mg_score * phase + eg_score * (24 - phase)) / 24;

            short bonus = 0;

            int white_mobility = 0;
            int black_mobility = 0;

            Board white_knights = boards[WHITE_KNIGHT];
            while (white_knights) {
                Square cell = Math::countr_zero(white_knights);
                white_mobility += Math::count_bits(atlas.get_knight_attacks(cell) & ~pieces[WHITE]);
                white_knights &= white_knights - 1;
            }

            Board black_knights = boards[BLACK_KNIGHT];
            while (black_knights) {
                Square cell = Math::countr_zero(black_knights);
                black_mobility += Math::count_bits(atlas.get_knight_attacks(cell) & ~pieces[BLACK]);
                black_knights &= black_knights - 1;
            }

            bonus += (white_mobility - black_mobility) * 5;

            for (int file = 0; file < 8; file++) {
                Board file_mask = 0x0101010101010101ULL << file;
                int white_pawns_on_file = Math::count_bits(boards[WHITE_PAWN] & file_mask);
                int black_pawns_on_file = Math::count_bits(boards[BLACK_PAWN] & file_mask);

                if (white_pawns_on_file > 1) bonus -= (white_pawns_on_file - 1) * 15;
                if (black_pawns_on_file > 1) bonus += (black_pawns_on_file - 1) * 15;
            }

            Board white_pawns = boards[WHITE_PAWN];
            while (white_pawns) {
                Square cell = Math::countr_zero(white_pawns);
                int rank = cell / 8;
                int file = cell % 8;

                Board front_mask = (cell < 56) ? (0xFFFFFFFFFFFFFFFFULL << (cell + 8)) : 0ULL;
                Board file_and_adjacent = 0x0101010101010101ULL << file;
                if (file > 0) file_and_adjacent |= 0x0101010101010101ULL << (file - 1);
                if (file < 7) file_and_adjacent |= 0x0101010101010101ULL << (file + 1);

                if (!(boards[BLACK_PAWN] & front_mask & file_and_adjacent)) {
                    bonus += (rank - 1) * 10 + 20;
                }

                white_pawns &= white_pawns - 1;
            }

            Board black_pawns = boards[BLACK_PAWN];
            while (black_pawns) {
                Square cell = Math::countr_zero(black_pawns);
                int rank = cell / 8;
                int file = cell % 8;

                Board front_mask = (cell > 7) ? (0xFFFFFFFFFFFFFFFFULL >> (64 - cell)) : 0ULL;
                Board file_and_adjacent = 0x0101010101010101ULL << file;
                if (file > 0) file_and_adjacent |= 0x0101010101010101ULL << (file - 1);
                if (file < 7) file_and_adjacent |= 0x0101010101010101ULL << (file + 1);

                if (!(boards[WHITE_PAWN] & front_mask & file_and_adjacent)) {
                    bonus -= (6 - rank) * 10 + 20;
                }

                black_pawns &= black_pawns - 1;
            }

            Board center_squares = (1ULL << 27) | (1ULL << 28) | (1ULL << 35) | (1ULL << 36);
            int white_center_control = Math::count_bits(pieces[WHITE] & center_squares);
            int black_center_control = Math::count_bits(pieces[BLACK] & center_squares);
            bonus += (white_center_control - black_center_control) * 10;

            short final_score = base_score + bonus;
            return (side_to_move == WHITE) ? final_score : -final_score;
        }
        /// @}

        inline MoveList generate_all_captures() noexcept {
            MoveList list;
            generate_queen_moves (list);
            generate_rook_moves  (list);
            generate_knight_moves(list);
            generate_bishop_moves(list);
            generate_pawn_moves  (list);
            generate_king_moves  (list);
            return list;
        }

        /**
         * @brief Generates all pseudo-legal moves in the position.
         */
        inline MoveList generate_all_moves() noexcept {
            MoveList list;
            generate_queen_moves (list);
            generate_rook_moves  (list);
            generate_knight_moves(list);
            generate_bishop_moves(list);
            generate_pawn_moves  (list);
            generate_king_moves  (list);
            generate_queen_moves (list, false);
            generate_rook_moves  (list, false);
            generate_knight_moves(list, false);
            generate_bishop_moves(list, false);
            generate_pawn_moves  (list, false);
            generate_king_moves  (list, false);
            return list;
        }
        /// @name Accessors
        /// @{

        inline Board get_hash() const noexcept { return internal_hash; }
        
        inline short get_phase() const noexcept { return current_phase; }
        
        /// @brief Gets the piece located on the given square.
        inline Piece get_piece(Square cell) const noexcept { return piece_on_square[cell]; }
        
        /// @brief Gets the color of the side currently to move.
        inline Color get_side_to_move() const noexcept { return side_to_move; }

        inline Square get_king_square(Color color) const noexcept { return king_square[color]; }

        /// @brief Gets the current game ply (distance from the search root).
        inline Square get_game_ply() const noexcept { return game_ply; }
        /// @}
    };

    /**
     * @class Search
     * @brief Manages the Alpha-Beta traversal, heuristic, and Time Management for the chess engine. 
     * Contains the main search loop, quiescence search, and move ordering logic.
     */
    class Search {
    private:
        // --- Search Control ---
        std::vector<TTEntry> transposition_table;              ///< Transposition Table for caching previously evaluated positions
        Move killer_moves[Constants::MAX_DEPTH][2];            ///< Heuristic: Moves that caused a beta cutoff at a given depth (2 per depth)
        int history[CELL_NB][CELL_NB];                         ///< Heuristic: Butterfly history table [from][to]
        
        bool abort_search;
        long long nodes_visited;
        std::chrono::steady_clock::time_point end_time_limit;
    
        /**
         * @brief Normalizes mate scores to be relative to the root node before saving to TT. 
         * This ensures that mate scores are consistent regardless of the depth at which they were found.
         */
        inline short value_to_tt(short value, short ply) const noexcept {
            if (value > Constants::VALUE_MATE - Constants::MAX_DEPTH) {
                return value + ply;
            } 
            if (value < -Constants::VALUE_MATE + Constants::MAX_DEPTH) {
                return value - ply;
            }
            return value;
        }

        /**
         * @brief Re-adjusts mate scores to be relative to the current search depth upon loading form TT. 
         * This is the inverse of value_to_tt and ensures that mate scores are interpreted correctly at different depths.
         */
        inline short value_from_tt(short value, short ply) const noexcept {
            if (value > Constants::VALUE_MATE - Constants::MAX_DEPTH) {
                return value - ply;
            } 
            if (value < -Constants::VALUE_MATE + Constants::MAX_DEPTH) {
                return value + ply;
            }
            return value;
        }

    public:
        /**
         * @brief Callback triggered periodically during the search.
         * Useful for UCI communication (e.g., sending "info depth...", checking GUI interrupts).
         */
        std::function<void(const Position&)> on_node_update = nullptr;
        
        Search() : transposition_table(Constants::TT_SIZE) {
            clear_heuristics();
        }

        void clear_heuristics() {
            for (int i = 0; i < Constants::MAX_DEPTH; i++) {
                killer_moves[i][0] = 0;
                killer_moves[i][1] = 0;
            }
            for (Square from = 0; from < CELL_NB; from++) {
                for (Square to = 0; to < CELL_NB; to++) {
                    history[from][to] = 0;
                }
            }
        }

        /**
         * @brief Initiates an Iterative Deepening search using Aspiration Windows to find the best move within a time limit.
         * @param pos Reference to the current position.
         * @param time_limit_ms Maximum allowed time for the search.
         * @param alpha Initial lower bound (typically -INF).
         * @param beta Initial upper bound (typically +INF).
         * @param out_score Reference to store the evaluation score of the best move.
         * @return The best move found.
         */
        Move get_best_move(Position& pos,std::chrono::milliseconds time_limit_ms, short alpha, short beta, short& out_score) {
            abort_search = false;
            nodes_visited = 0;
            short depth = 1;
            Move best_move = 0;
            Move current_depth_best = 0;
            end_time_limit = std::chrono::steady_clock::now() + time_limit_ms;

            // Clear killer moves for new search
            for (int i = 0; i < Constants::MAX_DEPTH; i++) {
                killer_moves[i][0] = 0;
                killer_moves[i][1] = 0;
            }

            short prev_score = 0;
            short aspiration_delta = 20; // Starts at ~1/5 of a pawn and will widen if needed

            while (!abort_search && depth <= Constants::MAX_DEPTH) {
                short temp_alpha = -Constants::VALUE_INFINITE;
                short temp_beta = Constants::VALUE_INFINITE;

                if (depth >= 5) {
                    temp_alpha = prev_score - aspiration_delta;
                    temp_beta = prev_score + aspiration_delta;
                }

                while (!abort_search) {
                    current_depth_best = find_best_move(pos, depth, temp_alpha, temp_beta, out_score);

                    if (abort_search) break;
                    if (out_score <= temp_alpha) {
                        if (temp_alpha <= -Constants::VALUE_INFINITE + aspiration_delta + 5) {
                            break;
                        }
                        // Fail-low: the true score is lower than our window, so we need to widen and re-search
                        temp_alpha = std::max(static_cast<short>(-Constants::VALUE_INFINITE), static_cast<short>(temp_alpha - aspiration_delta));
                        aspiration_delta += 25; // Increase the delta for the next iteration to widen the window more aggressively
                    } else if (out_score >= temp_beta) {
                        if (temp_beta >= Constants::VALUE_INFINITE - aspiration_delta - 5) {
                            break;
                        }
                        // Fail-high: the true score is higher than our window, so we need to widen and re-search
                        temp_beta = std::min(static_cast<short>(Constants::VALUE_INFINITE), static_cast<short>(temp_beta + aspiration_delta));
                        aspiration_delta += 25; // Increase the delta for the next iteration to widen the window more aggressively
                    } else {
                        // Success: Score is within the window. Record the results and advance depth.
                        if (current_depth_best != 0) {
                            best_move = current_depth_best;
                            prev_score = out_score;
                            aspiration_delta = std::max(15, aspiration_delta - 5); // Narrow down slightly the window for the next depth
                        }
                        depth++;
                        break;
                    }

                    if (!abort_search && current_depth_best != 0) {
                        best_move = current_depth_best;
                        prev_score = out_score;
                        aspiration_delta = std::max(15, aspiration_delta - 5); // Narrow down slightly the window for the next depth
                    }
                    depth++;
                }
            }
            return best_move;
        }

        /**
         * @brief Retrieves the Principal Variation (PV) line from the Transposition Table.
         */
        inline std::vector<Move> get_PV(Position pos, Square max_length) {
            std::vector<Move> pv;
            for (Square i = 0; i < max_length; i++) {
                Board index = pos.get_hash() & ((1 << 20) - 1);
                if (transposition_table[index].key == pos.get_hash() && transposition_table[index].best_move != 0) {
                    Move tt_move = transposition_table[index].best_move;
                    if (!pos.make_move(tt_move)) break;
                    pv.push_back(tt_move);
                } else {
                    break;
                }
            }
            return pv;
        }

        /**
         * @brief Immediately halts the ongoing search. 
         * Essential for the UCI "stop" command and exact time management.
         */
        void stop() noexcept {
            abort_search = true;
        }

    private:
        /// @name Search Algorithms
        /// @{

        inline Move find_best_move(Position& pos, short depth, short alpha, short beta, short& out_score) {
            Move best_move_found = 0;
            short best_score = -Constants::VALUE_INFINITE;
            MoveList moves = pos.generate_all_moves();

            Board hash   = pos.get_hash();
            Board index  = hash & ((1 << 20) - 1);
            
            Move tt_move = 0;
            if (transposition_table[index].key == hash) {
                tt_move = transposition_table[index].best_move;
            }

            score_moves(moves, tt_move, 0);

            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                if (!pos.make_move(moves.moves[i])) continue;

                // Root PV node search - we want the most accurate score here, so we search with a full window
                short score = -search(pos, depth - 1, -beta, -alpha, 1);
                pos.unmake_move(moves.moves[i]);

                if (abort_search) return best_move_found;
                if (score > best_score) {
                    best_score = score;
                    best_move_found = moves.moves[i];
                }
                if (score > alpha) {
                    alpha = score;
                }
            }

            out_score = best_score;

            if (best_move_found != 0) {
                transposition_table[index].key = hash;
                transposition_table[index].best_move = best_move_found;
                transposition_table[index].score = value_to_tt(best_score, 0);
                transposition_table[index].depth = depth;
                transposition_table[index].flag = EXACT;
                transposition_table[index].phase = pos.get_phase();
            }
            return best_move_found;
        }

        /**
         * @brief Core Alpha-Beta search updated with PVS, LMR, and Singular Extensions.
         * @param depth Remaining search depth.
         * @param alpha Lower bound score.
         * @param beta Upper bound score.
         * @param ply_from_root Distance from the root node.
         * @param excluded_move Skip this move (used by Singular Extensions).
         * @return The best score found in the sub-tree.
         */
        inline short search(Position& pos, short depth, short alpha, short beta, short ply_from_root, Move excluded_move = 0) noexcept {
            if (ply_from_root >= Constants::MAX_DEPTH - 1) {
                return pos.evaluate();
            }
            if (pos.get_game_ply() >= Constants::MAX_GAME_PLIES - 1) {
                return pos.evaluate();
            }
            
            // Check detection
            bool in_check = pos.is_square_attacked(pos.get_king_square(pos.get_side_to_move()), Color(pos.get_side_to_move() ^ 1ULL));
            
            // Check extension: if we are under check, we extend the search by +1 ply
            if (in_check) {
                depth++;
            } 

            if (depth <= 0) return quiescence(pos, alpha, beta);

            short temp = alpha;
            Board hash = pos.get_hash();
            Board index = hash & ((1 << 20) - 1);

            Move tt_move = 0;
            short tt_depth = 0;
            if (transposition_table[index].key == hash) {
                tt_move = transposition_table[index].best_move;
                tt_depth = transposition_table[index].depth;
                if (tt_depth >= depth && excluded_move == 0) {
                    short tt_score = value_from_tt(transposition_table[index].score, ply_from_root);
                    switch (transposition_table[index].flag) {
                        case EXACT: return tt_score;
                        case ALPHA: if (tt_score <= alpha) return tt_score; break;
                        case BETA: if (tt_score >= beta) return tt_score; break;
                    }
                }
            }

            // Null Move Pruning
            if (depth >= 3 && !in_check && ply_from_root > 0 && pos.get_phase() > 6 && excluded_move == 0) {
                short static_eval = pos.evaluate();
                if (static_eval >= beta) {
                    pos.make_null_move();

                    short null_score = -search(pos, depth - 3, -beta, -beta + 1, ply_from_root + 1);

                    pos.unmake_null_move();

                    if (abort_search) return 0;
                    if (null_score >= beta) {
                        return beta; // beta cutoff on null move, so we can skip searching this node further
                    }
                }
            }

            bool extended = false;
            if (depth >= 6 && tt_move != 0 && excluded_move == 0 && tt_depth >= depth - 3) {
                short tt_score = value_from_tt(transposition_table[index].score, ply_from_root);
                if (std::abs(tt_score) < Constants::VALUE_MATE - 100) {
                    short margin = 2 * depth; // Singular margin (~2 centipawns per depth)
                    short singular_beta = tt_score - margin;
                    
                    // Exclude the best move and see if any other move can reach its level
                    short singular_score = search(pos, depth - 3, singular_beta - 1, singular_beta, ply_from_root + 1, tt_move);
                    
                    if (singular_score < singular_beta) {
                        extended = true; // This move is vastly superior to all others, extend search!
                    }
                }
            }

            MoveList moves = pos.generate_all_moves();
            score_moves(moves, tt_move, ply_from_root);

            short best_score = -Constants::VALUE_INFINITE;
            Move best_move = 0;
            short legal_moves = 0;

            short next_depth = depth - 1 + (extended ? 1 : 0);

            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                Move current_move = moves.moves[i];
                
                // Skip the excluded move when verifying singularity
                if (current_move == excluded_move) continue;

                if (!pos.make_move(current_move)) continue;
                legal_moves++;

                short score;
                std::uint8_t flag = get_flag(current_move);
                bool is_capture = (flag == CAPTURE || flag >= KNIGHT_PROMOTION_AND_CAPTURE);

                if (legal_moves == 1) {
                    // 1. PV node: Search with full window
                    score = -search(pos, next_depth, -beta, -alpha, ply_from_root + 1);
                } else {
                    // 2. Non-PV nodes: Scout search with Null Window (PVS)
                    short reduction = 0;

                    // Late Move Reductions (LMR) integrated with PVS
                    if (i >= 4 && depth >= 3 && !is_capture && !in_check) {
                        reduction = 1;
                        if (i >= 12) reduction = 2;    
                    }

                    score = -search(pos, std::max(0, next_depth - reduction), -alpha - 1, -alpha, ply_from_root + 1);

                    // Re-search at full depth with null window if reduced search fails high
                    if (reduction > 0 && score > alpha) {
                        score = -search(pos, next_depth, -alpha - 1, -alpha, ply_from_root + 1);
                    }

                    if (score > alpha && score < beta) {
                        score = -search(pos, next_depth, -beta, -alpha, ply_from_root + 1);
                    }
                }

                pos.unmake_move(current_move);

                if (abort_search) return 0;
                check_time(pos);

                if (score > best_score) {
                    best_score = score;
                    best_move = current_move;
                }

                if (score >= beta) {
                    // Beta cutoff: update killer moves and history for quiet moves
                    if (!is_capture && ply_from_root < Constants::MAX_DEPTH) {
                        // Update killer moves
                        if (killer_moves[ply_from_root][0] != current_move) {
                            killer_moves[ply_from_root][1] = killer_moves[ply_from_root][0];
                            killer_moves[ply_from_root][0] = current_move;
                        }

                        // Update history heuristic
                        Square from = get_from(current_move);
                        Square to = get_to(current_move);
                        history[from][to] += depth * depth;
                    }
                    alpha = score;
                    break;
                }
                if (score > alpha) alpha = score;
            }
            if (legal_moves == 0) {
                if (excluded_move != 0) return alpha; // Excluded search, don't trigger mate
                if (in_check) return -Constants::VALUE_MATE + ply_from_root; // Checkmate
                return 0; // Stalemate
            }

            // Save search results to Transposition Table
            if (excluded_move == 0) {
                transposition_table[index].key = hash;
                transposition_table[index].best_move = best_move;
                transposition_table[index].score = value_to_tt(best_score, ply_from_root);
                transposition_table[index].depth = depth;
                transposition_table[index].flag = (best_score >= beta) ? BETA : ((best_score > temp) ? EXACT : ALPHA);
                transposition_table[index].phase = pos.get_phase();
            }

            return best_score;
        }

         /**
         * @brief Quiescence search extended to handle check evasions safely.
         */
        inline short quiescence(Position& pos, short alpha, short beta) {
            if (pos.get_game_ply() >= Constants::MAX_GAME_PLIES - 1) {
                return pos.evaluate();
            }

            bool in_check = pos.is_square_attacked(pos.get_king_square(pos.get_side_to_move()), Color(pos.get_side_to_move() ^ 1ULL));

            // Stand pat: if not in check, establish a lower bound score
            if (!in_check) {
                short stand_pat = pos.evaluate();
                if (stand_pat >= beta) return beta;
                if (stand_pat > alpha) alpha = stand_pat;
            }

            // If in check: we generate ALL legal moves (to evade check).
            // If not in check: we only generate captures to keep search quiet.
            MoveList moves = in_check ? pos.generate_all_moves() : pos.generate_all_captures();
            score_moves(moves, 0, 0); // Quick scoring

            short legal_moves = 0;
            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                if (!pos.make_move(moves.moves[i])) continue;
                legal_moves++;

                short score = -quiescence(pos, -beta, -alpha);
                pos.unmake_move(moves.moves[i]);

                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            }

            // If we are under check but have no legal moves, it is checkmate
            if (in_check && legal_moves == 0) {
                return -Constants::VALUE_MATE + pos.get_game_ply();
            }

            return alpha;
        }
        /// @}

        /// @name Search Utilities
        /// @{

        /**
         * @brief Assigns heuristic scores to generated moves to optimize alpha-beta pruning (Move Ordering).
         * Prioritizes the Transposition Table move, followed by Killer moves, and the History heuristic.
         * @param list The move list to score.
         * @param tt_move The best move retrieved from the Transposition Table (if any).
         * @param ply The current distance from the root (used for matching killer moves).
         */
        inline void score_moves(MoveList& list, Move tt_move, short ply) noexcept {
            for (Square i = 0; i < list.count; i++) {
                Move move = list.moves[i];

                // TT move gets highest priority
                if (move == tt_move) {
                    list.scores[i] = Constants::SCORE_TT;
                    continue;
                }

                // Killer moves get high priority (only for non-captures)
                std::uint8_t flag = get_flag(move);
                bool is_capture = (flag == CAPTURE || flag == EN_PASSANT || flag >= KNIGHT_PROMOTION_AND_CAPTURE);

                if (!is_capture) {
                    if (move == killer_moves[ply][0]) {
                        list.scores[i] += Constants::SCORE_KILLER_1;
                    } else if (move == killer_moves[ply][1]) {
                        list.scores[i] += Constants::SCORE_KILLER_2;
                    } else {
                        // History heuristic for quiet moves
                        Square from = get_from(move);
                        Square to = get_to(move);
                        list.scores[i] += history[from][to] / 100;
                    }
                }
            }
        }

        /**
         * @brief Partially sorts the move list by extracting the best remaining move to the current_index.
         * Implements a lazy Selection Sort (one element per call) to save time if a cutoff occurs early.
         * @param list The move list.
         * @param current_index The current iteration step in the move loop.
         */
        inline void pick_move(MoveList& list, Square current_index) noexcept {
            short best_score = list.scores[current_index];
            Square best_index = current_index;
            for (Square i = current_index + 1; i < list.count; i++) {
                if (list.scores[i] > best_score) {
                    best_score = list.scores[i];
                    best_index = i;
                }
            }
            std::swap(list.moves[current_index], list.moves[best_index]);
            std::swap(list.scores[current_index], list.scores[best_index]);
        }

        /**
         * @brief Interleaves time-checking logic to avoid heavy std::chrono calls on every node.
         * Checks the clock only once every 2048 nodes.
         */
        inline void check_time(const Position& pos) noexcept {
            nodes_visited++;
            if ((nodes_visited & 2047) == 0) {
                if (std::chrono::steady_clock::now() >= end_time_limit) {
                    abort_search = true;
                }
                if (on_node_update) on_node_update(pos);
            }
        }

        /// @}
    };
}