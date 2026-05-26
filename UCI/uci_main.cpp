/**
 * @file uci_main.cpp
 * @brief Universal Chess Interface (UCI) implementation for MyChess.
 * 
 * This module acts as a console wrapper around the chess engine. 
 * It listens to standard input (stdin) for UCI commands sent by a GUI 
 * (like CuteChess, Arena, or Lichess) and dispatches search tasks to a background thread.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <vector>
#include <thread>
#include <memory>
#include <chrono>
#include <algorithm>
#include <cctype>

#include "Position.h"
#include "WeightsIO.h"

using namespace MyChess;

// --- Global Engine State ---
Position engine_pos("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
Search engine_search;
std::unique_ptr<std::thread> search_thread = nullptr;

/**
 * @brief Converts a UCI coordinate string (e.g., "e2e4", "e7e8q") into the engine's 16-bit Move format.
 * @param pos The current board position.
 * @param token The UCI move string.
 * @return The encoded Move, or 0 if the move is invalid/illegal.
 */
Move parse_uci_move(Position& pos, const std::string& token) {
    if (token.length() < 4) return 0;

    auto valid_sq = [](int file, int rank) {
        return file >= 0 && file < 8 && rank >= 0 && rank < 8;
    };

    MoveList moves = pos.generate_all_moves();
    int from_file = token[0] - 'a';
    int from_rank = token[1] - '1';
    int to_file   = token[2] - 'a';
    int to_rank   = token[3] - '1';
    if (!valid_sq(from_file, from_rank) || !valid_sq(to_file, to_rank)) return 0;

    Square from = from_rank * 8 + from_file;
    Square to   = to_rank * 8 + to_file;

    for (int i = 0; i < moves.count; i++) {
        Move m = moves.moves[i];
        if (get_from(m) == from && get_to(m) == to) {
            
            // Check if the move is a promotion
            if (token.length() == 5) {
                char promo = token[4];
                std::uint8_t flag = get_flag(m);
                
                bool is_q = (flag == QUEEN_PROMOTION || flag == QUEEN_PROMOTION_AND_CAPTURE);
                bool is_r = (flag == ROOK_PROMOTION  || flag == ROOK_PROMOTION_AND_CAPTURE);
                bool is_b = (flag == BISHOP_PROMOTION|| flag == BISHOP_PROMOTION_AND_CAPTURE);
                bool is_n = (flag == KNIGHT_PROMOTION|| flag == KNIGHT_PROMOTION_AND_CAPTURE);

                if ((promo == 'q' && is_q) || (promo == 'r' && is_r) ||
                    (promo == 'b' && is_b) || (promo == 'n' && is_n)) {
                    return m;
                }
                continue; // Squares match, but wrong promotion piece selected
            }
            return m;
        }
    }
    return 0; // Illegal move
}

/**
 * @brief Formats a 16-bit engine move back into a standard UCI string.
 */
std::string format_uci_move(Move move) {
    if (move == 0) return "0000"; // Null move (game over)
    
    int from = get_from(move);
    int to = get_to(move);
    std::string promo = "";
    std::uint8_t flag = get_flag(move);
    
    if (flag == QUEEN_PROMOTION  || flag == QUEEN_PROMOTION_AND_CAPTURE) promo = "q";
    else if (flag == ROOK_PROMOTION   || flag == ROOK_PROMOTION_AND_CAPTURE) promo = "r";
    else if (flag == BISHOP_PROMOTION || flag == BISHOP_PROMOTION_AND_CAPTURE) promo = "b";
    else if (flag == KNIGHT_PROMOTION || flag == KNIGHT_PROMOTION_AND_CAPTURE) promo = "n";

    std::string result = "";
    result += (char)('a' + (from % 8));
    result += (char)('1' + (from / 8));
    result += (char)('a' + (to % 8));
    result += (char)('1' + (to / 8));
    result += promo;
    return result;
}

std::string format_score(short score) {
    if (score > 31000) {
        int mate_in = (32000 - score + 1) / 2;
        return "mate " + std::to_string(mate_in);
    } else if (score < -31000) {
        int mate_in = (32000 - std::abs(score) + 1) / 2;
        return "mate -" + std::to_string(mate_in);
    }
    return "cp " + std::to_string(score);
}

/**
 * @brief Thread worker that executes the search.
 * Runs in the background so the main thread can continue parsing "stop" or "quit" commands.
 */
void search_worker(int time_ms, short target_depth, long long target_nodes) {
    short out_score = 0;    
    engine_search.on_iteration_end = [](short d, short score, long long n, long long t, const std::vector<Move>& pv) {
        std::cout << "info depth " << d 
                  << " score " << format_score(score) 
                  << " nodes " << n 
                  << " time " << t 
                  << " pv";
        for (Move m : pv) {
            std::cout << " " << format_uci_move(m);
        }
        std::cout << std::endl;
    };
    Move best_move = engine_search.get_best_move(engine_pos, time_ms, target_depth, target_nodes, -32000, 32000, out_score);
    std::cout << "bestmove " << format_uci_move(best_move) << std::endl;
}

/**
 * @brief Calculates the time allocated for the current move.
 */
int calculate_time(Color side, int wtime, int btime, int winc, int binc, int movestogo) {
    int time_left = (side == WHITE) ? wtime : btime;
    int inc = (side == WHITE) ? winc : binc;

    if (time_left < 0) return 3000; // Fallback to 3 seconds if time is unspecified

    // Basic time management: Assume the game will last 40 more moves
    if (movestogo == 0) movestogo = 40; 
    
    int time_for_move = (time_left / movestogo) + (inc / 2);
    
    // Safety margins to prevent flagging (losing by time)
    if (time_for_move >= time_left) time_for_move = time_left - 500;
    if (time_for_move < 50) time_for_move = 50; // Minimum 50ms to guarantee at least depth 1

    return time_for_move;
}

uint64_t perft(Position& pos, int depth) {
    if (depth == 0) return 1ULL;
    MoveList list = pos.generate_all_moves();
    uint64_t nodes = 0;
    for (int i = 0; i < list.count; i++) {
        if (!pos.make_move(list.moves[i])) continue;
        nodes += perft(pos, depth - 1);
        pos.unmake_move(list.moves[i]);
    }
    return nodes;
}

int main() {
    // Unbuffered I/O is critical for UCI. It prevents the OS from delaying output.
    std::setbuf(stdin, NULL);
    std::setbuf(stdout, NULL);

    // Load tuned weights if they exist
    if (!WeightsIO::load_weights("trained_weights.bin")) {
        WeightsIO::reset_to_defaults();
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string token;
        iss >> token;

        if (token == "uci") {
            std::cout << "MyChess\n";
            std::cout << "pojemucha\n";
            std::cout << "uciok\n" << std::flush;
        }
        else if (token == "isready") {
            std::cout << "readyok\n" << std::flush;
        }
        else if (token == "ucinewgame") {
            engine_search.clear_heuristics();
        }
        else if (token == "position") {
            std::string mode;
            iss >> mode;

            if (mode == "startpos") {
                engine_pos.parse_FEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
            } else if (mode == "fen") {
                std::string fen_part = "";
                std::string t;
                for (int i = 0; i < 6 && (iss >> t) && t != "moves"; i++) {
                    fen_part += t + " ";
                }
                engine_pos.parse_FEN(fen_part);
            }
            
            std::string temp;
            while (iss >> temp) {
                if (temp == "moves") {
                    while (iss >> temp) {
                        Move m = parse_uci_move(engine_pos, temp);
                        if (m != 0) engine_pos.make_move(m);
                    }
                    break;
                }
            }
            continue;
        }
        else if (token == "go") {
            int wtime = -1, btime = -1, winc = 0, binc = 0, movestogo = 40;
            int movetime = -1;
            short depth = -1;
            long long nodes = -1;
            bool infinite = false;

            std::string param;
            while (iss >> param) {
                if (param == "wtime") iss >> wtime;
                else if (param == "btime") iss >> btime;
                else if (param == "winc") iss >> winc;
                else if (param == "binc") iss >> binc;
                else if (param == "movestogo") iss >> movestogo;
                else if (param == "movetime") iss >> movetime;
                else if (param == "depth") iss >> depth;
                else if (param == "nodes") iss >> nodes;
                else if (param == "infinite") infinite = true;
            }

            int time_for_move = -1; 
            if (movetime != -1) {
                time_for_move = movetime; // GUI forced an exact time limit
            } else if (wtime != -1 || btime != -1) {
                time_for_move = calculate_time(engine_pos.get_side_to_move(), wtime, btime, winc, binc, movestogo);
            } else if (!infinite && depth == -1 && nodes == -1) {
                time_for_move = 3000;
            }

            // Clean up the previous search thread if it exists
            if (search_thread && search_thread->joinable()) {
                engine_search.stop();
                search_thread->join();
            }

            // Launch the search asynchronously
            search_thread = std::make_unique<std::thread>(search_worker, time_for_move, depth, nodes);
        }
        else if (token == "stop") {
            engine_search.stop();
            if (search_thread && search_thread->joinable()) {
                search_thread->join();
            }
        }
        else if (token == "quit") {
            engine_search.stop();
            if (search_thread && search_thread->joinable()) {
                search_thread->join();
            }
            break;
        }
        else if (token == "perft") {
            int d;
            iss >> d;
            auto start = std::chrono::steady_clock::now();
            uint64_t nodes = perft(engine_pos, d);
            auto end = std::chrono::steady_clock::now();
            auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            std::cout << "Nodes: " << nodes << " Time: " << diff << "ms" << std::endl;
        }
    }
    
    return 0;
}