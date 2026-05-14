#include <SFML/Graphics.hpp>
#include <SFML/System.hpp>
#include <SFML/Window.hpp>

#include <thread>
#include <mutex>
#include <atomic>
#include <sstream>
#include <fstream>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <memory>

#include "Position.h"
#include "Tuner.h"

using namespace MyChess;

constexpr int WINDOW_WIDTH  = 1400;
constexpr int WINDOW_HEIGHT = 900;
constexpr int BOARD_SIZE    = 800;
constexpr int CELL_SIZE     = BOARD_SIZE / 8;
constexpr int SIDEBAR_WIDTH = 320;

const sf::Color LIGHT_SQUARE(240, 217, 181);
const sf::Color DARK_SQUARE (181, 136, 99);
const sf::Color PANEL_COLOR (40, 40, 45);
const sf::Color BUTTON_COLOR(65, 65, 75);
const sf::Color HOVER_COLOR (90, 90, 100);
const sf::Color TEXT_COLOR  (230, 230, 230);

// ============================================================
// UI STATE
// ============================================================

enum class GameState {
    MENU,
    SIDE_SELECT,
    SETTINGS,
    FEN_INPUT,
    PLAYING,
    ANALYSIS
};

struct UIState {
    std::string fen_input;
    std::string dataset_path;

    bool editing_fen  = false;
    bool editing_path = false;

    bool sidebar_open = true;
    
    int selected_square = -1;
    std::vector<int> legal_moves_from_selection;
    std::vector<std::string> move_history;
    std::string game_status = "";
    bool game_over = false;
};

// ============================================================
// BUTTON
// ============================================================

struct Button {
    sf::RectangleShape shape;
    sf::Text text;

    bool contains(sf::Vector2f point) const {
        return shape.getGlobalBounds().contains(point);
    }

    void draw(sf::RenderWindow& window) {
        window.draw(shape);
        window.draw(text);
    }
};

Button create_button(
    const sf::Font& font,
    const std::string& label,
    float x,
    float y,
    float width,
    float height,
    unsigned int text_size = 28
) {
    Button button;

    button.shape.setPosition(x, y);
    button.shape.setSize({width, height});
    button.shape.setFillColor(BUTTON_COLOR);
    button.shape.setOutlineThickness(2.f);
    button.shape.setOutlineColor(sf::Color::Black);

    button.text.setFont(font);
    button.text.setString(label);
    button.text.setCharacterSize(text_size);
    button.text.setFillColor(TEXT_COLOR);

    sf::FloatRect bounds = button.text.getLocalBounds();

    button.text.setPosition(
        x + (width - bounds.width) / 2.f,
        y + (height - bounds.height) / 2.f - 6.f
    );

    return button;
}

// ============================================================
// TEXT INPUT
// ============================================================

struct TextInput {
    sf::RectangleShape box;
    sf::Text text;
    std::string value;
    bool active = false;

    void draw(sf::RenderWindow& window) {
        window.draw(box);
        window.draw(text);
    }

    void update_text() {
        text.setString(value);
    }
};

TextInput create_input(
    const sf::Font& font,
    float x,
    float y,
    float width,
    float height
) {
    TextInput input;

    input.box.setPosition(x, y);
    input.box.setSize({width, height});
    input.box.setFillColor(sf::Color(55, 55, 60));
    input.box.setOutlineColor(sf::Color::White);
    input.box.setOutlineThickness(2.f);

    input.text.setFont(font);
    input.text.setCharacterSize(24);
    input.text.setFillColor(TEXT_COLOR);
    input.text.setPosition(x + 10.f, y + 8.f);

    return input;
}

// ============================================================
// BOARD HELPERS
// ============================================================

sf::Vector2i square_to_screen(int square) {
    int rank = 7 - square / 8;
    int file = square % 8;

    return {
        file * CELL_SIZE,
        rank * CELL_SIZE
    };
}

// ============================================================
// PIECE RENDERING
// ============================================================

std::string piece_to_string(Piece piece) {
    switch (piece) {
        case WHITE_PAWN:   return "P";
        case WHITE_KNIGHT: return "N";
        case WHITE_BISHOP: return "B";
        case WHITE_ROOK:   return "R";
        case WHITE_QUEEN:  return "Q";
        case WHITE_KING:   return "K";
        case BLACK_PAWN:   return "p";
        case BLACK_KNIGHT: return "n";
        case BLACK_BISHOP: return "b";
        case BLACK_ROOK:   return "r";
        case BLACK_QUEEN:  return "q";
        case BLACK_KING:   return "k";
        default:           return " ";
    }
}

void draw_pieces(sf::RenderWindow& window, const Position& pos, const sf::Font& font) {
    for (int square = 0; square < 64; square++) {
        Piece piece = pos.get_piece(square);
        
        if (piece == EMPTY) continue;
        
        sf::Vector2i screen_pos = square_to_screen(square);
        
        sf::Text piece_text;
        piece_text.setFont(font);
        piece_text.setCharacterSize(56);
        bool is_white = piece < 6;
        piece_text.setFillColor(is_white ? sf::Color::White : sf::Color(100, 100, 100));
        
        piece_text.setString(piece_to_string(piece));
        
        sf::FloatRect bounds = piece_text.getLocalBounds();
        piece_text.setPosition(
            screen_pos.x + (CELL_SIZE - bounds.width) / 2.f - 5.f,
            screen_pos.y + (CELL_SIZE - bounds.height) / 2.f - 15.f
        );
        
        window.draw(piece_text);
    }
}

std::vector<int> get_legal_moves_from_square(Position& pos, int square) {
    std::vector<int> legal_moves;
    
    if (square < 0 || square > 63) return legal_moves;
    
    Piece piece = pos.get_piece(square);
    if (piece == EMPTY) return legal_moves;
    
    Color piece_color = (piece < 6) ? WHITE : BLACK;
    if (piece_color != pos.get_side_to_move()) return legal_moves;
    
    MoveList moves = pos.generate_all_moves();
    
    for (Square i = 0; i < moves.count; i++) {
        int from = moves.moves[i] & 0x3F;
        if (from == square) {
            int to = (moves.moves[i] >> 6) & 0x3F;
            
            Position test_pos = pos;
            if (test_pos.make_move(moves.moves[i])) {
                legal_moves.push_back(to);
            }
        }
    }
    
    return legal_moves;
}

void draw_legal_moves(sf::RenderWindow& window, const std::vector<int>& legal_moves) {
    for (int move_to : legal_moves) {
        sf::Vector2i screen_pos = square_to_screen(move_to);
        
        sf::CircleShape indicator(8.f);
        indicator.setFillColor(sf::Color(100, 200, 100, 100));
        indicator.setPosition(
            screen_pos.x + CELL_SIZE / 2.f - 8.f,
            screen_pos.y + CELL_SIZE / 2.f - 8.f
        );
        
        window.draw(indicator);
    }
}

void draw_best_move_arrow(sf::RenderWindow& window, Move best_move, short evaluation) {
    if (best_move == 0) return;
    
    int from = best_move & 0x3F;
    int to = (best_move >> 6) & 0x3F;
    
    sf::Vector2i from_screen = square_to_screen(from);
    sf::Vector2i to_screen = square_to_screen(to);
    
    sf::Vector2f from_center(
        from_screen.x + CELL_SIZE / 2.f,
        from_screen.y + CELL_SIZE / 2.f
    );
    sf::Vector2f to_center(
        to_screen.x + CELL_SIZE / 2.f,
        to_screen.y + CELL_SIZE / 2.f
    );
    
    sf::Vector2f direction = to_center - from_center;
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    
    if (length < 1.f) return;
    
    direction /= length;
    
    sf::RectangleShape arrow(sf::Vector2f(length, 8.f));
    arrow.setPosition(from_center);
    arrow.setFillColor(sf::Color(255, 200, 0, 150));
    
    float angle = std::atan2(direction.y, direction.x) * 180.f / 3.14159f;
    arrow.setRotation(angle);
    
    window.draw(arrow);
    
    sf::CircleShape arrowhead(6.f, 3);
    arrowhead.setFillColor(sf::Color(255, 200, 0, 200));
    arrowhead.setPosition(to_center.x - 6.f, to_center.y - 6.f);
    arrowhead.setRotation(angle + 90.f);
    window.draw(arrowhead);
}

std::string move_to_string(Move move) {
    int from = move & 0x3F;
    int to   = (move >> 6) & 0x3F;

    char f1 = 'a' + (from % 8);
    char r1 = '1' + (from / 8);

    char f2 = 'a' + (to % 8);
    char r2 = '1' + (to / 8);

    std::string result;

    result += f1;
    result += r1;
    result += f2;
    result += r2;

    return result;
}

bool is_valid_move(Position& pos, int from, int to) {
    if (from < 0 || from > 63 || to < 0 || to > 63) return false;
    
    Piece piece = pos.get_piece(from);
    if (piece == EMPTY) return false;
    
    Color piece_color = (piece < 6) ? WHITE : BLACK;
    if (piece_color != pos.get_side_to_move()) return false;
    
    MoveList moves = pos.generate_all_moves();
    
    for (Square i = 0; i < moves.count; i++) {
        int move_from = moves.moves[i] & 0x3F;
        int move_to = (moves.moves[i] >> 6) & 0x3F;
        
        if (move_from == from && move_to == to) {
            Position test_pos = pos;
            return test_pos.make_move(moves.moves[i]);
        }
    }
    
    return false;
}

std::string build_pv_string(Position& pos) {
    std::stringstream ss;

    auto pv = pos.get_PV(8);

    for (Move m : pv) {
        ss << move_to_string(m) << " ";
    }

    return ss.str();
}

// ============================================================
// TRAINING
// ============================================================

void load_dataset_from_file(const std::string& path, Tuner& tuner) {
    std::ifstream file(path);

    if (!file.is_open()) {
        std::cout << "Could not open dataset: " << path << std::endl;
        return;
    }

    std::string line;

    while (std::getline(file, line)) {
        std::stringstream ss(line);

        std::string fen;
        double result;

        std::getline(ss, fen, ';');
        ss >> result;

        if (!fen.empty()) {
            tuner.add_entry(fen, result);
        }
    }

    std::cout << "Dataset loaded" << std::endl;
}

// ============================================================
// DRAW BOARD
// ============================================================

void draw_board(sf::RenderWindow& window) {
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            sf::RectangleShape square;

            square.setSize({CELL_SIZE, CELL_SIZE});
            square.setPosition(file * CELL_SIZE, rank * CELL_SIZE);

            bool light = (rank + file) % 2 == 0;

            square.setFillColor(light ? LIGHT_SQUARE : DARK_SQUARE);

            window.draw(square);
        }
    }
}

// ============================================================
// MAIN
// ============================================================

int main() {

    sf::RenderWindow window(
        sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT),
        "Chess Engine"
    );

    window.setFramerateLimit(144);

    sf::Font font;

    if (!font.loadFromFile("GUI/assets/fonts/Roboto-Regular.ttf")) {
        std::cout << "Could not load font" << std::endl;
        return -1;
    }

    // ========================================================
    // ENGINE
    // ========================================================

    Position board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");

    bool human_is_white = true;

    std::atomic<bool> engine_thinking = false;

    std::mutex engine_mutex;

    std::string engine_info = "Engine idle";

    Move engine_best_move = 0;

    // ========================================================
    // UI STATE
    // ========================================================

    GameState game_state = GameState::MENU;

    UIState ui;

    // ========================================================
    // BUTTONS
    // ========================================================

    Button btn_play = create_button(font, "Play", 520, 240, 360, 70);

    Button btn_settings = create_button(font, "Settings", 520, 340, 360, 70);

    Button btn_load_fen = create_button(font, "Load FEN", 520, 440, 360, 70);

    Button btn_exit = create_button(font, "Exit", 520, 540, 360, 70);

    Button btn_white = create_button(font, "Play White", 520, 320, 360, 70);

    Button btn_black = create_button(font, "Play Black", 520, 430, 360, 70);

    Button btn_back = create_button(font, "Back", 40, 780, 200, 60);

    Button btn_apply_fen = create_button(font, "Apply FEN", 520, 420, 300, 70);

    Button btn_train = create_button(font, "Train", 520, 420, 300, 70);

    Button btn_toggle_sidebar = create_button(font, "<", BOARD_SIZE, 10, 40, 60, 32);

    // ========================================================
    // INPUTS
    // ========================================================

    TextInput fen_input = create_input(font, 250, 300, 900, 60);

    TextInput dataset_input = create_input(font, 250, 300, 900, 60);

    // ========================================================
    // ENGINE THREAD
    // ========================================================

    std::unique_ptr<std::thread> engine_thread = nullptr;
    long long nodes_searched = 0;

    auto start_engine = [&]() {

        if (engine_thinking) {
            return;
        }

        engine_thinking = true;
        engine_best_move = 0;

        if (engine_thread && engine_thread->joinable()) {
            engine_thread->join();
        }

        engine_thread = std::make_unique<std::thread>([&]() {

            Position copy = board;

            Move best = copy.get_best_move(
                std::chrono::milliseconds(3000),
                -32000,
                32000
            );

            std::string pv = build_pv_string(copy);

            {
                std::lock_guard<std::mutex> lock(engine_mutex);

                engine_best_move = best;

                std::stringstream ss;
                ss << "Evaluation: " << copy.evaluate() << "\n\n";
                ss << "Best move: " << move_to_string(best) << "\n\n";
                ss << "PV:\n" << pv;

                engine_info = ss.str();
            }

            engine_thinking = false;
        });
    };

    // ========================================================
    // MAIN LOOP
    // ========================================================

    while (window.isOpen()) {

        sf::Event event;

        while (window.pollEvent(event)) {

            if (event.type == sf::Event::Closed) {
                window.close();
            }

            // ====================================================
            // TEXT INPUT
            // ====================================================

            if (event.type == sf::Event::TextEntered) {

                if (fen_input.active) {

                    if (event.text.unicode == '\b') {
                        if (!fen_input.value.empty()) {
                            fen_input.value.pop_back();
                        }
                    }
                    else if (event.text.unicode < 128) {
                        fen_input.value += static_cast<char>(event.text.unicode);
                    }

                    fen_input.update_text();
                }

                if (dataset_input.active) {

                    if (event.text.unicode == '\b') {
                        if (!dataset_input.value.empty()) {
                            dataset_input.value.pop_back();
                        }
                    }
                    else if (event.text.unicode < 128) {
                        dataset_input.value += static_cast<char>(event.text.unicode);
                    }

                    dataset_input.update_text();
                }
            }

            // ====================================================
            // MOUSE
            // ====================================================

            if (event.type == sf::Event::MouseButtonPressed) {

                sf::Vector2f mouse = window.mapPixelToCoords(
                    sf::Mouse::getPosition(window)
                );

                // =================================================
                // MENU
                // =================================================

                if (game_state == GameState::MENU) {

                    if (btn_play.contains(mouse)) {
                        game_state = GameState::SIDE_SELECT;
                    }

                    else if (btn_settings.contains(mouse)) {
                        game_state = GameState::SETTINGS;
                    }

                    else if (btn_load_fen.contains(mouse)) {
                        game_state = GameState::FEN_INPUT;
                    }

                    else if (btn_exit.contains(mouse)) {
                        window.close();
                    }
                }

                // =================================================
                // SIDE SELECT
                // =================================================

                else if (game_state == GameState::SIDE_SELECT) {

                    if (btn_white.contains(mouse)) {
                        human_is_white = true;
                        game_state = GameState::PLAYING;
                    }

                    else if (btn_black.contains(mouse)) {
                        human_is_white = false;
                        game_state = GameState::PLAYING;
                        start_engine();
                    }

                    else if (btn_back.contains(mouse)) {
                        game_state = GameState::MENU;
                    }
                }

                // =================================================
                // SETTINGS
                // =================================================

                else if (game_state == GameState::SETTINGS) {

                    dataset_input.active = dataset_input.box
                        .getGlobalBounds()
                        .contains(mouse);

                    if (btn_train.contains(mouse)) {

                        Tuner tuner;

                        load_dataset_from_file(
                            dataset_input.value,
                            tuner
                        );

                        tuner.train(100, 0.5);
                    }

                    else if (btn_back.contains(mouse)) {
                        game_state = GameState::MENU;
                    }
                }

                // =================================================
                // FEN INPUT
                // =================================================

                else if (game_state == GameState::FEN_INPUT) {

                    fen_input.active = fen_input.box
                        .getGlobalBounds()
                        .contains(mouse);

                    if (btn_apply_fen.contains(mouse)) {
                        if (!fen_input.value.empty()) {
                            try {
                                board.parse_FEN(fen_input.value);
                                ui.game_over = false;
                                ui.game_status = "";
                                ui.move_history.clear();
                                ui.selected_square = -1;
                                game_state = GameState::PLAYING;
                                
                                if (!human_is_white) {
                                    start_engine();
                                }
                            } catch (...) {
                                std::cout << "Invalid FEN!" << std::endl;
                            }
                        }
                    }

                    else if (btn_back.contains(mouse)) {
                        game_state = GameState::MENU;
                    }
                }

                // =================================================
                // PLAYING
                // =================================================

                else if (game_state == GameState::PLAYING) {

                    if (btn_toggle_sidebar.contains(mouse)) {
                        ui.sidebar_open = !ui.sidebar_open;
                    }

                    if (!ui.game_over && mouse.x < BOARD_SIZE && mouse.y < BOARD_SIZE) {
                        int file = static_cast<int>(mouse.x / CELL_SIZE);
                        int rank = 7 - static_cast<int>(mouse.y / CELL_SIZE);
                        int clicked_square = rank * 8 + file;

                        if (ui.selected_square == -1) {
                            ui.legal_moves_from_selection = get_legal_moves_from_square(board, clicked_square);
                            if (!ui.legal_moves_from_selection.empty()) {
                                ui.selected_square = clicked_square;
                            }
                        } else {
                            if (clicked_square == ui.selected_square) {
                                ui.selected_square = -1;
                                ui.legal_moves_from_selection.clear();
                            } else if (std::find(ui.legal_moves_from_selection.begin(), 
                                               ui.legal_moves_from_selection.end(), 
                                               clicked_square) != ui.legal_moves_from_selection.end()) {
                                
                                if (is_valid_move(board, ui.selected_square, clicked_square)) {
                                    MoveList moves = board.generate_all_moves();
                                    for (Square i = 0; i < moves.count; i++) {
                                        int from = moves.moves[i] & 0x3F;
                                        int to = (moves.moves[i] >> 6) & 0x3F;
                                        if (from == ui.selected_square && to == clicked_square) {
                                            board.make_move(moves.moves[i]);
                                            ui.move_history.push_back(move_to_string(moves.moves[i]));
                                            
                                            ui.selected_square = -1;
                                            ui.legal_moves_from_selection.clear();
                                            engine_best_move = 0;
                                            
                                            start_engine();
                                            break;
                                        }
                                    }
                                }
                            } else {
                                ui.selected_square = clicked_square;
                                ui.legal_moves_from_selection = get_legal_moves_from_square(board, clicked_square);
                                if (ui.legal_moves_from_selection.empty()) {
                                    ui.selected_square = -1;
                                }
                            }
                        }
                    }
                }
            }
        }

        // ========================================================
        // DRAW
        // ========================================================

        window.clear(sf::Color(25, 25, 30));

        // ========================================================
        // MENU
        // ========================================================

        if (game_state == GameState::MENU) {

            sf::Text title;
            title.setFont(font);
            title.setCharacterSize(60);
            title.setString("Chess Engine");
            title.setPosition(470, 120);

            window.draw(title);

            btn_play.draw(window);
            btn_settings.draw(window);
            btn_load_fen.draw(window);
            btn_exit.draw(window);
        }

        // ========================================================
        // SIDE SELECT
        // ========================================================

        else if (game_state == GameState::SIDE_SELECT) {

            sf::Text title;
            title.setFont(font);
            title.setCharacterSize(50);
            title.setString("Choose Side");
            title.setPosition(500, 180);

            window.draw(title);

            btn_white.draw(window);
            btn_black.draw(window);
            btn_back.draw(window);
        }

        // ========================================================
        // SETTINGS
        // ========================================================

        else if (game_state == GameState::SETTINGS) {

            sf::Text title;
            title.setFont(font);
            title.setCharacterSize(50);
            title.setString("Training Settings");
            title.setPosition(430, 180);

            window.draw(title);

            dataset_input.draw(window);

            btn_train.draw(window);
            btn_back.draw(window);
        }

        // ========================================================
        // FEN INPUT
        // ========================================================

        else if (game_state == GameState::FEN_INPUT) {

            sf::Text title;
            title.setFont(font);
            title.setCharacterSize(50);
            title.setString("Load FEN");
            title.setPosition(540, 180);

            window.draw(title);

            fen_input.draw(window);

            btn_apply_fen.draw(window);
            btn_back.draw(window);
        }

        // ========================================================
        // PLAYING
        // ========================================================

        else if (game_state == GameState::PLAYING) {

            draw_board(window);
            draw_pieces(window, board, font);
            draw_legal_moves(window, ui.legal_moves_from_selection);
            
            {
                std::lock_guard<std::mutex> lock(engine_mutex);
                draw_best_move_arrow(window, engine_best_move, board.evaluate());
            }

            // ====================================================
            // SIDEBAR
            // ====================================================

            if (ui.sidebar_open) {

                sf::RectangleShape sidebar;

                sidebar.setPosition(BOARD_SIZE, 0);
                sidebar.setSize({SIDEBAR_WIDTH, WINDOW_HEIGHT});
                sidebar.setFillColor(PANEL_COLOR);

                window.draw(sidebar);

                // Engine Info
                sf::Text engine_text;
                engine_text.setFont(font);
                engine_text.setCharacterSize(18);
                engine_text.setFillColor(TEXT_COLOR);

                {
                    std::lock_guard<std::mutex> lock(engine_mutex);
                    engine_text.setString(engine_info);
                }

                engine_text.setPosition(BOARD_SIZE + 15, 80);
                window.draw(engine_text);

                // Move History
                sf::Text history_text;
                history_text.setFont(font);
                history_text.setCharacterSize(16);
                history_text.setFillColor(sf::Color(180, 180, 180));
                
                std::stringstream history_ss;
                history_ss << "Moves: ";
                for (size_t i = 0; i < ui.move_history.size(); i++) {
                    history_ss << ui.move_history[i];
                    if (i < ui.move_history.size() - 1) history_ss << " ";
                }
                
                history_text.setString(history_ss.str());
                history_text.setPosition(BOARD_SIZE + 15, 450);
                window.draw(history_text);

                // Game Status
                if (!ui.game_status.empty()) {
                    sf::Text status_text;
                    status_text.setFont(font);
                    status_text.setCharacterSize(20);
                    status_text.setFillColor(sf::Color(255, 100, 100));
                    status_text.setString(ui.game_status);
                    status_text.setPosition(BOARD_SIZE + 15, 550);
                    window.draw(status_text);
                }
            }

            btn_toggle_sidebar.draw(window);
        }

        window.display();
    }

    return 0;
}