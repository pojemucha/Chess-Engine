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
#include <cassert>
#include <atomic>

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

        constexpr size_t TT_SIZE = 1 << 22;     ///< ~4.1 Million entries (~96MB) for the Transposition Table

        constexpr int PIECE_VALUES[14] = {100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000, 0, 0};
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
            if (b == 0) return 64;
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
        std::uint16_t halfmove_clock; ///< Halfmove clock before the move
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
        Board boards[PIECE_NB];                      ///< Bitboards representing the locations of all pieces, indexed by piece type.
        Board pieces[COLOR_NB];                      ///< Aggregate bitboards for all White (index 0) and Black (index 1) pieces.
        Board occupancy;                             ///< Bitboard representing all occupied squares on the board.
        Color side_to_move;
        std::uint8_t castling_rights;                ///< 4-bit mask representing current castling availability (Format: KQkq).
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
        Piece piece_on_square[CELL_NB];              ///< Mailbox (array-based) representation of the board for fast square lookups.
        
        // --- Search State ---
        UndoInfo undo_history[Constants::MAX_GAME_PLIES]; ///< Irreversible state history stack for unmake_move()
        Square game_ply = 0;                         ///< Current search depth from root
        Square king_square[COLOR_NB];                ///< Cached king positions for quick check-detection and move generation

        // --- Evaluation & Hash ---
        int mg_score, eg_score;                      ///< Incrementally updated Midgame (mg) and Endgame (eg) evaluation scores. Kept up-to-date during make_move/unmake_move to avoid full recalculations.
        Board internal_hash;                         ///< Incrementally updated Zobrist hash key for Transposition Table lookups.
        short current_phase;                         ///< Tapered evaluation phase counter (0 to 24). 24 represents the opening/midgame (full material), 0 represents a pure pawn endgame.
        std::uint16_t halfmove_clock = 0;            ///< Halfmove clock for the fifty-move rule
        Board hash_history[Constants::MAX_GAME_PLIES];    ///< Hashes along the current line for repetition detection

        /**
         * @brief Resets the board to a fully empty, safe default state.
         */
        inline void clear_state() noexcept {
            for (Square i = 0; i < CELL_NB; i++) piece_on_square[i] = EMPTY;
            for (Square i = 0; i < PIECE_NB; i++) boards[i] = 0ULL;
            pieces[WHITE] = 0ULL;
            pieces[BLACK] = 0ULL;
            occupancy = 0ULL;
            side_to_move = WHITE;
            castling_rights = 0;
            en_passant_square = 64;
            game_ply = 0;
            king_square[WHITE] = 64;
            king_square[BLACK] = 64;
            mg_score = 0;
            eg_score = 0;
            internal_hash = 0ULL;
            current_phase = 0;
            halfmove_clock = 0;
        }

        public:
            /// @brief Initializes an empty board.
            Position() noexcept { clear_state(); }
            
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
            assert(removed_piece != EMPTY);
            assert(color == (removed_piece >= BLACK_PAWN ? BLACK : WHITE));
            mg_score -= int_weight[removed_piece] + int_PST_Midgame[removed_piece][cell];
            eg_score -= int_weight[removed_piece] + int_PST_Endgame[removed_piece][cell];
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
            boards[removed_piece] &= ~mask;
            pieces[color]         &= ~mask;
            occupancy             &= ~mask;
            piece_on_square[cell] = EMPTY;
        }
        
        /**
         * @brief Places a piece on the board and updates incremental evaluations.
         */
        inline void put_piece(const Square cell, const Piece piece, const Color color) noexcept {
            assert(piece_on_square[cell] == EMPTY);
            mg_score += int_weight[piece] + int_PST_Midgame[piece][cell];
            eg_score += int_weight[piece] + int_PST_Endgame[piece][cell];
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
            boards[piece] |= mask;
            pieces[color] |= mask;
            occupancy     |= mask;
            piece_on_square[cell] = piece;
        }
        
        /**
         * @brief Moves a piece from one square to another (Quiet moves).
         */
        inline void move_piece(const Square from, const Square to) noexcept {
            Piece moving_piece = piece_on_square[from];
            assert(moving_piece != EMPTY);
            assert(piece_on_square[to] == EMPTY);
            mg_score += int_PST_Midgame[moving_piece][to] - int_PST_Midgame[moving_piece][from];
            eg_score += int_PST_Endgame[moving_piece][to] - int_PST_Endgame[moving_piece][from];
            internal_hash ^= atlas.z_pieces[moving_piece][from] ^ atlas.z_pieces[moving_piece][to];
            Board from_mask = (1ULL << from);
            Board to_mask   = (1ULL << to);
            boards[moving_piece] &= ~from_mask;
            boards[moving_piece] |= to_mask;
            occupancy &= ~from_mask;
            occupancy |= to_mask;
            pieces[side_to_move] &= ~from_mask;
            pieces[side_to_move] |= to_mask;
            piece_on_square[from] = EMPTY;
            piece_on_square[to] = moving_piece;
        }
        
        /**
         * @brief Executes a capture, removing the victim and placing the attacker.
         */
        inline void capture(const Square from, const Square to) {
            Piece from_piece = piece_on_square[from];
            Piece to_piece = piece_on_square[to];
            assert(from_piece != EMPTY);
            assert(to_piece != EMPTY);
            Board from_mask = (1ULL << from);
            Board to_mask = (1ULL << to);
            Color piece_color = side_to_move;

            mg_score += int_PST_Midgame[from_piece][to] - int_PST_Midgame[from_piece][from] - int_PST_Midgame[to_piece][to] - int_weight[to_piece];
            eg_score += int_PST_Endgame[from_piece][to] - int_PST_Endgame[from_piece][from] - int_PST_Endgame[to_piece][to] - int_weight[to_piece];
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
            boards[to_piece] &= ~to_mask;
            boards[from_piece] &= ~from_mask;
            boards[from_piece] |= to_mask;
            pieces[piece_color] &= ~from_mask;
            pieces[piece_color] |= to_mask;
            pieces[(piece_color ^ 1ULL)] &= ~to_mask;
            occupancy &= ~from_mask;
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
            Piece from_piece = piece_on_square[from];
            
            if (to >= 56 || to <= 7) {
                if (flag == CAPTURE) {
                    Piece to_piece = piece_on_square[to];
                    int victim_val   = Constants::PIECE_VALUES[to_piece];
                    int attacker_val = Constants::PIECE_VALUES[from_piece];
                    int move_score = 2500000 + (victim_val * 10) - (attacker_val / 10);
                    
                    list.add(encode_move(from, to, QUEEN_PROMOTION_AND_CAPTURE), move_score + 1000000);
                    list.add(encode_move(from, to, KNIGHT_PROMOTION_AND_CAPTURE), move_score + 500000);
                    list.add(encode_move(from, to, ROOK_PROMOTION_AND_CAPTURE), move_score + 400000);
                    list.add(encode_move(from, to, BISHOP_PROMOTION_AND_CAPTURE), move_score + 300000);
                } else {
                    list.add(encode_move(from, to, QUEEN_PROMOTION), 1000000);
                    list.add(encode_move(from, to, KNIGHT_PROMOTION), 500000);
                    list.add(encode_move(from, to, ROOK_PROMOTION), 400000);
                    list.add(encode_move(from, to, BISHOP_PROMOTION), 300000);
                }
            } else {
                int move_score = 0;
                if (flag == CAPTURE) {
                    Piece to_piece = piece_on_square[to];
                    int victim_val   = Constants::PIECE_VALUES[to_piece];
                    int attacker_val = Constants::PIECE_VALUES[from_piece];
                    move_score = 2000000 + (victim_val * 10) - (attacker_val / 10);
                } else {
                    double pst_delta = (current_phase * (int_PST_Midgame[from_piece][to] - int_PST_Midgame[from_piece][from]) + (24 - current_phase) * (int_PST_Endgame[from_piece][to] - int_PST_Endgame[from_piece][from])) / 24;
                    move_score = static_cast<int>((from_piece >= BLACK_PAWN ? -pst_delta : pst_delta));
                }
                list.add(encode_move(from, to, flag), move_score);
            }
        }

        /// @}

        /// @name Move Generation Sub-routines
        /// @{

        /**
         * @brief Extracts and encodes all capture moves from a given attack bitboard.
         * @param from The origin square of the attacking piece.
         * @param list The move list to append generated captures to.
         * @param board The bitboard containing valid capture destinations.
         */
        inline void generate_captures(const Square from, MoveList& list, Board board) {
            while (board) {
                Square to = Math::countr_zero(board);
                Piece from_piece = piece_on_square[from];
                Piece to_piece   = piece_on_square[to];
                int victim_val   = Constants::PIECE_VALUES[to_piece];
                int attacker_val = Constants::PIECE_VALUES[from_piece];
                int move_score   = 2000000 + (victim_val * 10) - (attacker_val / 10);
                
                list.add(encode_move(from, to, CAPTURE), move_score);
                board &= (board - 1);
            }
        }
        
        /**
         * @brief Extracts and encodes all quiet (non-capturing) moves from a given bitboard.
         * @param from The origin square of the moving piece.
         * @param list The move list to append generated quiet moves to.
         * @param board The bitboard containing valid quiet destinations.
         */
        inline void generate_silents(const Square from, MoveList& list, Board board) {
            while (board) {
                Square to = Math::countr_zero(board);
                Piece piece = piece_on_square[from];
                double pst_delta = (current_phase * (int_PST_Midgame[piece][to] - int_PST_Midgame[piece][from]) + (24 - current_phase) * (int_PST_Endgame[piece][to] - int_PST_Endgame[piece][from])) / 24;
                short move_score = static_cast<short>(piece >= BLACK_PAWN ? -pst_delta : pst_delta);
                list.add(encode_move(from, to, SILENT_MOVE), move_score);
                board &= (board - 1);
            }
        }
        
        /**
         * @brief Generates pseudo-legal moves for all Knights of the current side to move.
         * @param list The move list to append generated moves to.
         * @param isCapturing If true, generates only captures; if false, generates only quiet moves.
         */
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

        /**
         * @brief Generates pseudo-legal moves for all Rooks of the current side to move.
         * Utilizes magic bitboards for sliding attack generation.
         * @param list The move list to populate.
         * @param isCapturing Flag to toggle between capture and quiet move generation.
         */
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

        /**
         * @brief Generates pseudo-legal moves for all Bishops of the current side to move.
         * Utilizes magic bitboards for sliding attack generation.
         * @param list The move list to populate.
         * @param isCapturing Flag to toggle between capture and quiet move generation.
         */
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

        /**
         * @brief Generates pseudo-legal moves for all Queens of the current side to move.
         * Utilizes magic bitboards for sliding attack generation.
         * @param list The move list to populate.
         * @param isCapturing Flag to toggle between capture and quiet move generation.
         */
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

        /**
         * @brief Generates pseudo-legal moves for the King of the current side to move.
         * Automaticly handles castling.
         * @param list The move list to append generated moves to.
         * @param isCapturing If true, generates only captures; if false, generates only quiet moves.
         */
        inline void generate_king_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board king_board = boards[WHITE_KING + offset];
            if (!king_board) return; // Safety check, should never happen in a valid position
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
                const Square rook_short_from = (side_to_move == WHITE) ? 7 : 63;
                const Square rook_long_from  = (side_to_move == WHITE) ? 0 : 56;
                const Piece rook_piece       = (side_to_move == WHITE) ? WHITE_ROOK : BLACK_ROOK;

                if ((castling_rights & (side_to_move == WHITE ? 0x02 : 0x08)) && 
                    piece_on_square[rook_short_from] == rook_piece && ((empty & short_path) == short_path)) {
                    if (!(is_square_attacked(4 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(5 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(6 + offset, Color(side_to_move ^ 1ULL)))) {
                        list.add(encode_move(from, 6 + offset, CASTLING), 0);
                    }
                }
                if ((castling_rights & (side_to_move == WHITE ? 0x04 : 0x10)) &&
                    piece_on_square[rook_long_from] == rook_piece && ((empty & long_path) == long_path)) {
                    if (!(is_square_attacked(2 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(3 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(4 + offset, Color(side_to_move ^ 1ULL)))) {
                        list.add(encode_move(from, 2 + offset, CASTLING), 0);
                    }
                }
                generate_silents(from, list, attacks & empty);
            } 
        }

        /**
         * @brief Generates all pseudo-legal pawn moves, including pushes, double jumps, captures, en passant, and promotions.
         * Automatically handles multiple promotion variants (Queen, Rook, Bishop, Knight) when reaching the final rank.
         * @param list The move list to populate.
         * @param isCapturing If true, generates captures and promotions; if false, generates pushes.
         */
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
                            int victim_val   = Constants::PIECE_VALUES[to_piece];
                            int attacker_val = Constants::PIECE_VALUES[from_piece];
                            int move_score   = 2000000 + (victim_val * 10) - (attacker_val / 10);
                            list.add(encode_move(from, en_passant_square, EN_PASSANT), move_score);
                        }
                    }
                    Board silents = shift_forward(1ULL << from) & empty;
                    if (silents) {
                        Square to = from + direction;
                        if (to >= 56 || to <= 7) {
                            add_pawn_move(list, from, to, SILENT_MOVE);
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
        inline bool is_square_attacked(const Square cell, const Color attacker_color) const noexcept {
            Square change_color = (attacker_color == WHITE) ? 0 : 6;

            Board pawn_attacks = atlas.get_pawn_attacks(Color(attacker_color ^ 1ULL), cell);
            if (pawn_attacks & boards[WHITE_PAWN + change_color]) return true;

            Board knight_attacks = atlas.get_knight_attacks(cell);
            if (knight_attacks & boards[WHITE_KNIGHT + change_color]) return true;

            Board king_attacks = atlas.get_king_attacks(cell);
            if (king_attacks & boards[WHITE_KING + change_color]) return true;

            Board bishop_attacks = atlas.get_bishop_attacks(cell, occupancy);
            if ((bishop_attacks & boards[WHITE_BISHOP + change_color]) || 
                (bishop_attacks & boards[WHITE_QUEEN + change_color])) {
                return true;
            }

            Board rook_attacks = atlas.get_rook_attacks(cell, occupancy);
            if ((rook_attacks & boards[WHITE_ROOK + change_color]) || 
                (rook_attacks & boards[WHITE_QUEEN + change_color])) {
                    return true;
                }
            
            return false;
        }

        /**
         * @brief Evaluates all potential attackers pointing at a specific square.
         * Useful for Static Exchange Evaluation (SEE).
         */
        inline Board attackers_to(Square cell, Board occ) const noexcept {
            Board attackers = 0;
            attackers |= atlas.get_pawn_attacks(WHITE, cell) & boards[BLACK_PAWN];
            attackers |= atlas.get_pawn_attacks(BLACK, cell) & boards[WHITE_PAWN];
            attackers |= atlas.get_knight_attacks(cell) & (boards[WHITE_KNIGHT] | boards[BLACK_KNIGHT]);
            attackers |= atlas.get_king_attacks(cell) & (boards[WHITE_KING] | boards[BLACK_KING]);
            
            Board rooks_queens = boards[WHITE_ROOK] | boards[BLACK_ROOK] | boards[WHITE_QUEEN] | boards[BLACK_QUEEN];
            Board bishops_queens = boards[WHITE_BISHOP] | boards[BLACK_BISHOP] | boards[WHITE_QUEEN] | boards[BLACK_QUEEN];
            
            attackers |= atlas.get_rook_attacks(cell, occ) & rooks_queens;
            attackers |= atlas.get_bishop_attacks(cell, occ) & bishops_queens;
            
            return attackers;
        }

        /**
         * @brief Simulates all captures on a single square to determine if a capture sequence is profitable.
         * Returns a value > 0 if the capture wins material, < 0 if it loses material.
         */
        inline int see_capture(Move move) const noexcept {
            Square from = get_from(move);
            Square to = get_to(move);
            Piece from_piece = piece_on_square[from];
            Piece to_piece = piece_on_square[to];
            std::uint8_t flag = get_flag(move);

            int gain[32];
            int d = 0;

            if (flag == EN_PASSANT) {
                gain[0] = Constants::PIECE_VALUES[WHITE_PAWN];
            } else {
                gain[0] = Constants::PIECE_VALUES[to_piece];
            }

            Piece attacker = from_piece;

            if (flag >= KNIGHT_PROMOTION) {
                int promo_val = Constants::PIECE_VALUES[WHITE_QUEEN];
                if (flag == KNIGHT_PROMOTION || flag == KNIGHT_PROMOTION_AND_CAPTURE) { 
                    promo_val = Constants::PIECE_VALUES[WHITE_KNIGHT]; 
                    attacker = (side_to_move == WHITE ? WHITE_KNIGHT : BLACK_KNIGHT); 
                } else if (flag == BISHOP_PROMOTION || flag == BISHOP_PROMOTION_AND_CAPTURE) { 
                    promo_val = Constants::PIECE_VALUES[WHITE_BISHOP]; 
                    attacker = (side_to_move == WHITE ? WHITE_BISHOP : BLACK_BISHOP); 
                } else if (flag == ROOK_PROMOTION || flag == ROOK_PROMOTION_AND_CAPTURE) { 
                    promo_val = Constants::PIECE_VALUES[WHITE_ROOK]; 
                    attacker = (side_to_move == WHITE ? WHITE_ROOK : BLACK_ROOK); 
                } else { 
                    promo_val = Constants::PIECE_VALUES[WHITE_QUEEN]; 
                    attacker = (side_to_move == WHITE ? WHITE_QUEEN : BLACK_QUEEN); 
                }
                
                gain[0] += promo_val - Constants::PIECE_VALUES[WHITE_PAWN];
            }

            Board occ = occupancy;
            occ ^= (1ULL << from); 
            if (flag == EN_PASSANT) {
                occ ^= (1ULL << (side_to_move == WHITE ? to - 8 : to + 8));
            }

            Color stm = Color(side_to_move ^ 1ULL);

            while (true) {
                d++;
                if (d >= 31) break; 
                
                gain[d] = Constants::PIECE_VALUES[attacker] - gain[d - 1];
                if (std::max(-gain[d-1], gain[d]) < 0) break;

                if (gain[d] < 0 && d > 1) break;

                Board attackers = attackers_to(to, occ) & occ & pieces[stm]; 
                if (!attackers) break;

                Square best_sq = 64;
                Piece best_piece = EMPTY;

                short offset = (stm == WHITE) ? 0 : 6;
                for (short p = 0; p < 6; p++) {
                    Board p_board = boards[p + offset] & attackers;
                    if (p_board) {
                        best_sq = Math::countr_zero(p_board);
                        best_piece = Piece(p + offset);
                        break; 
                    }
                }

                if (best_sq == 64) break; 

                occ ^= (1ULL << best_sq);
                attacker = best_piece;
                stm = Color(stm ^ 1ULL);
            }

            while (--d) {
                gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
            }
            return gain[0];
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
            if (moving_piece == EMPTY || (pieces[side_to_move] & (1ULL << from)) == 0) return false; // Invalid move sanity check

            undo_history[game_ply].castling_rights = castling_rights;
            undo_history[game_ply].en_passant_square = en_passant_square;
            undo_history[game_ply].captured_piece = piece_on_square[to];
            undo_history[game_ply].halfmove_clock = halfmove_clock;

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

            if (moving_piece == WHITE_PAWN || moving_piece == BLACK_PAWN || flag == CAPTURE || flag == EN_PASSANT ||
                flag == KNIGHT_PROMOTION_AND_CAPTURE || flag == BISHOP_PROMOTION_AND_CAPTURE || flag == ROOK_PROMOTION_AND_CAPTURE || flag == QUEEN_PROMOTION_AND_CAPTURE) {
                halfmove_clock = 0;
            } else {
                ++halfmove_clock;    
            }

            if (undo_history[game_ply].en_passant_square != 64) internal_hash ^= atlas.z_ep[undo_history[game_ply].en_passant_square & 7];
            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square & 7];
            castling_rights &= (castling_mask[from] & castling_mask[to]);
            internal_hash ^= atlas.z_castling[castling_rights] ^ atlas.z_castling[undo_history[game_ply].castling_rights];
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;
            game_ply++;
            hash_history[game_ply] = internal_hash;

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
            halfmove_clock = undo_history[game_ply].halfmove_clock;

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
            undo_history[game_ply].halfmove_clock = halfmove_clock;

            // If an en passant square was active, we MUST remove its Zobrist contribution
            if (en_passant_square != 64) {
                internal_hash ^= atlas.z_ep[en_passant_square & 7];
                en_passant_square = 64;
            }
            ++halfmove_clock;

            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;
            game_ply++;
            hash_history[game_ply] = internal_hash;
        }

        /**
         * @brief Restores the board state to the moment before the last make_null_move().
         */
        inline void unmake_null_move() noexcept {
            game_ply--;
            
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;

            castling_rights = undo_history[game_ply].castling_rights;
            halfmove_clock = undo_history[game_ply].halfmove_clock;

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
                    mg_score += int_weight[piece] + int_PST_Midgame[piece][cell];
                    eg_score += int_weight[piece] + int_PST_Endgame[piece][cell];
                    
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
        inline bool parse_FEN(const std::string& fen) {
            clear_state();

            std::istringstream ss(fen);
            std::string board_part, stm_part, castling_part, ep_part, halfmove_part, fullmove_part;
            
            auto fail = [&]() noexcept {
                std::cerr << "Invalid FEN string: " << fen << std::endl;
                clear_state();
            };
            
            if (!(ss >> board_part >> stm_part >> castling_part)) {
                fail();
                return false;
            }

            int file = 0;
            int rank = 7;
            int king_count[COLOR_NB] = {0, 0};

            // Piece placement field
            for (const char c : board_part) {
                if (c == '/') {
                    if (file != 8 || rank == 0) { fail(); return false; }
                    rank--;
                    file = 0;
                    continue;
                } 
                if (c >= '1' && c <= '8') {
                    file += c - '0';
                    if (file > 8) { fail(); return false; }
                    continue;
                }
                if (rank < 0 || rank > 7 || file < 0 || file > 7) { fail(); return false; }
                const Square cell = static_cast<Square>(rank * 8 + file);
                switch (c) {
                    case 'r': put_piece(cell, BLACK_ROOK, BLACK); break;
                    case 'n': put_piece(cell, BLACK_KNIGHT, BLACK); break;
                    case 'b': put_piece(cell, BLACK_BISHOP, BLACK); break;
                    case 'q': put_piece(cell, BLACK_QUEEN, BLACK); break;
                    case 'k': put_piece(cell, BLACK_KING, BLACK); king_square[BLACK] = cell; ++king_count[BLACK]; break;
                    case 'p': put_piece(cell, BLACK_PAWN, BLACK); break;
                    case 'R': put_piece(cell, WHITE_ROOK, WHITE); break;
                    case 'N': put_piece(cell, WHITE_KNIGHT, WHITE); break;
                    case 'B': put_piece(cell, WHITE_BISHOP, WHITE); break;
                    case 'Q': put_piece(cell, WHITE_QUEEN, WHITE); break;
                    case 'K': put_piece(cell, WHITE_KING, WHITE); king_square[WHITE] = cell; ++king_count[WHITE]; break;
                    case 'P': put_piece(cell, WHITE_PAWN, WHITE); break;
                    default: fail(); return false;
                }
                ++file;
                if (file > 8) { fail(); return false; }
            }

            if (rank != 0 || file != 8 || king_count[WHITE] != 1 || king_count[BLACK] != 1) { fail(); return false; }
            
            // Active color
            side_to_move = (stm_part == "b") ? BLACK : WHITE;
            if (side_to_move == BLACK) internal_hash ^= atlas.z_side;
            
            // Castling rights
            for (const char c : castling_part) {
                switch (c) {
                    case 'K': castling_rights |= 0x02; break;
                    case 'Q': castling_rights |= 0x04; break;
                    case 'k': castling_rights |= 0x08; break;
                    case 'q': castling_rights |= 0x10; break;
                    case '-': break;
                    default: fail(); return false;
                }
            }
            internal_hash ^= atlas.z_castling[castling_rights];

            // En passant square
            if (ss >> ep_part) {
                if (ep_part != "-" && ep_part.size() >= 2) {
                    int ep_file = ep_part[0] - 'a';
                    int ep_rank = ep_part[1] - '1';
                    if (ep_file >= 0 && ep_file < 8 && ep_rank >= 0 && ep_rank < 8) {
                        en_passant_square = static_cast<Square>(ep_rank * 8 + ep_file);
                        internal_hash ^= atlas.z_ep[ep_file];
                    }
                }
            }

            // Halfmove
            if (ss >> halfmove_part) {
                try { halfmove_clock = static_cast<std::uint16_t>(std::stoul(halfmove_part)); } 
                catch (...) { halfmove_clock = 0;}
            }

            // Fullmove
            if (ss >> fullmove_part) {
                (void)fullmove_part;
            }
            
            recalculate_evaluation();
            hash_history[0] = internal_hash;
            return true;
        }

        inline int evaluate_king_safety(Color our_color) const noexcept {
            int penalty = 0;
            Color enemy_color = Color(our_color ^ 1ULL);
            Square king_cell = king_square[our_color];

            Board king_zone = atlas.get_king_attacks(king_cell) | (1ULL << king_cell);
            Board pawn_shield_zone;
            Piece our_pawn, their_pawn;
            if (our_color == WHITE) {
                pawn_shield_zone = (king_zone << 8);
                our_pawn = WHITE_PAWN;
                their_pawn = BLACK_PAWN;
            } else {
                pawn_shield_zone = (king_zone >> 8);
                our_pawn = BLACK_PAWN;
                their_pawn = WHITE_PAWN;
            }
            
            int shield_pawns = Math::count_bits(boards[our_pawn] & pawn_shield_zone);
            if (shield_pawns < 3) {
                penalty += (3 - shield_pawns) * 15;
            }

            int king_file = king_cell % 8;
            Board file_mask = 0x0101010101010101ULL << king_file;

            if ((boards[our_pawn] & file_mask) == 0) {
                penalty += 25;
                if ((boards[their_pawn] & file_mask) == 0) {
                    penalty += 15;
                }
            }

            if (king_file > 0) {
                Board left_file = 0x0101010101010101ULL << (king_file - 1);
                if ((boards[our_pawn] & left_file) == 0) penalty += 15;
            }
            if (king_file < 7) {
                Board right_file = 0x0101010101010101ULL << (king_file + 1);
                if ((boards[our_pawn] & right_file) == 0) penalty += 15;
            }

            int attackers = 0;
            int attack_weight = 0;

            Piece enemy_knight, enemy_bishop, enemy_rook, enemy_queen;
            if (enemy_color == WHITE) {
                enemy_knight = WHITE_KNIGHT;
                enemy_bishop = WHITE_BISHOP;
                enemy_rook   = WHITE_ROOK;
                enemy_queen  = WHITE_QUEEN;
            } else {
                enemy_knight = BLACK_KNIGHT;
                enemy_bishop = BLACK_BISHOP;
                enemy_rook   = BLACK_ROOK;
                enemy_queen  = BLACK_QUEEN;
            }

            Board knights = boards[enemy_knight];
            while (knights) {
                Square cell = Math::countr_zero(knights);
                if (atlas.get_knight_attacks(cell) & king_zone) {
                    attackers++;
                    attack_weight += 2;
                }
                knights &= knights - 1;
            } 

            Board bishops = boards[enemy_bishop];
            while (bishops) {
                Square cell = Math::countr_zero(bishops);
                if (atlas.get_bishop_attacks(cell, occupancy) & king_zone) {
                    attackers++;
                    attack_weight += 2;
                }
                bishops &= bishops - 1;
            } 

            Board rooks = boards[enemy_rook];
            while (rooks) {
                Square cell = Math::countr_zero(rooks);
                if (atlas.get_rook_attacks(cell, occupancy) & king_zone) {
                    attackers++;
                    attack_weight += 3;
                }
                rooks &= rooks - 1;
            }

            Board queens = boards[enemy_queen];
            while (queens) {
                Square cell = Math::countr_zero(queens);
                if (atlas.get_queen_attacks(cell, occupancy) & king_zone) {
                    attackers++;
                    attack_weight += 5;
                }
                queens &= queens - 1;
            }

            if (attackers >= 2) {
                penalty += (attack_weight * attack_weight) * 3;
            }

            return penalty;
        }

        /**
         * @brief Computes the static heuristic evaluation of the current position.
         * Takes into account material, PSTs, mobility, pawn structure, and center control.
         * @return Evaluation score relative to the side to move (positive is good for current player).
         */
        inline short evaluate() const noexcept {
            // Draw - Both sides don't have enough material
            if (boards[WHITE_PAWN] == 0 && boards[BLACK_PAWN] == 0 &&
                boards[WHITE_ROOK] == 0 && boards[BLACK_ROOK] == 0 &&
                boards[WHITE_QUEEN] == 0 && boards[BLACK_QUEEN] == 0) {
                
                int white_minors = Math::count_bits(boards[WHITE_KNIGHT] | boards[WHITE_BISHOP]);
                int black_minors = Math::count_bits(boards[BLACK_KNIGHT] | boards[BLACK_BISHOP]);
                
                if (white_minors <= 1 && black_minors <= 1) {
                    return 0; 
                }
            }

            short phase = current_phase;
            if (phase > 24) phase = 24;
            short base_score = static_cast<short>((mg_score * phase + eg_score * (24 - phase)) / 24);

            short bonus = 0;

            int white_knight_mobility = 0, white_bishop_mobility = 0, white_rook_mobility = 0, white_queen_mobility = 0;
            int black_knight_mobility = 0, black_bishop_mobility = 0, black_rook_mobility = 0, black_queen_mobility = 0;

            Board white_knights = boards[WHITE_KNIGHT];
            while (white_knights) {
                Square cell = Math::countr_zero(white_knights);
                white_knight_mobility += Math::count_bits(atlas.get_knight_attacks(cell) & ~pieces[WHITE]);
                white_knights &= white_knights - 1;
            }

            Board black_knights = boards[BLACK_KNIGHT];
            while (black_knights) {
                Square cell = Math::countr_zero(black_knights);
                black_knight_mobility += Math::count_bits(atlas.get_knight_attacks(cell) & ~pieces[BLACK]);
                black_knights &= black_knights - 1;
            }

            Board white_bishops = boards[WHITE_BISHOP];
            while (white_bishops) {
                Square cell = Math::countr_zero(white_bishops);
                white_bishop_mobility += Math::count_bits(atlas.get_bishop_attacks(cell, occupancy) & ~pieces[WHITE]);
                white_bishops &= white_bishops - 1;
            }

            Board black_bishops = boards[BLACK_BISHOP];
            while (black_bishops) {
                Square cell = Math::countr_zero(black_bishops);
                black_bishop_mobility += Math::count_bits(atlas.get_bishop_attacks(cell, occupancy) & ~pieces[BLACK]);
                black_bishops &= black_bishops - 1;
            }
            
            Board white_rooks = boards[WHITE_ROOK];
            while (white_rooks) {
                Square cell = Math::countr_zero(white_rooks);
                white_rook_mobility += Math::count_bits(atlas.get_rook_attacks(cell, occupancy) & ~pieces[WHITE]);
                white_rooks &= white_rooks - 1;
            }

            Board black_rooks = boards[BLACK_ROOK];
            while (black_rooks) {
                Square cell = Math::countr_zero(black_rooks);
                black_rook_mobility += Math::count_bits(atlas.get_rook_attacks(cell, occupancy) & ~pieces[BLACK]);
                black_rooks &= black_rooks - 1;
            }
            
            Board white_queens = boards[WHITE_QUEEN];
            while (white_queens) {
                Square cell = Math::countr_zero(white_queens);
                white_queen_mobility += Math::count_bits(atlas.get_queen_attacks(cell, occupancy) & ~pieces[WHITE]);
                white_queens &= white_queens - 1;
            }

            Board black_queens = boards[BLACK_QUEEN];
            while (black_queens) {
                Square cell = Math::countr_zero(black_queens);
                black_queen_mobility += Math::count_bits(atlas.get_queen_attacks(cell, occupancy) & ~pieces[BLACK]);
                black_queens &= black_queens - 1;
            }

            bonus += (white_knight_mobility - black_knight_mobility) * 4;
            bonus += (white_bishop_mobility - black_bishop_mobility) * 3;
            bonus += (white_rook_mobility   - black_rook_mobility)   * 2;
            bonus += (white_queen_mobility  - black_queen_mobility);

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

                if (!(boards[BLACK_PAWN] & atlas.pawn_passed_masks[WHITE][cell])) {
                    bonus += (rank - 1) * 10 + 20;
                }

                white_pawns &= white_pawns - 1;
            }

            Board black_pawns = boards[BLACK_PAWN];
            while (black_pawns) {
                Square cell = Math::countr_zero(black_pawns);
                int rank = cell / 8;
                
                if (!(boards[WHITE_PAWN] & atlas.pawn_passed_masks[BLACK][cell])) {
                    bonus -= (6 - rank) * 10 + 20;
                }

                black_pawns &= black_pawns - 1;
            }

            Board white_rooks_eval = boards[WHITE_ROOK];
            while (white_rooks_eval) {
                Square cell = Math::countr_zero(white_rooks_eval);
                Board file_mask = 0x0101010101010101ULL << (cell % 8);
                if (!(boards[WHITE_PAWN] & file_mask)) {
                    bonus += 20; 
                    if (!(boards[BLACK_PAWN] & file_mask)) bonus += 15; 
                }
                white_rooks_eval &= white_rooks_eval - 1;
            }

            Board black_rooks_eval = boards[BLACK_ROOK];
            while (black_rooks_eval) {
                Square cell = Math::countr_zero(black_rooks_eval);
                Board file_mask = 0x0101010101010101ULL << (cell % 8);
                if (!(boards[BLACK_PAWN] & file_mask)) {
                    bonus -= 20; 
                    if (!(boards[WHITE_PAWN] & file_mask)) bonus -= 15;
                }
                black_rooks_eval &= black_rooks_eval - 1;
            }

            Board center_squares = (1ULL << 27) | (1ULL << 28) | (1ULL << 35) | (1ULL << 36);
            int white_center_control = Math::count_bits(pieces[WHITE] & center_squares);
            int black_center_control = Math::count_bits(pieces[BLACK] & center_squares);
            bonus += (white_center_control - black_center_control) * 10;

            int white_king_safety_penalty = evaluate_king_safety(WHITE);
            int black_king_safety_penalty = evaluate_king_safety(BLACK);

            bonus -= (white_king_safety_penalty * phase) / 24;
            bonus += (black_king_safety_penalty * phase) / 24;

            if (Math::count_bits(boards[WHITE_BISHOP]) >= 2) bonus += 40;
            if (Math::count_bits(boards[BLACK_BISHOP]) >= 2) bonus -= 40;

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

        inline Square get_pieces(Color color) const noexcept { return pieces[color]; }

        inline Square get_pawn_and_king_boards() const noexcept { return boards[WHITE_PAWN] | boards[BLACK_PAWN] | boards[WHITE_KING] | boards[BLACK_KING]; }
        /// @}

        /// @name Draw conditions
        /// @{
        
        inline bool is_fifty_move_draw() const noexcept {
            return halfmove_clock >= 100;
        }

        inline bool is_threefold_repetition() const noexcept {
            int repetitions = 1;
            int limit = static_cast<int>(game_ply) - static_cast<int>(halfmove_clock);
            if (limit < 0) limit = 0;

            for (int i = static_cast<int>(game_ply) - 2; i >= limit; i -= 2) {
                if (hash_history[i] == internal_hash && ++repetitions >= 2) {
                    return true;
                }
            }
            return false;
        }
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
        Move killer_moves[Constants::MAX_DEPTH][2];            ///< Heuristic table storing moves that caused a beta-cutoff, indexed by search depth. Used for move ordering to try strong refutations early in sibling nodes.
        int history[COLOR_NB][CELL_NB][CELL_NB];               ///< Butterfly history heuristic table storing success scores for quiet moves. Indexed by [Color][From Square][To Square]. Used for Late Move Reductions (LMR) and move ordering.
        
        std::atomic<bool> abort_search{false};

        long long nodes_visited;
        long long max_nodes = -1;
        std::chrono::steady_clock::time_point end_time_limit;

        /**
         * @brief Precomputed table for Late Move Reductions (LMR).
         * Provides depth reduction values based on the current depth and the number of moves already searched.
         */
        short LMR_table[64][256];
    
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
        std::function<void(short depth, short score, long long nodes, long long time_ms, const std::vector<Move>& pv)> on_iteration_end = nullptr;

        /**
         * @brief Callback triggered periodically during the search.
         * Useful for UCI communication (e.g., sending "info depth...", checking GUI interrupts).
         */
        std::function<void(const Position&)> on_node_update = nullptr;
        
        Search() : transposition_table(Constants::TT_SIZE) {
            clear_heuristics();
            calculate_LMR_tables();
        }

        inline void clear_heuristics() noexcept {
            for (int i = 0; i < Constants::MAX_DEPTH; i++) {
                killer_moves[i][0] = 0;
                killer_moves[i][1] = 0;
            }
            for (short color = 0; color < COLOR_NB; color++) {
                for (Square from = 0; from < CELL_NB; from++) {
                    for (Square to = 0; to < CELL_NB; to++) {
                        history[color][from][to] = 0;
                    }
                }   
            }
        }

        inline void calculate_LMR_tables() noexcept {
            for (int d = 0; d < 64; ++d) {
                for (int m = 0; m < 256; ++m) {
                    LMR_table[d][m] = 1 + static_cast<short>(std::log(std::max(1, d)) * std::log(std::max(1, m)) / 2.0);
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
        Move get_best_move(Position& pos, int time_limit_ms, short target_depth, long long target_nodes, short alpha, short beta, short& out_score) {
            abort_search = false;
            nodes_visited = 0;
            this->max_nodes = target_nodes;
            short depth = 1;
            Move best_move = 0;
            Move current_depth_best = 0;
 
            auto start_time = std::chrono::steady_clock::now();
            if (time_limit_ms > 0) {
                end_time_limit = start_time + std::chrono::milliseconds(time_limit_ms);
            } else {
                end_time_limit = start_time + std::chrono::hours(1000);
            }

            short search_max_depth = (target_depth > 0) ? target_depth : Constants::MAX_DEPTH;

            clear_heuristics();

            short prev_score = 0;
            short aspiration_delta = 20; // Starts at ~1/5 of a pawn and will widen if needed

            while (!abort_search && depth <= search_max_depth) {
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
                        temp_alpha = std::max(static_cast<short>(-Constants::VALUE_INFINITE), static_cast<short>(temp_alpha - aspiration_delta));
                        aspiration_delta += aspiration_delta / 2;
                    } else if (out_score >= temp_beta) {
                        temp_beta = std::min(static_cast<short>(Constants::VALUE_INFINITE), static_cast<short>(temp_beta + aspiration_delta));
                        aspiration_delta += aspiration_delta / 2;
                    } else {
                        break;
                    }
                }

                if (!abort_search && current_depth_best != 0) {
                    best_move = current_depth_best;
                    prev_score = out_score;
                    aspiration_delta = std::max(15, aspiration_delta - 5); // Narrow down slightly the window for the next depth

                    if (on_iteration_end) {
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();
                        std::vector<Move> pv = get_PV(pos, depth);
                        on_iteration_end(depth, out_score, nodes_visited, elapsed, pv);
                    }
                }
                depth++;
            }
            return best_move;
        }

        /**
         * @brief Retrieves the Principal Variation (PV) line from the Transposition Table safely.
         */
        inline std::vector<Move> get_PV(Position pos, Square max_length) {
            std::vector<Move> pv;
            std::vector<Board> visited_hashes; 
            
            for (Square i = 0; i < max_length; i++) {
                Board current_hash = pos.get_hash();
                
                bool loop = false;
                for (Board h : visited_hashes) {
                    if (h == current_hash) { loop = true; break; }
                }
                if (loop) break;
                
                visited_hashes.push_back(current_hash);

                Board index = current_hash & (Constants::TT_SIZE - 1);
                if (transposition_table[index].key == current_hash && transposition_table[index].best_move != 0) {
                    Move tt_move = transposition_table[index].best_move;
                    if (!pos.make_move(tt_move)) break; // No legal moves
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
        void stop() noexcept { abort_search = true; }

    private:
        /// @name Search Algorithms
        /// @{

        inline Move find_best_move(Position& pos, short depth, short alpha, short beta, short& out_score) {
            short original_alpha = alpha;
            Move best_move_found = 0;
            short best_score = -Constants::VALUE_INFINITE;
            MoveList moves = pos.generate_all_moves();

            Board hash   = pos.get_hash();
            Board index  = hash & (Constants::TT_SIZE - 1);
            
            Move tt_move = 0;
            if (transposition_table[index].key == hash) {
                tt_move = transposition_table[index].best_move;
            }

            score_moves(pos, moves, tt_move, 0);

            short legal_moves = 0;
            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                if (!pos.make_move(moves.moves[i])) continue;
                legal_moves++;

                // Root PV node search - we want the most accurate score here, so we search with a full window
                short score;
                if (legal_moves == 1) {
                    score = -search(pos, depth - 1, -beta, -alpha, 1);
                } else {
                    score = -search(pos, depth - 1, -alpha - 1, -alpha, 1);
                    if (score > alpha && score < beta) {
                        score = -search(pos, depth - 1, -beta, -alpha, 1);
                    }
                }
                pos.unmake_move(moves.moves[i]);

                if (abort_search) return best_move_found;
                if (score > best_score) {
                    best_score = score;
                    best_move_found = moves.moves[i];
                }
                if (score > alpha) {
                    alpha = score;
                }
                if (score >= beta) {
                    break;
                }
            }

            out_score = best_score;

            if (best_move_found != 0) {
                Move store_move = best_move_found;
                if (best_score <= original_alpha && tt_move != 0) {
                    store_move = tt_move;
                }

                if (transposition_table[index].key != hash || depth >= transposition_table[index].depth) {
                    transposition_table[index].key = hash;
                    transposition_table[index].best_move = store_move;
                    transposition_table[index].score = value_to_tt(best_score, 0);
                    transposition_table[index].depth = depth;
                    transposition_table[index].flag = (best_score >= beta) ? BETA : ((best_score > original_alpha) ? EXACT : ALPHA);
                    transposition_table[index].phase = pos.get_phase();
                }
            }
            return best_move_found;
        }

        /**
         * @brief Alpha-Beta principal variation search (PVS) with Quiescence, LMR, Null Move, and Singular Extensions.
         * @param pos Current board position.
         * @param depth Remaining search depth.
         * @param alpha Lower bound of the search window.
         * @param beta Upper bound of the search window.
         * @param ply_from_root Distance from the original root node.
         * @param excluded_move A move to exclude from generation (used during Singular Extensions).
         * @param extensions Number of depth extensions applied so far in this branch.
         * @return The best evaluation score found in this sub-tree.
         */
        inline short search(Position& pos, short depth, short alpha, short beta, short ply_from_root, Move excluded_move = 0, short extensions = 0) noexcept {
            if (abort_search) return 0;
            if (ply_from_root >= Constants::MAX_DEPTH - 1) {
                return pos.evaluate();
            }
            if (pos.get_game_ply() >= Constants::MAX_GAME_PLIES - 1) {
                return pos.evaluate();
            }
            if (pos.is_fifty_move_draw() || pos.is_threefold_repetition()) {
                return 0;
            }

            bool is_PV = (beta - alpha) > 1;
            
            // Check detection
            bool in_check = pos.is_square_attacked(pos.get_king_square(pos.get_side_to_move()), Color(pos.get_side_to_move() ^ 1ULL));
            
            if (depth <= 0) return quiescence(pos, alpha, beta, ply_from_root + 1, 0);

            // Mate Distance Pruning
            short mate_value = Constants::VALUE_MATE - ply_from_root;
            if (mate_value < beta) {
                beta = mate_value;
                if (alpha >= mate_value) return mate_value;
            }
            short mated_value = -Constants::VALUE_MATE + ply_from_root;
            if (mated_value > alpha) {
                alpha = mated_value;
                if (beta <= mated_value) return mated_value;
            }

            short temp = alpha;
            Board hash = pos.get_hash();
            Board index = hash & (Constants::TT_SIZE - 1);

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

            // Internal Iterative Deeping (IID)
            if (depth >= 5 && tt_move == 0 && excluded_move == 0 && !in_check) {
                short iid_depth = depth - 2;
                search(pos, iid_depth, alpha, beta, ply_from_root);
                if (transposition_table[index].key == hash) {
                    tt_move = transposition_table[index].best_move;
                }
            }

            bool extended = false;
            if (depth >= 6 && tt_move != 0 && excluded_move == 0 && tt_depth >= depth - 3 && transposition_table[index].flag != ALPHA) {
                short tt_score = value_from_tt(transposition_table[index].score, ply_from_root);
                if (std::abs(tt_score) < Constants::VALUE_MATE - 100) {
                    short singular_beta = tt_score - depth;
                    
                    // Exclude the best move and see if any other move can reach its level
                    short singular_score = search(pos, depth / 2, singular_beta - 1, singular_beta, ply_from_root + 1, tt_move, extensions);
                    
                    if (singular_score < singular_beta) {
                        extended = true; // This move is vastly superior to all others, extend search!
                    }
                }
            }

            short static_eval = pos.evaluate();

            // Reverse Futility Pruning (Static Null Move Pruning)
            if (!is_PV && !in_check && depth <= 5 && ply_from_root > 0 && !extended && std::abs(static_eval) < Constants::VALUE_MATE - 100) {
                short rfp_margin = 100 * depth;
                if (static_eval - rfp_margin >= beta) {
                    return static_eval;
                }
            }

            // Razoring
            if (!is_PV && !in_check && depth <= 2 && excluded_move == 0) {
                short razor_margin = 300 + 100 * depth;
                if (static_eval + razor_margin <= alpha) {
                    short q_score = quiescence(pos, alpha, beta, ply_from_root, 0);
                    if (q_score <= alpha) return q_score;
                }
            }

            bool futility_pruning = false;
            if (!is_PV && !in_check && !extended && depth <= 3 && std::abs(static_eval) < Constants::VALUE_MATE - 100) {
                int fp_margin = static_eval + 150 * depth;
                if (fp_margin <= alpha) {
                    futility_pruning = true;
                }
            }

            Board non_pawns = pos.get_pieces(pos.get_side_to_move()) & ~(pos.get_pawn_and_king_boards());

            // Null Move Pruning
            if (non_pawns && !is_PV && depth >= 3 && !in_check && ply_from_root > 0 && pos.get_phase() > 2 && excluded_move == 0) {
                if (static_eval >= beta) {
                    pos.make_null_move();

                    short R = 2 + depth / 4;
                    short null_score = -search(pos, std::max<short>(0, depth - 1 -R), -beta, -beta + 1, ply_from_root + 1, 0, extensions);

                    pos.unmake_null_move();

                    if (abort_search) return 0;
                    if (null_score >= beta) {
                        return beta; 
                    }
                }
            }

            MoveList moves = pos.generate_all_moves();
            score_moves(pos, moves, tt_move, ply_from_root);

            short best_score = -Constants::VALUE_INFINITE;
            Move best_move = 0;
            short legal_moves = 0;

            Move searched_quiets[256];
            int quiet_count = 0;
            Color color = pos.get_side_to_move();

            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                Move current_move = moves.moves[i];
                
                if (current_move == excluded_move) continue;

                std::uint8_t flag = get_flag(current_move);
                bool is_tactical = (flag == CAPTURE || flag == EN_PASSANT || flag >= KNIGHT_PROMOTION);
                bool is_killer = (current_move == killer_moves[ply_from_root][0] || current_move == killer_moves[ply_from_root][1]);

                // SEE Pruning (Before make_move)
                if (!is_PV && !in_check && depth <= 3 && is_tactical && flag < KNIGHT_PROMOTION && excluded_move == 0) {
                    if (pos.see_capture(current_move) < -200 * depth) continue;
                }

                if (!pos.make_move(current_move)) continue;

                bool gives_check = pos.is_square_attacked(pos.get_king_square(pos.get_side_to_move()), Color(pos.get_side_to_move() ^ 1ULL));

                // Apply Pruning (After make_move, protected by gives_check)
                if (!is_PV && !in_check && !gives_check && !is_killer && excluded_move == 0) {
                    
                    // Futility Pruning
                    if (futility_pruning && !is_tactical && legal_moves > 0) {
                        pos.unmake_move(current_move);
                        continue;
                    }

                    // History Pruning
                    if (depth <= 3 && !is_tactical && legal_moves > 0) {
                        int hist = history[color][get_from(current_move)][get_to(current_move)];
                        if (hist < -4000 * depth) {
                            pos.unmake_move(current_move);
                            continue;
                        }
                    }

                    // Late Move Pruning
                    if (depth <= 4 && !is_tactical) {
                        int lmp_threshold = 3 + 2 * depth * depth;
                        if (legal_moves > lmp_threshold) { // legal_moves here is BEFORE increment
                            pos.unmake_move(current_move);
                            continue;
                        }
                    }
                }

                if (!is_tactical) searched_quiets[quiet_count++] = current_move;
                
                // Extensions
                short extension = 0;
                if (in_check && extensions < 8) {
                    extension = 1; // Check Evasion extension
                } else if (gives_check && extensions < 8) {
                    extension = 1; // Gives Check extension
                } else if (extended && current_move == tt_move) {
                    extension = 1; // Singular extension
                }

                short next_depth = depth - 1 + extension;
                short next_extensions = extensions + extension;

                legal_moves++; // Increment before PVS logic

                short score;
                if (legal_moves == 1) {
                    // 1. PV node: Search with full window
                    score = -search(pos, next_depth, -beta, -alpha, ply_from_root + 1, 0, next_extensions);
                } else {
                    // 2. Non-PV nodes: Scout search with Null Window (PVS)
                    short reduction = 0;

                    // Late Move Reductions (LMR) integrated with PVS
                    if (legal_moves >= 4 && depth >= 3 && !is_tactical && !in_check && !gives_check && !is_killer) {
                        reduction = LMR_table[depth < 64 ? depth : 63][legal_moves < 256 ? legal_moves : 255];
                        
                        if (is_PV) reduction--;
                        
                        int hist = history[color][get_from(current_move)][get_to(current_move)];
                        if (hist > 0) reduction--;
                        else if (hist < -4000) reduction++;
                        
                        if (reduction >= depth) reduction = depth - 1;  
                        if (reduction < 0) reduction = 0;
                    }

                    score = -search(pos, std::max<short>(0, next_depth - reduction), -alpha - 1, -alpha, ply_from_root + 1, 0, next_extensions);

                    // Re-search at full depth with null window if reduced search fails high
                    if (reduction > 0 && score > alpha) {
                        score = -search(pos, next_depth, -alpha - 1, -alpha, ply_from_root + 1, 0, next_extensions);
                    }

                    if (score > alpha && score < beta) {
                        score = -search(pos, next_depth, -beta, -alpha, ply_from_root + 1, 0, next_extensions);
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
                    int bonus = depth * depth;
                    if (bonus > 400) bonus = 400;

                    // Beta cutoff: update killer moves and history for quiet moves
                    if (!is_tactical && ply_from_root < Constants::MAX_DEPTH) {
                        // Update killer moves
                        if (killer_moves[ply_from_root][0] != current_move) {
                            killer_moves[ply_from_root][1] = killer_moves[ply_from_root][0];
                            killer_moves[ply_from_root][0] = current_move;
                        }

                        // Update history heuristic with gravity
                        Square from = get_from(current_move);
                        Square to = get_to(current_move);
                        history[color][from][to] += bonus - history[color][from][to] * std::abs(bonus) / 16384;
                    }

                    int penalty_count = is_tactical ? quiet_count : quiet_count - 1;
                    for (int j = 0; j < penalty_count; j++) {
                        Square q_from = get_from(searched_quiets[j]);
                        Square q_to = get_to(searched_quiets[j]);
                        history[color][q_from][q_to] += (-bonus) - history[color][q_from][q_to] * std::abs(-bonus) / 16384;
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
                Move store_move = best_move;
                if (best_score <= temp && tt_move != 0) {
                    store_move = tt_move;
                }

                if (transposition_table[index].key != hash || depth >= transposition_table[index].depth) {
                    transposition_table[index].key = hash;
                    transposition_table[index].best_move = store_move;
                    transposition_table[index].score = value_to_tt(best_score, ply_from_root);
                    transposition_table[index].depth = depth;
                    transposition_table[index].flag = (best_score >= beta) ? BETA : ((best_score > temp) ? EXACT : ALPHA);
                    transposition_table[index].phase = pos.get_phase();
                }
            }

            return best_score;
        }

        /**
         * @brief Quiescence Search (QS) to resolve tactical skirmishes before evaluating a node statically.
         * Mitigates the horizon effect by continuing the search for all captures and check evasions.
         * @param pos Current board position.
         * @param alpha Lower bound score.
         * @param beta Upper bound score.
         * @param ply_from_root Current ply from root.
         * @param q_depth Current depth within the Quiescence search itself.
         * @return The static evaluation or best tactical sequence score.
         */
        inline short quiescence(Position& pos, short alpha, short beta, short ply_from_root, short q_depth) {
            check_time(pos); 
            if (abort_search) return 0;

            if (ply_from_root >= Constants::MAX_DEPTH - 1) {
                return pos.evaluate();
            }
            if (pos.is_fifty_move_draw() || pos.is_threefold_repetition()) {
                return 0;
            }

            Board hash = pos.get_hash();
            Board index = hash & (Constants::TT_SIZE - 1);
            Move tt_move = 0;
            if (transposition_table[index].key == hash) {
                tt_move = transposition_table[index].best_move;
                short tt_score = value_from_tt(transposition_table[index].score, ply_from_root);
                if (transposition_table[index].flag == EXACT) return tt_score;
                if (transposition_table[index].flag == ALPHA && tt_score <= alpha) return tt_score;
                if (transposition_table[index].flag == BETA && tt_score >= beta) return tt_score;
            }

            bool in_check = pos.is_square_attacked(pos.get_king_square(pos.get_side_to_move()), Color(pos.get_side_to_move() ^ 1ULL));

            // Stand pat: if not in check, establish a lower bound score
            short stand_pat = pos.evaluate();
            if (!in_check) {
                if (stand_pat >= beta) return beta;
                if (stand_pat > alpha) alpha = stand_pat;
            }

            // If in check: we generate ALL legal moves (to evade check).
            // If not in check: we only generate captures to keep search quiet.
            MoveList moves = in_check ? pos.generate_all_moves() : pos.generate_all_captures();
            score_moves(pos, moves, tt_move, ply_from_root); // Quick scoring

            short legal_moves = 0;
            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                Move move = moves.moves[i];

                std::uint8_t flag = get_flag(move);
                
                // Delta Pruning & SEE Pruning 
                if (!in_check && q_depth >= 1) {
                    Piece victim = pos.get_piece(get_to(move));
                    
                    int val_gain = 0;
                    if (flag >= KNIGHT_PROMOTION) {
                        val_gain = Constants::PIECE_VALUES[WHITE_QUEEN];
                    } else if (flag == EN_PASSANT) {
                        val_gain = 100;
                    } else if (victim != EMPTY) {
                        val_gain = Constants::PIECE_VALUES[victim];
                    }

                    if (stand_pat + val_gain + 200 < alpha) {
                        continue;
                    }
                    
                    if (flag < KNIGHT_PROMOTION && pos.see_capture(move) < 0) {
                        continue; // Prune losing captures
                    }
                }
                
                if (!pos.make_move(moves.moves[i])) continue;
                legal_moves++;

                short score = -quiescence(pos, -beta, -alpha, ply_from_root + 1, q_depth + 1);
                pos.unmake_move(moves.moves[i]);

                if (abort_search) return 0;
                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            }

            // If we are under check but have no legal moves, it is checkmate
            if (in_check && legal_moves == 0) {
                return -Constants::VALUE_MATE + ply_from_root;
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
        inline void score_moves(const Position& pos, MoveList& list, Move tt_move, short ply) noexcept {
            Color color = pos.get_side_to_move();
            for (Square i = 0; i < list.count; i++) {
                Move move = list.moves[i];

                // TT move gets highest priority
                if (move == tt_move) {
                    list.scores[i] = Constants::SCORE_TT;
                    continue;
                }

                std::uint8_t flag = get_flag(move);
                bool is_tactical = (flag == CAPTURE || flag == EN_PASSANT || flag >= KNIGHT_PROMOTION);

                if (is_tactical) {
                    int see_val = pos.see_capture(move);
                    if (see_val < 0) {
                        list.scores[i] = 750000 + see_val; 
                    } else {
                        list.scores[i] = 1000000 + list.scores[i] + (see_val * 10);
                    }
                } else {
                    if (move == killer_moves[ply][0]) {
                        list.scores[i] = Constants::SCORE_KILLER_1;
                    } else if (move == killer_moves[ply][1]) {
                        list.scores[i] = Constants::SCORE_KILLER_2;
                    } else {
                        // History heuristic for quiet moves
                        Square from = get_from(move);
                        Square to = get_to(move);
                        int hist = history[color][from][to];
                        if (hist >  700000) hist =  700000;
                        if (hist < -700000) hist = -700000;
                        list.scores[i] = hist;
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
            int best_score = list.scores[current_index];
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
            if (max_nodes > 0 && nodes_visited >= max_nodes) {
                abort_search = true;
            }
            if ((nodes_visited & 2047) == 0) {
                if (end_time_limit.time_since_epoch().count() > 0) {
                    if (std::chrono::steady_clock::now() >= end_time_limit) {
                        abort_search = true;
                    }
                }
                if (on_node_update) on_node_update(pos);
            }
        }

        /// @}
    };
}