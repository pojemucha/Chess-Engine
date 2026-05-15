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
#include <map>

#include "Position.h"
#include "Tuner.h"

using namespace MyChess;

constexpr int WINDOW_WIDTH  = 1400;
constexpr int WINDOW_HEIGHT = 900;
constexpr int BOARD_SIZE    = 800;
constexpr int CELL_SIZE     = BOARD_SIZE / 8;
constexpr int SIDEBAR_WIDTH = 400;  // Increased from 320 for more info
constexpr int SPRITE_SIZE   = 64;   // Approximate sprite dimension

const sf::Color LIGHT_SQUARE(240, 217, 181);
const sf::Color DARK_SQUARE (181, 136, 99);
const sf::Color PANEL_COLOR (40, 40, 45);
const sf::Color DARK_PANEL  (25, 25, 30);
const sf::Color BUTTON_COLOR(65, 65, 75);
const sf::Color BUTTON_HOVER(90, 90, 100);
const sf::Color BUTTON_ACTIVE(120, 80, 180);
const sf::Color TEXT_COLOR  (230, 230, 230);
const sf::Color ACCENT_COLOR(200, 150, 80);

// ============================================================
// SPRITE CACHE
// ============================================================

struct SpriteCache {
    std::map<std::string, sf::Texture> textures;
    
    bool load_piece_sprites() {
        const char* piece_files[] = {
            "GUI/assets/sprites/wP.png", "GUI/assets/sprites/wN.png", "GUI/assets/sprites/wB.png",
            "GUI/assets/sprites/wR.png", "GUI/assets/sprites/wQ.png", "GUI/assets/sprites/wK.png",
            "GUI/assets/sprites/bP.png", "GUI/assets/sprites/bN.png", "GUI/assets/sprites/bB.png",
            "GUI/assets/sprites/bR.png", "GUI/assets/sprites/bQ.png", "GUI/assets/sprites/bK.png"
        };
        
        for (const auto* file : piece_files) {
            sf::Texture tex;
            if (!tex.loadFromFile(file)) {
                std::cerr << "Failed to load sprite: " << file << std::endl;
                return false;
            }
            textures[file] = tex;
        }
        return true;
    }
    
    sf::Texture* get_piece_texture(Piece piece) {
        const char* piece_to_file[] = {
            "GUI/assets/sprites/wP.png", "GUI/assets/sprites/wN.png", "GUI/assets/sprites/wB.png",
            "GUI/assets/sprites/wR.png", "GUI/assets/sprites/wQ.png", "GUI/assets/sprites/wK.png",
            "GUI/assets/sprites/bP.png", "GUI/assets/sprites/bN.png", "GUI/assets/sprites/bB.png",
            "GUI/assets/sprites/bR.png", "GUI/assets/sprites/bQ.png", "GUI/assets/sprites/bK.png"
        };
        
        if (piece >= EMPTY) return nullptr;
        auto it = textures.find(piece_to_file[piece]);
        return it != textures.end() ? &it->second : nullptr;
    }
};

// ============================================================
// AUDIO SYSTEM (Placeholder structure for sound effects)
// ============================================================

struct AudioManager {
    // NOTE: Sound files needed (add to GUI/assets/sounds/):
    // - move.wav: Standard piece move sound (brief beep/click)
    // - capture.wav: Piece capture sound (longer/heavier sound)
    // - check.wav: Check or checkmate warning sound (alarm-like)
    
    bool audio_enabled = true;
    
    void play_move_sound() {
        // TODO: Implement when move.wav is available
        // Sound effect for regular moves
    }
    
    void play_capture_sound() {
        // TODO: Implement when capture.wav is available
        // Sound effect for captures
    }
    
    void play_check_sound() {
        // TODO: Implement when check.wav is available
        // Alert sound for check/checkmate
    }
    
    void disable_audio() { audio_enabled = false; }
    void enable_audio() { audio_enabled = true; }
};

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
    std::vector<std::string> best_move_sequence;  // Top 3 moves found by engine
    std::string game_status = "";
    bool game_over = false;
    bool engine_searching = false;  // Don't apply moves while engine thinks
    short eval_score = 0;  // Engine evaluation
    short search_depth = 0;  // Current search depth
};

// ============================================================
// BUTTON
// ============================================================

struct Button {
    sf::RectangleShape shape;
    sf::Text text;
    bool hovered = false;

    bool contains(sf::Vector2f point) const {
        return shape.getGlobalBounds().contains(point);
    }

    void update_hover(sf::Vector2f mouse) {
        hovered = contains(mouse);
    }

    void draw(sf::RenderWindow& window) {
        if (hovered) {
            shape.setFillColor(BUTTON_HOVER);
        } else {
            shape.setFillColor(BUTTON_COLOR);
        }
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
    button.shape.setOutlineColor(sf::Color(200, 150, 80));

    button.text.setFont(font);
    button.text.setString(label);
    button.text.setCharacterSize(text_size);
    button.text.setFillColor(TEXT_COLOR);

    auto bounds = button.text.getLocalBounds();
    button.text.setPosition(
        x + (width - bounds.width) / 2.f - bounds.left,
        y + (height - bounds.height) / 2.f - bounds.top - 5.f
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
    input.box.setFillColor(sf::Color(50, 50, 55));
    input.box.setOutlineThickness(2.f);
    input.box.setOutlineColor(ACCENT_COLOR);

    input.text.setFont(font);
    input.text.setCharacterSize(24);
    input.text.setFillColor(TEXT_COLOR);
    input.text.setPosition(x + 10, y + 5);

    return input;
}

// ============================================================
// BOARD RENDERING
// ============================================================

void draw_board(sf::RenderWindow& window) {
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            sf::RectangleShape cell(sf::Vector2f(CELL_SIZE, CELL_SIZE));
            cell.setPosition(file * CELL_SIZE, rank * CELL_SIZE);
            
            bool is_light = (rank + file) % 2 == 0;
            cell.setFillColor(is_light ? LIGHT_SQUARE : DARK_SQUARE);
            
            window.draw(cell);
        }
    }
}

sf::Vector2i square_to_screen(int square) {
    int rank = 7 - square / 8;
    int file = square % 8;

    return {
        file * CELL_SIZE,
        rank * CELL_SIZE
    };
}

// ============================================================
// PIECE RENDERING WITH SPRITES
// ============================================================

void draw_pieces(sf::RenderWindow& window, const Position& pos, SpriteCache& sprite_cache) {
    for (int square = 0; square < 64; square++) {
        Piece piece = pos.get_piece(square);
        
        if (piece == EMPTY) continue;
        
        sf::Texture* texture = sprite_cache.get_piece_texture(piece);
        if (!texture) continue;
        
        sf::Vector2i screen_pos = square_to_screen(square);
        
        sf::Sprite sprite(*texture);
        sprite.setScale(0.9f, 0.9f);
        
        // Center sprite on square
        sf::FloatRect sprite_bounds = sprite.getLocalBounds();
        sprite.setPosition(
            screen_pos.x + (CELL_SIZE - sprite_bounds.width * 0.9f) / 2.f,
            screen_pos.y + (CELL_SIZE - sprite_bounds.height * 0.9f) / 2.f
        );
        
        window.draw(sprite);
    }
}

// ============================================================
// LEGAL MOVES CALCULATION (WITH SAFE BOUNDS CHECKING)
// ============================================================

std::vector<int> get_legal_moves_from_square(Position& pos, int square) {
    std::vector<int> legal_moves;
    
    if (square < 0 || square > 63) return legal_moves;
    
    Piece piece = pos.get_piece(square);
    if (piece == EMPTY) return legal_moves;
    
    Color piece_color = (piece < 6) ? WHITE : BLACK;
    if (piece_color != pos.get_side_to_move()) return legal_moves;
    
    MoveList moves = pos.generate_all_moves();
    
    // CRITICAL: Check count to avoid out-of-bounds access
    if (moves.count < 0 || moves.count > 256) {
        std::cerr << "Invalid move count: " << moves.count << std::endl;
        return legal_moves;
    }
    
    for (Square i = 0; i < moves.count; i++) {
        if (i >= 256) break; // Safety check
        
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

// ============================================================
// LEGAL MOVES VISUALIZATION
// ============================================================

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

// ============================================================
// BEST MOVE ARROW
// ============================================================

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

// ============================================================
// MOVE VALIDATION & CONVERSION
// ============================================================

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

    MoveList moves = pos.generate_all_moves();
    
    if (moves.count < 0 || moves.count > 256) {
        return false;
    }

    for (Square i = 0; i < moves.count; i++) {
        if (i >= 256) break;
        
        int move_from = moves.moves[i] & 0x3F;
        int move_to = (moves.moves[i] >> 6) & 0x3F;

        if (move_from == from && move_to == to) {
            return true;
        }
    }

    return false;
}

// ============================================================
// MENU RENDERING (IMPROVED DESIGN)
// ============================================================

void draw_menu_background(sf::RenderWindow& window) {
    // Gradient background effect with rectangles
    sf::RectangleShape bg_top(sf::Vector2f(WINDOW_WIDTH, WINDOW_HEIGHT / 2));
    bg_top.setFillColor(sf::Color(35, 35, 45));
    window.draw(bg_top);
    
    sf::RectangleShape bg_bottom(sf::Vector2f(WINDOW_WIDTH, WINDOW_HEIGHT / 2));
    bg_bottom.setPosition(0, WINDOW_HEIGHT / 2);
    bg_bottom.setFillColor(sf::Color(40, 40, 50));
    window.draw(bg_bottom);
}

void draw_menu_title(sf::RenderWindow& window, const sf::Font& font) {
    sf::Text title;
    title.setFont(font);
    title.setString("CHESS ENGINE");
    title.setCharacterSize(80);
    title.setFillColor(ACCENT_COLOR);
    
    auto bounds = title.getLocalBounds();
    title.setPosition(
        (WINDOW_WIDTH - bounds.width) / 2.f,
        50.f
    );
    
    window.draw(title);
    
    // Subtitle
    sf::Text subtitle;
    subtitle.setFont(font);
    subtitle.setString("with GUI Visualization");
    subtitle.setCharacterSize(24);
    subtitle.setFillColor(sf::Color(150, 150, 150));
    
    auto sub_bounds = subtitle.getLocalBounds();
    subtitle.setPosition(
        (WINDOW_WIDTH - sub_bounds.width) / 2.f,
        140.f
    );
    
    window.draw(subtitle);
}

// ============================================================
// MAIN PROGRAM
// ============================================================

int main() {

    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Chess Engine");
    window.setFramerateLimit(60);

    if (!window.isOpen()) {
        std::cout << "Failed to create window" << std::endl;
        return -1;
    }

    if (!window.isOpen()) {
        std::cout << "Could not load font" << std::endl;
        return -1;
    }

    sf::Font font;
    if (!font.loadFromFile("GUI/assets/fonts/Roboto-Regular.ttf")) {
        std::cout << "Could not load font" << std::endl;
        return -1;
    }

    // ========================================================
    // SPRITE CACHE
    // ========================================================

    SpriteCache sprite_cache;
    if (!sprite_cache.load_piece_sprites()) {
        std::cerr << "Failed to load piece sprites!" << std::endl;
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

    Button btn_play = create_button(font, "Play", 520, 280, 360, 70);

    Button btn_settings = create_button(font, "Settings", 520, 380, 360, 70);

    Button btn_load_fen = create_button(font, "Load FEN", 520, 480, 360, 70);

    Button btn_exit = create_button(font, "Exit", 520, 580, 360, 70);

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

            Move best_move = 0;
            short best_score = 0;

            try {
                // Use time-limited search: 3 seconds
                best_move = board.get_best_move(std::chrono::milliseconds(3000), -30000, 30000);
            } catch (...) {
                std::cout << "Engine error" << std::endl;
            }

            {
                std::lock_guard<std::mutex> lock(engine_mutex);
                engine_best_move = best_move;
                if (best_move != 0) {
                    engine_info = "Move: " + move_to_string(best_move);
                } else {
                    engine_info = "Thinking...";
                }
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

            // =================================================
            // TEXT INPUT
            // =================================================

            if (event.type == sf::Event::TextEntered) {

                if (fen_input.active) {
                    if (event.text.unicode == 8) {
                        if (!fen_input.value.empty()) {
                            fen_input.value.pop_back();
                        }
                    } else if (event.text.unicode == 13) {
                        // Enter key
                    } else if (event.text.unicode < 128) {
                        fen_input.value += static_cast<char>(event.text.unicode);
                    }
                    fen_input.text.setString(fen_input.value);
                }

                if (dataset_input.active) {
                    if (event.text.unicode == 8) {
                        if (!dataset_input.value.empty()) {
                            dataset_input.value.pop_back();
                        }
                    } else if (event.text.unicode == 13) {
                        // Enter key
                    } else if (event.text.unicode < 128) {
                        dataset_input.value += static_cast<char>(event.text.unicode);
                    }
                    dataset_input.text.setString(dataset_input.value);
                }
            }

            // =================================================
            // MOUSE CLICK
            // =================================================

            if (event.type == sf::Event::MouseButtonPressed) {

                sf::Vector2f mouse = window.mapPixelToCoords(sf::Mouse::getPosition(window));

                // =================================================
                // MENU
                // =================================================

                if (game_state == GameState::MENU) {

                    if (btn_play.contains(mouse)) {
                        game_state = GameState::SIDE_SELECT;
                    }

                    if (btn_settings.contains(mouse)) {
                        game_state = GameState::SETTINGS;
                    }

                    if (btn_load_fen.contains(mouse)) {
                        game_state = GameState::FEN_INPUT;
                        fen_input.value = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
                    }

                    if (btn_exit.contains(mouse)) {
                        window.close();
                    }
                }

                // =================================================
                // SIDE SELECT
                // =================================================

                else if (game_state == GameState::SIDE_SELECT) {

                    if (btn_white.contains(mouse)) {
                        human_is_white = true;
                        ui.game_over = false;
                        ui.game_status = "";
                        ui.move_history.clear();
                        ui.selected_square = -1;
                        game_state = GameState::PLAYING;
                    }

                    if (btn_black.contains(mouse)) {
                        human_is_white = false;
                        ui.game_over = false;
                        ui.game_status = "";
                        ui.move_history.clear();
                        ui.selected_square = -1;
                        game_state = GameState::PLAYING;
                        
                        start_engine();
                    }

                    if (btn_back.contains(mouse)) {
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
                        // Tuner functionality - load dataset and train
                        Tuner tuner;
                        // tuner.train(100, 0.5);
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
                                    
                                    if (moves.count >= 0 && moves.count <= 256) {
                                        for (Square i = 0; i < moves.count; i++) {
                                            if (i >= 256) break;
                                            
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
                                }
                            } else {
                                ui.selected_square = clicked_square;
                                ui.legal_moves_from_selection = get_legal_moves_from_square(board, clicked_square);
                            }
                        }
                    }
                }
            }

            // =================================================
            // MOUSE MOVE (FOR HOVER EFFECTS)
            // =================================================

            if (event.type == sf::Event::MouseMoved) {
                sf::Vector2f mouse = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                
                btn_play.update_hover(mouse);
                btn_settings.update_hover(mouse);
                btn_load_fen.update_hover(mouse);
                btn_exit.update_hover(mouse);
                btn_white.update_hover(mouse);
                btn_black.update_hover(mouse);
                btn_back.update_hover(mouse);
                btn_apply_fen.update_hover(mouse);
                btn_train.update_hover(mouse);
                btn_toggle_sidebar.update_hover(mouse);
            }
        }

        window.clear();

        // ========================================================
        // MENU
        // ========================================================

        if (game_state == GameState::MENU) {

            draw_menu_background(window);
            draw_menu_title(window, font);

            btn_play.draw(window);
            btn_settings.draw(window);
            btn_load_fen.draw(window);
            btn_exit.draw(window);
        }

        // ========================================================
        // SIDE SELECT
        // ========================================================

        else if (game_state == GameState::SIDE_SELECT) {

            draw_menu_background(window);

            sf::Text title;
            title.setFont(font);
            title.setString("Choose Your Side");
            title.setCharacterSize(60);
            title.setFillColor(ACCENT_COLOR);
            
            auto bounds = title.getLocalBounds();
            title.setPosition(
                (WINDOW_WIDTH - bounds.width) / 2.f,
                100.f
            );

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
            title.setString("Settings");
            title.setPosition(540, 180);

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
            draw_pieces(window, board, sprite_cache);
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
                sidebar.setFillColor(DARK_PANEL);

                window.draw(sidebar);

                // Engine Info & Best Moves
                sf::Text engine_text;
                engine_text.setFont(font);
                engine_text.setCharacterSize(14);
                engine_text.setFillColor(ACCENT_COLOR);

                {
                    std::lock_guard<std::mutex> lock(engine_mutex);
                    engine_text.setString(engine_info);
                }

                engine_text.setPosition(BOARD_SIZE + 15, 15);
                window.draw(engine_text);

                // Best Move Sequence
                if (!ui.best_move_sequence.empty()) {
                    sf::Text best_moves_text;
                    best_moves_text.setFont(font);
                    best_moves_text.setCharacterSize(12);
                    best_moves_text.setFillColor(TEXT_COLOR);
                    
                    std::string best_line = "Best: ";
                    for (size_t i = 0; i < std::min(size_t(3), ui.best_move_sequence.size()); i++) {
                        best_line += ui.best_move_sequence[i];
                        if (i < ui.best_move_sequence.size() - 1) best_line += " -> ";
                    }
                    
                    best_moves_text.setString(best_line);
                    best_moves_text.setPosition(BOARD_SIZE + 15, 70);
                    window.draw(best_moves_text);
                }

                // Evaluation Score
                sf::Text eval_text;
                eval_text.setFont(font);
                eval_text.setCharacterSize(12);
                eval_text.setFillColor(sf::Color(100, 200, 100));
                eval_text.setString("Eval: " + std::to_string(ui.eval_score / 100) + "." + 
                                   std::to_string(std::abs(ui.eval_score % 100)));
                eval_text.setPosition(BOARD_SIZE + 15, 100);
                window.draw(eval_text);

                // Move History (Vertical display)
                sf::Text move_header;
                move_header.setFont(font);
                move_header.setCharacterSize(14);
                move_header.setFillColor(ACCENT_COLOR);
                move_header.setString("Move History:");
                move_header.setPosition(BOARD_SIZE + 15, 140);
                window.draw(move_header);
                
                sf::Text history_text;
                history_text.setFont(font);
                history_text.setCharacterSize(13);
                history_text.setFillColor(sf::Color(180, 180, 180));
                
                std::stringstream history_ss;
                for (size_t i = 0; i < ui.move_history.size(); i++) {
                    history_ss << std::to_string(i + 1) << ". " << ui.move_history[i] << "\n";
                }
                
                history_text.setString(history_ss.str());
                history_text.setPosition(BOARD_SIZE + 15, 165);
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
