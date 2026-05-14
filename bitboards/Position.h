#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <chrono>

#include "../data/EvalWeights.h"
#include "Atlas.h"

using Move = std::uint16_t;

namespace MyChess {

    namespace Math {

        constexpr Square Kim_Walish_table[64] = {
             0, 47,  1, 56, 48, 27,  2, 60,
            57, 49, 41, 37, 28, 16,  3, 61,
            54, 58, 35, 52, 50, 42, 21, 44,
            38, 32, 29, 23, 17, 11,  4, 62,
            46, 55, 26, 59, 40, 36, 15, 53,
            34, 51, 20, 43, 31, 22, 10, 45,
            25, 39, 14, 33, 19, 30,  9, 24,
            13, 18,  8, 12,  7,  6,  5, 63
        };

        inline Square countr_zero(Board b) noexcept {
            #if defined(__GNUC__) || defined(__clang__)
            return __builtin_ctzll(b);
            #else
            return Kim_Walish_table[((b ^ (b - 1)) * 0x03f79d71b4cb0a89ULL) >> 58];
            #endif
        }
    }

    constexpr Square MAX_DEPTH = 2048;

    enum move_flags : std::uint8_t {
        SILENT_MOVE, PAWN_DOUBLE_JUMP, CASTLING, EN_PASSANT, CAPTURE, 
        KNIGHT_PROMOTION, BISHOP_PROMOTION, ROOK_PROMOTION, QUEEN_PROMOTION,
        KNIGHT_PROMOTION_AND_CAPTURE, BISHOP_PROMOTION_AND_CAPTURE, 
        ROOK_PROMOTION_AND_CAPTURE, QUEEN_PROMOTION_AND_CAPTURE
    };

    enum TT_flags : std::uint8_t { EXACT, ALPHA, BETA };

    struct UndoInfo {
        Piece captured_piece;
        std::uint8_t castling_rights;
        Square en_passant_square;
    };

    struct MoveList {
        Move moves[256];
        short scores[256];
        Square count = 0;

        void add(const Move move, const short move_score) {
            scores[count] = move_score;
            moves[count++] = move;
        }
    };

    struct TTEntry {
        Board key;
        Move best_move;
        short score;
        short depth;
        std::uint8_t flag;
        Square phase;
    };
    
    class Position {
    private:
        Board boards[PIECE_NB];
        Board pieces[COLOR_NB];
        Board occupancy;
        Color side_to_move;
        std::uint8_t castling_rights;
        std::uint8_t castling_mask[CELL_NB] = {
            13, 15, 15, 15, 12, 15, 15, 14,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            15, 15, 15, 15, 15, 15, 15, 15,
            7,  15, 15, 15, 3,  15, 15, 11
        };
        Square en_passant_square;
        Piece piece_on_square[CELL_NB];
        UndoInfo history[MAX_DEPTH];
        Square game_ply = 0;
        Square king_square[COLOR_NB];
        short mg_score, eg_score;
        Board internal_hash;
        std::vector<TTEntry> transposition_table;
        Square current_phase;
        bool abort_search;
        long long nodes_visited;
        std::chrono::steady_clock::time_point end_time_limit;

        public:
            Position(const std::string& fen) : transposition_table(1 << 20) {
                parse_FEN(fen);
            }
        
        private:
        inline Move encode_move(const Square from, const Square to, const std::uint8_t flag) noexcept {
            return (from | (to << 6) | (flag << 12));
        }
        
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

        inline Square get_from(const Move move) noexcept {
            return move & 0x3F;
        }
        
        inline Square get_to(const Move move) noexcept {
            return (move >> 6) & 0x3F;
        }
        
        inline std::uint8_t get_flag(const Move move) noexcept {
            return move >> 12;
        }

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

        inline Board shift_forward(Board b) const noexcept {
            return (side_to_move == WHITE) ? (b << 8) : (b >> 8);
        }

        inline void add_pawn_move(MoveList& list, Square from, Square to, std::uint8_t flag) noexcept {
            if (to >= 56 || to <= 7) {
                if (flag == CAPTURE) {
                        Piece from_piece = piece_on_square[from];
                        Piece to_piece   = piece_on_square[to];
                        short victim_value   = std::abs(weight[to_piece]);
                        short attacker_value = std::abs(weight[from_piece]);
                        short move_score     = (victim_value * 100) - (attacker_value / 10);
                        list.add(encode_move(from, to, QUEEN_PROMOTION_AND_CAPTURE), move_score + weight[WHITE_QUEEN]);
                        list.add(encode_move(from, to, KNIGHT_PROMOTION_AND_CAPTURE), move_score + weight[WHITE_KNIGHT] * 2);
                        list.add(encode_move(from, to, ROOK_PROMOTION_AND_CAPTURE), move_score + weight[WHITE_ROOK]);
                        list.add(encode_move(from, to, BISHOP_PROMOTION_AND_CAPTURE), move_score + weight[WHITE_BISHOP]);
                } else {
                        list.add(encode_move(from, to, QUEEN_PROMOTION), weight[WHITE_QUEEN]);
                        list.add(encode_move(from, to, KNIGHT_PROMOTION), weight[WHITE_KNIGHT] * 2);
                        list.add(encode_move(from, to, ROOK_PROMOTION), weight[WHITE_ROOK]);
                        list.add(encode_move(from, to, BISHOP_PROMOTION), weight[WHITE_BISHOP]);
                }
            } else {
                Piece from_piece = piece_on_square[from];
                Piece to_piece   = piece_on_square[to];
                short victim_value   = std::abs(weight[to_piece]);
                short attacker_value = std::abs(weight[from_piece]);
                short move_score     = (victim_value * 100) - (attacker_value / 10);
                list.add(encode_move(from, to, flag), (flag == CAPTURE) ? move_score : 0);
            }
        }

    public:    
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

            //Loading the board
            Square counter = 0;
            Square file = 0;
            Square rank = 7;
            while(fen[counter] != ' ') {
                Square cell = rank * 8 + file;
                switch (fen[counter]) {
                case 'r':
                    put_piece(cell, BLACK_ROOK, BLACK);
                    internal_hash ^= atlas.z_pieces[BLACK_ROOK][cell];
                    break;
        
                case 'n':
                    put_piece(cell, BLACK_KNIGHT, BLACK);
                    internal_hash ^= atlas.z_pieces[BLACK_KNIGHT][cell];
                    break;
            
                case 'b':
                    put_piece(cell, BLACK_BISHOP, BLACK);
                    internal_hash ^= atlas.z_pieces[BLACK_BISHOP][cell];
                    break;
                
                case 'q':
                    put_piece(cell, BLACK_QUEEN, BLACK);
                    internal_hash ^= atlas.z_pieces[BLACK_QUEEN][cell];
                    break;
                    
                case 'k':
                    put_piece(cell, BLACK_KING, BLACK);
                    internal_hash ^= atlas.z_pieces[BLACK_KING][cell];
                    king_square[BLACK] = cell;
                    break;

                case 'p':
                    put_piece(cell, BLACK_PAWN, BLACK);
                    internal_hash ^= atlas.z_pieces[BLACK_PAWN][cell];
                    break;
                
                case 'R':
                    put_piece(cell, WHITE_ROOK, WHITE);
                    internal_hash ^= atlas.z_pieces[WHITE_ROOK][cell];
                    break;
        
                case 'N':
                    put_piece(cell, WHITE_KNIGHT, WHITE);
                    internal_hash ^= atlas.z_pieces[WHITE_KNIGHT][cell];
                    break;
            
                case 'B':
                    put_piece(cell, WHITE_BISHOP, WHITE);
                    internal_hash ^= atlas.z_pieces[WHITE_BISHOP][cell];
                    break;
                
                case 'Q':
                    put_piece(cell, WHITE_QUEEN, WHITE);
                    internal_hash ^= atlas.z_pieces[WHITE_QUEEN][cell];
                    break;
                    
                case 'K':
                    put_piece(cell, WHITE_KING, WHITE);
                    internal_hash ^= atlas.z_pieces[WHITE_KING][cell];
                    king_square[WHITE] = cell;
                    break;

                case 'P':
                    put_piece(cell, WHITE_PAWN, WHITE);
                    internal_hash ^= atlas.z_pieces[WHITE_PAWN][cell];
                    break;
                
                case '/':
                    rank--;
                    file = 65535;
                    break;
                
                default:
                    file += fen[counter] - '1';
                    break;
                }
                file++;
                counter++;
            }
            //Side to move
            counter++;
            switch (fen[counter]) {
                case 'w':
                    side_to_move = WHITE;
                    break;
                case 'b':
                    side_to_move = BLACK;
                    break;
                default:
                    break;
            }
            if (side_to_move == BLACK) internal_hash ^= atlas.z_side;
            //Castling rights
            counter = counter + 2;
            while (fen[counter] != ' ') {
                switch (fen[counter]) {
                    case 'K':
                        castling_rights ^= (1ULL << 1);
                        break;
                    case 'Q':
                        castling_rights ^= (1ULL << 2);
                        break;
                    case 'k':
                        castling_rights ^= (1ULL << 3);
                        break;
                    case 'q':
                        castling_rights ^= (1ULL << 4);
                        break;
                    default:
                        break;
                }
                counter++;
            }
            internal_hash ^= atlas.z_castling[castling_rights];
            //En Passant
            counter++;
            if (fen[counter] == '-') {
                en_passant_square = 64;
                counter++;
            } else {
                file = fen[counter++] - 'a';
                rank = fen[counter++] - '1';
                en_passant_square = rank * 8 + file;
                internal_hash ^= atlas.z_ep[file];
            }   
        }
        
        inline bool make_move(const Move move) noexcept {
            const Square from = get_from(move);
            const Square to = get_to(move);
            const std::uint8_t flag = get_flag(move);
            const Piece moving_piece = piece_on_square[from];
            
            history[game_ply].castling_rights = castling_rights;
            history[game_ply].en_passant_square = en_passant_square;
            history[game_ply].captured_piece = piece_on_square[to];

            en_passant_square = 64;

            switch (flag) {
                case SILENT_MOVE: 
                    move_piece(from, to);
                    break;
                
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
                    history[game_ply].captured_piece = (side_to_move == WHITE) ? BLACK_PAWN : WHITE_PAWN;
                    remove_piece((to ^ 8ULL), Color(side_to_move ^ 1ULL));
                    move_piece(from, to);
                    break;

                case CAPTURE:
                    capture(from, to);
                    break;
                
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
            
            if (history[game_ply].en_passant_square != 64) internal_hash ^= atlas.z_ep[history[game_ply].en_passant_square];
            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square % 8];
            castling_rights &= (castling_mask[from] & castling_mask[to]);
            internal_hash ^= atlas.z_castling[castling_rights] ^ atlas.z_castling[history[game_ply].castling_rights];
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_side;
            game_ply++;
            
            if (is_square_attacked(king_square[Color(side_to_move ^ 1ULL)], side_to_move)) {
                unmake_move(move);
                return false;
            }

            return true;
        }

        inline void unmake_move(const Move move) noexcept {
            game_ply--;

            const Square from = get_from(move);
            const Square to = get_to(move);
            const std::uint8_t flag = get_flag(move);
            const Piece moving_piece = piece_on_square[to];

            internal_hash ^= atlas.z_side;
            side_to_move = Color(side_to_move ^ 1ULL);
            internal_hash ^= atlas.z_castling[castling_rights] ^ atlas.z_castling[history[game_ply].castling_rights];
            castling_rights = history[game_ply].castling_rights;

            const Square offset = (side_to_move == WHITE) ? 0 : 6;
            king_square[side_to_move] = (moving_piece == (WHITE_KING + offset)) ? from : king_square[side_to_move]; 

            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square % 8];
            en_passant_square = history[game_ply].en_passant_square;
            if (en_passant_square != 64) internal_hash ^= atlas.z_ep[en_passant_square % 8];

            switch (flag)
            {
            case CAPTURE:
                move_piece(to, from);
                put_piece(to, history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));
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
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);
                break;

            case BISHOP_PROMOTION:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);
                break;

            case ROOK_PROMOTION:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);                  
                break;

            case QUEEN_PROMOTION:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);                    
                break;

            case KNIGHT_PROMOTION_AND_CAPTURE:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);                    
                put_piece(to, history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));
                break;

            case BISHOP_PROMOTION_AND_CAPTURE:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);                    
                put_piece(to, history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));                
                break;

            case ROOK_PROMOTION_AND_CAPTURE:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);                    
                put_piece(to, history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));                
                break;

            case QUEEN_PROMOTION_AND_CAPTURE:
                remove_piece(to, side_to_move);
                put_piece(from, (side_to_move == WHITE ? WHITE_PAWN : BLACK_PAWN), side_to_move);                    
                put_piece(to, history[game_ply].captured_piece, Color(side_to_move ^ 1ULL));                
                break;
            
            default:
                move_piece(to, from);
                break;
            }
        }

    private:
        inline void generate_captures(const Square from, MoveList& list, Board board) {
            while (board) {
                Square to = Math::countr_zero(board);
                Piece from_piece = piece_on_square[from];
                Piece to_piece   = piece_on_square[to];
                short victim_value   = std::abs(weight[to_piece]);
                short attacker_value = std::abs(weight[from_piece]);
                short move_score     = (victim_value * 100) - (attacker_value / 10);
                list.add(encode_move(from, to, CAPTURE), move_score);
                board &= (board - 1);
            }
        }

        inline void generate_silents(const Square from, MoveList& list, Board board) {
            while (board) {
                Square to = Math::countr_zero(board); 
                list.add(encode_move(from, to, SILENT_MOVE), 0);
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

                if (isCapturing) {
                    Board captures = attacks & enemy;
                    generate_captures(from, list, captures);
                } else {
                    Board silents = attacks & empty;
                    generate_silents(from, list, silents);
                }               

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

                if (isCapturing) {
                    Board captures = attacks & enemy;
                    generate_captures(from, list, captures);
                } else {
                    Board silents = attacks & empty;
                    generate_silents(from, list, silents);
                } 

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

                if (isCapturing) {
                    Board captures = attacks & enemy;
                    generate_captures(from, list, captures);
                } else {
                    Board silents = attacks & empty;
                    generate_silents(from, list, silents);
                } 

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

                if (isCapturing) {
                    Board captures = attacks & enemy;
                    generate_captures(from, list, captures);
                } else {
                    Board silents = attacks & empty;
                    generate_silents(from, list, silents);
                } 

                queen_board &= (queen_board - 1);
            }
        }

        inline void generate_king_moves(MoveList& list, const bool isCapturing = true) noexcept {
            Square offset = (side_to_move == WHITE) ? 0 : 6;
            Board king_board = boards[WHITE_KING + offset];
            Board enemy = pieces[Color(side_to_move ^ 1ULL)];
            Board empty = ~occupancy;
            std::uint8_t castling_rght = castling_rights;

            Square from = Math::countr_zero(king_board);
            Board attacks = atlas.get_king_attacks(from);

            if (isCapturing) {
                Board captures = attacks & enemy;
                generate_captures(from, list, captures);
            } else {
                offset = (side_to_move == WHITE) ? 0 : 56;
                const Board long_path = (side_to_move == WHITE) ? (1ULL << 1) | (1ULL << 2) | (1ULL << 3) : (1ULL << 57) | (1ULL << 58) | (1ULL << 59);
                const Board short_path = (side_to_move == WHITE) ? (1ULL << 5) | (1ULL << 6) : (1ULL << 61) | (1ULL << 62); 
                if ((castling_rights & (side_to_move == WHITE ? 1 : 4)) && ((empty & short_path) == short_path)) {
                    if (!(is_square_attacked(4 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(5 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(6 + offset, Color(side_to_move ^ 1ULL)))) {
                        list.add(encode_move(from, 6 + offset, CASTLING), 0);
                    }
                }
                if ((castling_rights & (side_to_move == WHITE ? 2 : 8)) && ((empty & long_path) == long_path)) {
                    if (!(is_square_attacked(2 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(3 + offset, Color(side_to_move ^ 1ULL)) || is_square_attacked(4 + offset, Color(side_to_move ^ 1ULL)))) {
                        list.add(encode_move(from, 2 + offset, CASTLING), 0);
                    }
                }
                
                Board silents = attacks & empty;
                generate_silents(from, list, silents);
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
                            short move_score     = (victim_value * 100) - (attacker_value / 10);
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


        inline void score_moves(MoveList& list, Move tt_move) noexcept {
            if (!tt_move) return;
            for (Square i = 0; i < list.count; i++) {
                if (list.moves[i] == tt_move) {
                    list.scores[i] = 30000;
                    return;
                }
            }
        }

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

        inline void check_time() noexcept {
            nodes_visited++;
            if ((nodes_visited & 2047) == 0) {
                if (std::chrono::steady_clock::now() >= end_time_limit) {
                    abort_search = true;
                }
            }
        }

        inline short search(short depth, short alpha, short beta, short ply_from_root) noexcept {
            if (depth == 0) return quiescence(alpha, beta);

            short temp = alpha;
            Board index = internal_hash & ((1 << 20) - 1);

            Move tt_move = 0;

            if (transposition_table[index].key == internal_hash) {
                tt_move = transposition_table[index].best_move;
                if (transposition_table[index].depth >= depth) {
                    short tt_score = transposition_table[index].score;
                    switch (transposition_table[index].flag) {
                        case EXACT:
                            return tt_score;
                            break;
                        case ALPHA:
                            if (tt_score <= alpha)
                            return tt_score;
                            break;
                        case BETA:
                            if (tt_score >= beta)
                            return tt_score;
                            break;   
                    }
                }
            }

            MoveList moves = generate_all_moves();
            score_moves(moves, tt_move);

            short best_score = -32768;
            Move best_move = 0;
            short legal_moves = 0;

            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                if (!make_move(moves.moves[i])) continue;
                legal_moves++;
                short score = -search(depth - 1, -beta, -alpha, ply_from_root + 1);
                unmake_move(moves.moves[i]);
                
                if (abort_search) return 0;
                check_time();

                if (score > best_score) {
                    best_score = score;
                    best_move = moves.moves[i];
                }
                if (score > alpha) alpha = score;
            }
            if (legal_moves == 0) {
                if (is_square_attacked(king_square[side_to_move], Color(side_to_move ^ 1ULL)))
                    return -32000 + depth;
                return 0;
            }
            transposition_table[index].key = internal_hash;
            transposition_table[index].best_move = best_move;
            transposition_table[index].score = best_score;
            transposition_table[index].depth = depth;
            transposition_table[index].flag = (best_score >= beta) ? BETA : ((best_score > temp) ? EXACT : ALPHA); 
            transposition_table[index].phase = current_phase;

            return best_score;
        }

        inline short quiescence(short alpha, short beta) {
            short stand_pat = evaluate();
            if (stand_pat >= beta) return beta;
            if (stand_pat > alpha) alpha = stand_pat;

            MoveList moves = generate_all_captures();
            
            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                if (!make_move(moves.moves[i])) continue;
                short score = -quiescence(-beta, -alpha);
                unmake_move(moves.moves[i]);

                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            }
            return alpha;
        }

        inline Move find_best_move(short depth, short alpha, short beta) {
            Move best_move_found = 0;
            short best_score = -32768;
            MoveList moves = generate_all_moves();

            Board index = internal_hash & ((1 << 20) - 1);

            Move tt_move = 0;
            if (transposition_table[index].key == internal_hash) {
                tt_move = transposition_table[index].best_move;
            }
            score_moves(moves, tt_move);

            for (Square i = 0; i < moves.count; i++) {
                pick_move(moves, i);
                if (!make_move(moves.moves[i])) continue;
                short score = -search(depth - 1, -beta, -alpha, 1);
                unmake_move(moves.moves[i]);

                if (abort_search) return best_move_found;
                if (score >= best_score) {
                    best_score = score;
                    best_move_found = moves.moves[i];
                }
                if (score > alpha) {
                    alpha = score;
                }
            }
            return best_move_found;
        }

    public:
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

        Move get_best_move(std::chrono::milliseconds time_limit_ms, short alpha, short beta) {
            abort_search = false;
            nodes_visited = 0;
            short depth = 1;
            Move best_move = 0;
            Move current_depth_best = 0;
            end_time_limit = std::chrono::steady_clock::now() + time_limit_ms;
            
            while (!abort_search && depth <= MAX_DEPTH) {
                current_depth_best = find_best_move(depth, alpha, beta);
                if (!abort_search && current_depth_best != 0) {
                    best_move = current_depth_best;
                }
                depth++;
            }
            return best_move;
        }

        inline short evaluate() const noexcept {
            short phase = current_phase;
            if (phase > 24) phase = 24;
            short score = (mg_score * phase + eg_score * (24 - phase)) / 24;
            return (side_to_move == WHITE) ? score : -score;
        }

        inline void update_weights(double adjustment) noexcept {
            if (side_to_move == BLACK) adjustment = - adjustment;
            
            Square phase = current_phase;
            if (phase > 24) phase = 24;

            double mg_adj = adjustment * ((double)phase / 24.0);
            double eg_adj = adjustment * ((double)(24 - phase) / 24.0);

            for (Square piece = WHITE_PAWN; piece <= BLACK_KING; piece++) {
                Board b = boards[piece];
                while (b) {
                    Square cell = Math::countr_zero(b);

                    PST_Midgame[piece][cell] -= static_cast<short>(mg_adj);
                    PST_Endgame[piece][cell] -= static_cast<short>(eg_adj);

                    weight[piece] -= static_cast<short>(adjustment);

                    b &= (b - 1);
                }
            }
        }

        inline std::vector<Move> get_PV(Square max_length) {
            std::vector<Move> pv;

            for (Square i = 0; i < max_length; i++) {
                Board index = internal_hash & ((1 << 20) - 1);
                if (transposition_table[index].key == internal_hash && transposition_table[index].best_move != 0) {
                    Move tt_move = transposition_table[index].best_move;
                    if (!make_move(tt_move)) {
                        break;
                    }
                    pv.push_back(tt_move);
                } else {
                    break;
                }
            }

            for (Square i = pv.size() - 1; i >= 0; i--) {
                unmake_move(pv[i]);
            }

            return pv;
        }

        inline Piece get_piece(Square cell) const noexcept {
            return piece_on_square[cell];
        }

        inline Color get_side_to_move() const noexcept {
            return side_to_move;
        }
    };
}