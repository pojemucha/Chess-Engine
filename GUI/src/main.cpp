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
#include <cstdio>

#include "Position.h"
#include "Tuner.h"
#include "WeightsIO.h"

using namespace MyChess;

constexpr int WINDOW_WIDTH  = 1400;
constexpr int WINDOW_HEIGHT = 900;
constexpr int BOARD_SIZE    = 800;
constexpr int CELL_SIZE     = BOARD_SIZE / 8;
constexpr int SIDEBAR_WIDTH = WINDOW_WIDTH - BOARD_SIZE;  

const sf::Color LIGHT_SQUARE(240, 217, 181);
const sf::Color DARK_SQUARE (181, 136, 99);
const sf::Color PANEL_COLOR (30, 30, 35);
const sf::Color DARK_PANEL  (25, 25, 30);
const sf::Color BUTTON_COLOR(50, 50, 60);
const sf::Color BUTTON_HOVER(70, 70, 85);
const sf::Color BUTTON_ACTIVE(100, 180, 255);
const sf::Color TEXT_COLOR  (230, 230, 230);
const sf::Color ACCENT_COLOR(100, 180, 255);

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

enum class GameState {
    MENU,
    SIDE_SELECT,
    SETTINGS,
    FEN_INPUT,
    PLAYING,
    PROMOTION_DIALOG
};

struct UIState {
    bool sidebar_open = false;
    float board_offset_x = 200.0f;  // Начальное смещение для центрирования доски
    float target_offset_x = 200.0f;
    int selected_square = -1;
    std::vector<int> legal_moves_from_selection;
    std::vector<std::string> move_history;
    std::vector<std::string> best_move_sequence;
    std::string game_status = "";
    bool game_over = false;
    short eval_score = 0;

    // Promotion dialog state
    int promotion_from = -1;
    int promotion_to = -1;
    std::uint8_t promotion_base_flag = SILENT_MOVE;

    // Last move highlight
    int last_move_from = -1;
    int last_move_to = -1;

    // Animation state
    bool animating = false;
    int anim_from = -1;
    int anim_to = -1;
    Piece anim_piece = EMPTY;
    float anim_progress = 0.0f;
    sf::Clock anim_clock;
};

struct GameSettings {
    int search_time_ms = 3000;

    short alpha = -32000;
    short beta  = 32000;

    bool show_analysis = true;
    bool show_best_move = true;
    bool show_legal_moves = true;

    bool auto_save_weights = true;
    
    void save_to_file(const std::string& filename) {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << "{\n";
            file << "  \"search_time_ms\": " << search_time_ms << ",\n";
            file << "  \"alpha\": " << alpha << ",\n";
            file << "  \"beta\": " << beta << ",\n";
            file << "  \"show_analysis\": " << (show_analysis ? "true" : "false") << ",\n";
            file << "  \"show_best_move\": " << (show_best_move ? "true" : "false") << ",\n";
            file << "  \"show_legal_moves\": " << (show_legal_moves ? "true" : "false") << ",\n";
            file << "  \"auto_save_weights\": " << (auto_save_weights ? "true" : "false") << "\n";
            file << "}\n";
        }
    }
    
    void load_from_file(const std::string& filename) {
        std::ifstream file(filename);
        if (file.is_open()) {
            std::string line;
            while (std::getline(file, line)) {
                if (line.find("search_time_ms") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) search_time_ms = std::stoi(line.substr(pos + 1));
                } else if (line.find("alpha") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) alpha = static_cast<short>(std::stoi(line.substr(pos + 1)));
                } else if (line.find("beta") != std::string::npos) {
                    size_t pos = line.find(':');
                    if (pos != std::string::npos) beta = static_cast<short>(std::stoi(line.substr(pos + 1)));
                } else if (line.find("show_analysis") != std::string::npos) {
                    show_analysis = line.find("true") != std::string::npos;
                } else if (line.find("show_best_move") != std::string::npos) {
                    show_best_move = line.find("true") != std::string::npos;
                } else if (line.find("show_legal_moves") != std::string::npos) {
                    show_legal_moves = line.find("true") != std::string::npos;
                } else if (line.find("auto_save_weights") != std::string::npos) {
                    auto_save_weights = line.find("true") != std::string::npos;
                }
            }
        }
    }
};

struct Button {
    sf::RectangleShape shape;
    sf::Text text;
    bool hovered = false;
    bool active = false;

    bool contains(sf::Vector2f point) const { return shape.getGlobalBounds().contains(point); }
    void update_hover(sf::Vector2f mouse) { hovered = contains(mouse); }

    void draw(sf::RenderWindow& window) {
        shape.setFillColor(active ? BUTTON_ACTIVE : (hovered ? BUTTON_HOVER : BUTTON_COLOR));
        window.draw(shape);
        window.draw(text);
    }
};

Button create_button(const sf::Font& font, const std::string& label, float x, float y, float width, float height, unsigned int text_size = 28) {
    Button button;
    button.shape.setPosition(x, y);
    button.shape.setSize({width, height});
    button.shape.setFillColor(BUTTON_COLOR);
    button.shape.setOutlineThickness(1.f);
    button.shape.setOutlineColor(ACCENT_COLOR);

    button.text.setFont(font);
    button.text.setString(label);
    button.text.setCharacterSize(text_size);
    button.text.setFillColor(TEXT_COLOR);

    auto bounds = button.text.getLocalBounds();
    button.text.setOrigin(bounds.left + bounds.width / 2.f, bounds.top + bounds.height / 2.f);
    button.text.setPosition(x + width / 2.f, y + height / 2.f);
    return button;
}

struct Toggle {
    sf::RectangleShape box;
    sf::Text text;
    bool* value_ptr;

    void draw(sf::RenderWindow& window) {
        box.setFillColor(*value_ptr ? BUTTON_ACTIVE : BUTTON_COLOR);
        window.draw(box);
        window.draw(text);
    }
    
    bool handle_click(sf::Vector2f mouse) {
        if (box.getGlobalBounds().contains(mouse) || text.getGlobalBounds().contains(mouse)) {
            *value_ptr = !(*value_ptr);
            return true;
        }
        return false;
    }
};

Toggle create_toggle(const sf::Font& font, const std::string& label, float x, float y, bool* val_ptr) {
    Toggle t;
    t.box.setSize({20, 20});
    t.box.setPosition(x, y);
    t.box.setOutlineThickness(1.f);
    t.box.setOutlineColor(ACCENT_COLOR);
    
    t.text.setFont(font);
    t.text.setString(label);
    t.text.setCharacterSize(16);
    t.text.setFillColor(TEXT_COLOR);
    t.text.setPosition(x + 30, y - 2);
    
    t.value_ptr = val_ptr;
    return t;
}

struct Slider {
    sf::RectangleShape bg;
    sf::RectangleShape fill;
    sf::CircleShape knob;
    sf::Text label;
    sf::Text value_text;
    int* value_ptr;
    int min_val, max_val;
    bool dragging = false;

    void update(sf::Vector2f mouse, bool clicked) {
        if (clicked && bg.getGlobalBounds().contains(mouse)) dragging = true;
        if (dragging) {
            float progress = (mouse.x - bg.getPosition().x) / bg.getSize().x;
            progress = std::max(0.0f, std::min(1.0f, progress));
            *value_ptr = min_val + static_cast<int>(progress * (max_val - min_val));
        }
    }
    
    void draw(sf::RenderWindow& window) {
        float progress = static_cast<float>(*value_ptr - min_val) / (max_val - min_val);
        fill.setSize({bg.getSize().x * progress, bg.getSize().y});
        knob.setPosition(bg.getPosition().x + fill.getSize().x - knob.getRadius(), bg.getPosition().y + bg.getSize().y / 2 - knob.getRadius());
        value_text.setString(std::to_string(*value_ptr) + " ms");
        
        window.draw(label);
        window.draw(bg);
        window.draw(fill);
        window.draw(knob);
        window.draw(value_text);
    }
};

Slider create_slider(const sf::Font& font, const std::string& lbl, float x, float y, float w, int* val_ptr, int min_v, int max_v) {
    Slider s;
    s.bg.setPosition(x, y + 25);
    s.bg.setSize({w, 10});
    s.bg.setFillColor(sf::Color(80, 80, 80));

    s.fill.setPosition(x, y + 25);
    s.fill.setSize({0, 10});
    s.fill.setFillColor(ACCENT_COLOR);

    s.knob.setRadius(8);
    s.knob.setFillColor(BUTTON_ACTIVE);

    s.label.setFont(font);
    s.label.setString(lbl);
    s.label.setCharacterSize(18);
    s.label.setFillColor(TEXT_COLOR);
    s.label.setPosition(x, y);

    s.value_text.setFont(font);
    s.value_text.setCharacterSize(18);
    s.value_text.setFillColor(ACCENT_COLOR);
    s.value_text.setPosition(x + w + 15, y + 20);

    s.value_ptr = val_ptr;
    s.min_val = min_v;
    s.max_val = max_v;
    return s;
}

struct SliderAlpha {
    sf::RectangleShape bg;
    sf::RectangleShape fill;
    sf::CircleShape knob;
    sf::Text label;
    sf::Text value_text;
    short* value_ptr;
    bool dragging = false;

    void update(sf::Vector2f mouse, bool clicked) {
        if (clicked && bg.getGlobalBounds().contains(mouse)) dragging = true;
        if (dragging) {
            float progress = (mouse.x - bg.getPosition().x) / bg.getSize().x;
            progress = std::max(0.0f, std::min(1.0f, progress));
            *value_ptr = static_cast<short>(-32000 + progress * 32000);
        }
    }

    void draw(sf::RenderWindow& window) {
        float progress = static_cast<float>(*value_ptr + 32000) / 32000.0f;
        fill.setSize({bg.getSize().x * progress, bg.getSize().y});
        knob.setPosition(bg.getPosition().x + fill.getSize().x - knob.getRadius(), bg.getPosition().y + bg.getSize().y / 2 - knob.getRadius());
        value_text.setString(std::to_string(*value_ptr));

        window.draw(label);
        window.draw(bg);
        window.draw(fill);
        window.draw(knob);
        window.draw(value_text);
    }
};

SliderAlpha create_slider_alpha(const sf::Font& font, const std::string& lbl, float x, float y, float w, short* val_ptr) {
    SliderAlpha s;
    s.bg.setPosition(x, y + 25);
    s.bg.setSize({w, 10});
    s.bg.setFillColor(sf::Color(80, 80, 80));

    s.fill.setPosition(x, y + 25);
    s.fill.setSize({0, 10});
    s.fill.setFillColor(ACCENT_COLOR);

    s.knob.setRadius(8);
    s.knob.setFillColor(BUTTON_ACTIVE);

    s.label.setFont(font);
    s.label.setString(lbl);
    s.label.setCharacterSize(18);
    s.label.setFillColor(TEXT_COLOR);
    s.label.setPosition(x, y);

    s.value_text.setFont(font);
    s.value_text.setCharacterSize(18);
    s.value_text.setFillColor(ACCENT_COLOR);
    s.value_text.setPosition(x + w + 15, y + 20);

    s.value_ptr = val_ptr;
    return s;
}

struct SliderBeta {
    sf::RectangleShape bg;
    sf::RectangleShape fill;
    sf::CircleShape knob;
    sf::Text label;
    sf::Text value_text;
    short* value_ptr;
    bool dragging = false;

    void update(sf::Vector2f mouse, bool clicked) {
        if (clicked && bg.getGlobalBounds().contains(mouse)) dragging = true;
        if (dragging) {
            float progress = (mouse.x - bg.getPosition().x) / bg.getSize().x;
            progress = std::max(0.0f, std::min(1.0f, progress));
            *value_ptr = static_cast<short>(progress * 32000);
        }
    }

    void draw(sf::RenderWindow& window) {
        float progress = static_cast<float>(*value_ptr) / 32000.0f;
        fill.setSize({bg.getSize().x * progress, bg.getSize().y});
        knob.setPosition(bg.getPosition().x + fill.getSize().x - knob.getRadius(), bg.getPosition().y + bg.getSize().y / 2 - knob.getRadius());
        value_text.setString(std::to_string(*value_ptr));

        window.draw(label);
        window.draw(bg);
        window.draw(fill);
        window.draw(knob);
        window.draw(value_text);
    }
};

SliderBeta create_slider_beta(const sf::Font& font, const std::string& lbl, float x, float y, float w, short* val_ptr) {
    SliderBeta s;
    s.bg.setPosition(x, y + 25);
    s.bg.setSize({w, 10});
    s.bg.setFillColor(sf::Color(80, 80, 80));

    s.fill.setPosition(x, y + 25);
    s.fill.setSize({0, 10});
    s.fill.setFillColor(ACCENT_COLOR);

    s.knob.setRadius(8);
    s.knob.setFillColor(BUTTON_ACTIVE);

    s.label.setFont(font);
    s.label.setString(lbl);
    s.label.setCharacterSize(18);
    s.label.setFillColor(TEXT_COLOR);
    s.label.setPosition(x, y);

    s.value_text.setFont(font);
    s.value_text.setCharacterSize(18);
    s.value_text.setFillColor(ACCENT_COLOR);
    s.value_text.setPosition(x + w + 15, y + 20);

    s.value_ptr = val_ptr;
    return s;
}


struct TextInput {
    sf::RectangleShape box;
    sf::Text text;
    std::string value;
    bool active = false;

    void draw(sf::RenderWindow& window) {
        box.setOutlineColor(active ? BUTTON_ACTIVE : ACCENT_COLOR);
        window.draw(box);
        window.draw(text);
    }
};

TextInput create_input(const sf::Font& font, float x, float y, float width, float height) {
    TextInput input;
    input.box.setPosition(x, y);
    input.box.setSize({width, height});
    input.box.setFillColor(sf::Color(50, 50, 55));
    input.box.setOutlineThickness(1.f);
    input.text.setFont(font);
    input.text.setCharacterSize(18);
    input.text.setFillColor(TEXT_COLOR);
    input.text.setPosition(x + 10, y + 15);
    return input;
}

void draw_board(sf::RenderWindow& window, float board_offset_x = 0.0f) {
    for (int rank = 0; rank < 8; rank++) {
        for (int file = 0; file < 8; file++) {
            sf::RectangleShape cell(sf::Vector2f(CELL_SIZE, CELL_SIZE));
            cell.setPosition(board_offset_x + file * CELL_SIZE, rank * CELL_SIZE);
            bool is_light = (rank + file) % 2 == 0;
            cell.setFillColor(is_light ? LIGHT_SQUARE : DARK_SQUARE);
            window.draw(cell);
        }
    }
}

sf::Vector2i square_to_screen(int square, float board_offset_x = 0.0f) {
    int rank = 7 - square / 8;
    int file = square % 8;
    return { static_cast<int>(board_offset_x + file * CELL_SIZE), rank * CELL_SIZE };
}

void draw_last_move_highlight(sf::RenderWindow& window, int from, int to, float board_offset_x = 0.0f) {
    if (from < 0 || from > 63 || to < 0 || to > 63) return;

    sf::Color highlight_color(255, 255, 0, 80);  // Желтая полупрозрачная подсветка

    // Подсветка клетки "откуда"
    sf::Vector2i from_screen = square_to_screen(from, board_offset_x);
    sf::RectangleShape from_highlight(sf::Vector2f(CELL_SIZE, CELL_SIZE));
    from_highlight.setPosition(from_screen.x, from_screen.y);
    from_highlight.setFillColor(highlight_color);
    window.draw(from_highlight);

    // Подсветка клетки "куда"
    sf::Vector2i to_screen = square_to_screen(to, board_offset_x);
    sf::RectangleShape to_highlight(sf::Vector2f(CELL_SIZE, CELL_SIZE));
    to_highlight.setPosition(to_screen.x, to_screen.y);
    to_highlight.setFillColor(highlight_color);
    window.draw(to_highlight);
}

void draw_check_highlight(sf::RenderWindow& window, Position& pos, float board_offset_x = 0.0f) {
    Color side = pos.get_side_to_move();
    Piece king_piece = (side == WHITE) ? WHITE_KING : BLACK_KING;

    // Находим короля
    Square king_sq = 64;
    for (int sq = 0; sq < 64; sq++) {
        if (pos.get_piece(sq) == king_piece) {
            king_sq = sq;
            break;
        }
    }

    if (king_sq < 64) {
        Color enemy = (side == WHITE) ? BLACK : WHITE;
        if (pos.is_square_attacked(king_sq, enemy)) {
            // Красная подсветка короля под шахом
            sf::Vector2i king_screen = square_to_screen(king_sq, board_offset_x);
            sf::RectangleShape check_highlight(sf::Vector2f(CELL_SIZE, CELL_SIZE));
            check_highlight.setPosition(king_screen.x, king_screen.y);
            check_highlight.setFillColor(sf::Color(255, 0, 0, 100));
            window.draw(check_highlight);
        }
    }
}

void draw_pieces(sf::RenderWindow& window, const Position& pos, SpriteCache& sprite_cache, float board_offset_x = 0.0f) {
    for (int square = 0; square < 64; square++) {
        Piece piece = pos.get_piece(square);
        if (piece == EMPTY) continue;
        sf::Texture* texture = sprite_cache.get_piece_texture(piece);
        if (!texture) continue;
        sf::Vector2i screen_pos = square_to_screen(square, board_offset_x);
        sf::Sprite sprite(*texture);
        sprite.setScale(0.9f, 0.9f);
        sf::FloatRect sprite_bounds = sprite.getLocalBounds();
        sprite.setPosition(screen_pos.x + (CELL_SIZE - sprite_bounds.width * 0.9f) / 2.f, screen_pos.y + (CELL_SIZE - sprite_bounds.height * 0.9f) / 2.f);
        window.draw(sprite);
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
    if (moves.count < 0 || moves.count > 256) return legal_moves;
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

void draw_legal_moves(sf::RenderWindow& window, const std::vector<int>& legal_moves, float board_offset_x = 0.0f) {
    for (int move_to : legal_moves) {
        sf::Vector2i screen_pos = square_to_screen(move_to, board_offset_x);
        sf::CircleShape indicator(8.f);
        indicator.setFillColor(sf::Color(100, 200, 100, 150));
        indicator.setPosition(screen_pos.x + CELL_SIZE / 2.f - 8.f, screen_pos.y + CELL_SIZE / 2.f - 8.f);
        window.draw(indicator);
    }
}

void draw_best_move_arrow(sf::RenderWindow& window, Move best_move, float board_offset_x = 0.0f) {
    if (best_move == 0) return;
    int from = best_move & 0x3F;
    int to = (best_move >> 6) & 0x3F;
    sf::Vector2i from_screen = square_to_screen(from, board_offset_x);
    sf::Vector2i to_screen = square_to_screen(to, board_offset_x);
    sf::Vector2f from_center(from_screen.x + CELL_SIZE / 2.f, from_screen.y + CELL_SIZE / 2.f);
    sf::Vector2f to_center(to_screen.x + CELL_SIZE / 2.f, to_screen.y + CELL_SIZE / 2.f);
    sf::Vector2f direction = to_center - from_center;
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length < 1.f) return;
    direction /= length;
    sf::RectangleShape arrow(sf::Vector2f(length, 8.f));
    arrow.setPosition(from_center);
    arrow.setFillColor(sf::Color(100, 180, 255, 180));
    float angle = std::atan2(direction.y, direction.x) * 180.f / 3.14159f;
    arrow.setRotation(angle);
    window.draw(arrow);
    sf::CircleShape arrowhead(8.f, 3);
    arrowhead.setFillColor(sf::Color(100, 180, 255, 200));
    arrowhead.setPosition(to_center.x - 8.f, to_center.y - 8.f);
    arrowhead.setRotation(angle + 90.f);
    window.draw(arrowhead);
}

void draw_promotion_dialog(sf::RenderWindow& window, SpriteCache& sprite_cache, Color side, int to_square, float board_offset_x = 0.0f) {
    // Полупрозрачный фон
    sf::RectangleShape overlay(sf::Vector2f(BOARD_SIZE, BOARD_SIZE));
    overlay.setPosition(board_offset_x, 0);
    overlay.setFillColor(sf::Color(0, 0, 0, 180));
    window.draw(overlay);

    // Определяем позицию диалога (на клетке, куда идет пешка)
    sf::Vector2i screen_pos = square_to_screen(to_square, board_offset_x);

    // Размеры диалога
    float dialog_width = CELL_SIZE;
    float dialog_height = CELL_SIZE * 4;

    // Корректируем позицию, чтобы диалог не выходил за границы доски
    float dialog_x = screen_pos.x;
    float dialog_y = screen_pos.y;

    if (side == WHITE) {
        // Для белых показываем диалог вверх от клетки превращения
        dialog_y = screen_pos.y - dialog_height + CELL_SIZE;
        if (dialog_y < 0) dialog_y = screen_pos.y;
    } else {
        // Для черных показываем диалог вниз от клетки превращения
        if (dialog_y + dialog_height > BOARD_SIZE) {
            dialog_y = screen_pos.y - dialog_height + CELL_SIZE;
        }
    }

    // Фон диалога
    sf::RectangleShape dialog_bg(sf::Vector2f(dialog_width, dialog_height));
    dialog_bg.setPosition(dialog_x, dialog_y);
    dialog_bg.setFillColor(sf::Color(40, 40, 45));
    dialog_bg.setOutlineThickness(3.f);
    dialog_bg.setOutlineColor(ACCENT_COLOR);
    window.draw(dialog_bg);

    // Отрисовываем фигуры для выбора (Ферзь, Ладья, Слон, Конь)
    Piece pieces[4] = {
        side == WHITE ? WHITE_QUEEN : BLACK_QUEEN,
        side == WHITE ? WHITE_ROOK : BLACK_ROOK,
        side == WHITE ? WHITE_BISHOP : BLACK_BISHOP,
        side == WHITE ? WHITE_KNIGHT : BLACK_KNIGHT
    };

    for (int i = 0; i < 4; i++) {
        sf::RectangleShape piece_bg(sf::Vector2f(CELL_SIZE, CELL_SIZE));
        piece_bg.setPosition(dialog_x, dialog_y + i * CELL_SIZE);
        piece_bg.setFillColor((i % 2 == 0) ? LIGHT_SQUARE : DARK_SQUARE);
        window.draw(piece_bg);

        sf::Texture* texture = sprite_cache.get_piece_texture(pieces[i]);
        if (texture) {
            sf::Sprite sprite(*texture);
            sprite.setScale(0.9f, 0.9f);
            sf::FloatRect sprite_bounds = sprite.getLocalBounds();
            sprite.setPosition(
                dialog_x + (CELL_SIZE - sprite_bounds.width * 0.9f) / 2.f,
                dialog_y + i * CELL_SIZE + (CELL_SIZE - sprite_bounds.height * 0.9f) / 2.f
            );
            window.draw(sprite);
        }
    }
}

std::string move_to_string(Move move) {
    int from = move & 0x3F, to = (move >> 6) & 0x3F;
    std::string result;
    result += (char)('a' + (from % 8)); result += (char)('1' + (from / 8));
    result += (char)('a' + (to % 8));   result += (char)('1' + (to / 8));
    return result;
}

std::string check_game_over(Position& pos) {
    MoveList moves = pos.generate_all_moves();
    int legal_moves = 0;

    for (Square i = 0; i < moves.count; i++) {
        Position test_pos = pos;
        if (test_pos.make_move(moves.moves[i])) {
            legal_moves++;
            break;
        }
    }

    if (legal_moves == 0) {
        Color side = pos.get_side_to_move();
        Color enemy = (side == WHITE) ? BLACK : WHITE;

        // Находим короля текущей стороны
        Square king_sq = 0;
        Piece king_piece = (side == WHITE) ? WHITE_KING : BLACK_KING;

        for (int sq = 0; sq < 64; sq++) {
            if (pos.get_piece(sq) == king_piece) {
                king_sq = sq;
                break;
            }
        }

        // Проверяем, атакован ли король противником
        bool in_check = pos.is_square_attacked(king_sq, enemy);

        if (in_check) {
            return (side == WHITE) ? "Checkmate! Black wins!" : "Checkmate! White wins!";
        } else {
            return "Stalemate! Draw!";
        }
    }

    return "";
}

bool is_valid_move(Position& pos, int from, int to) {
    if (from < 0 || from > 63 || to < 0 || to > 63) return false;
    MoveList moves = pos.generate_all_moves();
    for (Square i = 0; i < moves.count; i++) {
        if ((moves.moves[i] & 0x3F) == from && ((moves.moves[i] >> 6) & 0x3F) == to) return true;
    }
    return false;
}

void draw_menu_background(sf::RenderWindow& window) {
    window.clear(DARK_PANEL);
}

int main() {
    sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "Chess Engine");
    window.setFramerateLimit(60);

    sf::Font font;
    if (!font.loadFromFile("GUI/assets/fonts/Roboto-Regular.ttf")) {
        std::cout << "Could not load font" << std::endl;
        return -1;
    }

    SpriteCache sprite_cache;
    sprite_cache.load_piece_sprites();

    // Загружаем сохраненные веса или используем значения по умолчанию
    WeightsIO::load_weights("trained_weights.bin");

    GameSettings settings;
    settings.load_from_file("game_settings.json");

    Position board("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    Position visual_engine_board;

    bool human_is_white = true;
    std::atomic<bool> engine_thinking = false;
    std::mutex engine_mutex;
    std::string engine_info = "Engine idle";
    Move engine_best_move = 0;
    
    GameState game_state = GameState::MENU;
    UIState ui;

    Tuner tuner;

    // MENU WIDGETS
    Button btn_play = create_button(font, "Play", 450, 280, 500, 70);
    Button btn_settings = create_button(font, "Settings", 450, 380, 500, 70);
    Button btn_load_fen = create_button(font, "Load FEN", 450, 480, 500, 70);
    Button btn_exit = create_button(font, "Exit", 450, 580, 500, 70);
    
    Button btn_white = create_button(font, "Play White", 450, 320, 500, 70);
    Button btn_black = create_button(font, "Play Black", 450, 430, 500, 70);
    Button btn_back = create_button(font, "Back", 40, 780, 200, 60);

    // SETTINGS WIDGETS
    Toggle toggle_analysis_settings = create_toggle(font, "Show Engine Thinking Animation", 450, 200, &settings.show_analysis);
    Toggle toggle_best_move_settings = create_toggle(font, "Show Best Move Arrow", 450, 250, &settings.show_best_move);
    Toggle toggle_legal_moves_settings = create_toggle(font, "Show Legal Moves", 450, 300, &settings.show_legal_moves);
    Slider slider_time_settings = create_slider(font, "Search Time (ms)", 450, 370, 400, &settings.search_time_ms, 500, 30000);
    Button btn_preset_1s_settings = create_button(font, "1s", 450, 450, 80, 40, 16);
    Button btn_preset_3s_settings = create_button(font, "3s", 540, 450, 80, 40, 16);
    Button btn_preset_5s_settings = create_button(font, "5s", 630, 450, 80, 40, 16);
    Button btn_preset_10s_settings = create_button(font, "10s", 720, 450, 80, 40, 16);
    SliderAlpha slider_alpha_settings = create_slider_alpha(font, "Alpha", 450, 520, 400, &settings.alpha);
    SliderBeta slider_beta_settings = create_slider_beta(font, "Beta", 450, 590, 400, &settings.beta);
    TextInput dataset_input = create_input(font, 450, 680, 500, 50);
    Button btn_train = create_button(font, "Train Model", 450, 760, 500, 60, 22);

    // FEN WIDGETS
    TextInput fen_input = create_input(font, 250, 300, 900, 60);
    Button btn_apply_fen = create_button(font, "Apply FEN", 450, 420, 500, 70);

    // PLAYING WIDGETS (Sidebar)
    Button btn_toggle_sidebar = create_button(font, ">", BOARD_SIZE + 210, 400, 40, 60, 28);
    Toggle toggle_analysis_sidebar = create_toggle(font, "Show Engine Thinking", BOARD_SIZE + 40, 140, &settings.show_analysis);
    Toggle toggle_best_move_sidebar = create_toggle(font, "Show PV Arrow", BOARD_SIZE + 40, 180, &settings.show_best_move);
    Toggle toggle_legal_moves_sidebar = create_toggle(font, "Show Legal Moves", BOARD_SIZE + 40, 220, &settings.show_legal_moves);
    Slider slider_time_sidebar = create_slider(font, "Search Time (ms)", BOARD_SIZE + 40, 280, 450, &settings.search_time_ms, 500, 30000);
    Button btn_preset_1s_sidebar = create_button(font, "1s", BOARD_SIZE + 40, 350, 90, 40, 16);
    Button btn_preset_3s_sidebar = create_button(font, "3s", BOARD_SIZE + 160, 350, 90, 40, 16);
    Button btn_preset_5s_sidebar = create_button(font, "5s", BOARD_SIZE + 280, 350, 90, 40, 16);
    Button btn_preset_10s_sidebar = create_button(font, "10s", BOARD_SIZE + 400, 350, 90, 40, 16);
    SliderAlpha slider_alpha_sidebar = create_slider_alpha(font, "Alpha", BOARD_SIZE + 40, 420, 450, &settings.alpha);
    SliderBeta slider_beta_sidebar = create_slider_beta(font, "Beta", BOARD_SIZE + 40, 490, 450, &settings.beta);
    Button btn_new_game = create_button(font, "New Game", BOARD_SIZE + 40, 800, 200, 50, 20);

    std::unique_ptr<std::thread> engine_thread = nullptr;

    auto start_engine = [&]() {
        if (engine_thinking) return;
        engine_thinking = true;
        engine_best_move = 0;
        if (engine_thread && engine_thread->joinable()) engine_thread->join();

        // Мы передаем в поток движка КОПИЮ доски, чтобы GUI безопасно рендерился
        engine_thread = std::make_unique<std::thread>([&, search_board = board]() mutable {
            
            // Задаем коллбэк для безопасного обновления визуализации мыслей 
            search_board.on_node_update = [&](const Position& p) {
                if (settings.show_analysis) {
                    std::lock_guard<std::mutex> lock(engine_mutex);
                    visual_engine_board = p;
                }
            };

            short best_score = 0;
            Move best_move = 0;
            try {
                best_move = search_board.get_best_move(std::chrono::milliseconds(settings.search_time_ms), settings.alpha, settings.beta, best_score);
            } catch (...) { std::cout << "Engine error" << std::endl; }

            std::vector<Move> pv = search_board.get_PV(4);
            std::vector<std::string> pv_strings;
            for (Move m : pv) pv_strings.push_back(move_to_string(m));

            // После поиска, применяем ход к НАСТОЯЩЕЙ доске
            if (best_move != 0) {
                board.make_move(best_move);
                ui.move_history.push_back(move_to_string(best_move));
                engine_best_move = best_move;

                // Проверяем окончание игры
                std::string game_result = check_game_over(board);
                if (!game_result.empty()) {
                    ui.game_over = true;
                    ui.game_status = game_result;
                }
            }

            {
                std::lock_guard<std::mutex> lock(engine_mutex);
                engine_info = (best_move != 0) ? "Engine played: " + move_to_string(best_move) : "Engine idle";
                ui.best_move_sequence = pv_strings;
                
                // Переводим оценку из относительной (со стороны того, кто ходил) в абсолютную (положительное - победа Белых)
                short display_score = (search_board.get_side_to_move() == WHITE) ? best_score : -best_score;
                ui.eval_score = display_score;
            }
            engine_thinking = false;
        });
    };

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) window.close();

            if (event.type == sf::Event::TextEntered) {
                if (fen_input.active && game_state == GameState::FEN_INPUT) {
                    if (event.text.unicode == 8 && !fen_input.value.empty()) fen_input.value.pop_back();
                    else if (event.text.unicode < 128 && event.text.unicode != 8 && event.text.unicode != 13) fen_input.value += static_cast<char>(event.text.unicode);
                    fen_input.text.setString(fen_input.value);
                }
                if (dataset_input.active && game_state == GameState::SETTINGS) {
                    if (event.text.unicode == 8 && !dataset_input.value.empty()) dataset_input.value.pop_back();
                    else if (event.text.unicode < 128 && event.text.unicode != 8 && event.text.unicode != 13) dataset_input.value += static_cast<char>(event.text.unicode);
                    dataset_input.text.setString(dataset_input.value);
                }
            }

            if (event.type == sf::Event::MouseButtonReleased) {
                slider_time_settings.dragging = false;
                slider_alpha_settings.dragging = false;
                slider_beta_settings.dragging = false;
                slider_time_sidebar.dragging = false;
                slider_alpha_sidebar.dragging = false;
                slider_beta_sidebar.dragging = false;
            }

            if (event.type == sf::Event::MouseButtonPressed) {
                sf::Vector2f mouse = window.mapPixelToCoords(sf::Mouse::getPosition(window));

                if (game_state == GameState::MENU) {
                    if (btn_play.contains(mouse)) game_state = GameState::SIDE_SELECT;
                    if (btn_settings.contains(mouse)) game_state = GameState::SETTINGS;
                    if (btn_load_fen.contains(mouse)) { game_state = GameState::FEN_INPUT; fen_input.value = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"; fen_input.text.setString(fen_input.value); }
                    if (btn_exit.contains(mouse)) window.close();
                }
                else if (game_state == GameState::SIDE_SELECT) {
                    if (btn_white.contains(mouse)) {
                        human_is_white = true; ui.move_history.clear(); ui.best_move_sequence.clear(); ui.eval_score = 0; ui.selected_square = -1; game_state = GameState::PLAYING;
                    }
                    if (btn_black.contains(mouse)) {
                        human_is_white = false; ui.move_history.clear(); ui.best_move_sequence.clear(); ui.eval_score = 0; ui.selected_square = -1; game_state = GameState::PLAYING;
                        start_engine();
                    }
                    if (btn_back.contains(mouse)) game_state = GameState::MENU;
                }
                else if (game_state == GameState::SETTINGS) {
                    toggle_analysis_settings.handle_click(mouse);
                    toggle_best_move_settings.handle_click(mouse);
                    toggle_legal_moves_settings.handle_click(mouse);
                    slider_time_settings.update(mouse, true);
                    slider_alpha_settings.update(mouse, true);
                    slider_beta_settings.update(mouse, true);

                    if (btn_preset_1s_settings.contains(mouse)) settings.search_time_ms = 1000;
                    if (btn_preset_3s_settings.contains(mouse)) settings.search_time_ms = 3000;
                    if (btn_preset_5s_settings.contains(mouse)) settings.search_time_ms = 5000;
                    if (btn_preset_10s_settings.contains(mouse)) settings.search_time_ms = 10000;

                    dataset_input.active = dataset_input.box.getGlobalBounds().contains(mouse);

                    if (btn_train.contains(mouse) && !dataset_input.value.empty()) {
                        if (tuner.open_dataset(dataset_input.value)) {
                            std::thread([&]() {
                                tuner.train(100, 0.05);
                                WeightsIO::save_weights("trained_weights.bin");
                                std::cout << "[Main] Weights saved after training" << std::endl;
                            }).detach();
                        }
                    }
                    if (btn_back.contains(mouse)) { settings.save_to_file("game_settings.json"); game_state = GameState::MENU; }
                }
                else if (game_state == GameState::FEN_INPUT) {
                    fen_input.active = fen_input.box.getGlobalBounds().contains(mouse);
                    if (btn_apply_fen.contains(mouse) && !fen_input.value.empty()) {
                        board.parse_FEN(fen_input.value);
                        ui.move_history.clear(); ui.best_move_sequence.clear(); ui.eval_score = 0; ui.selected_square = -1; game_state = GameState::PLAYING;
                        if (!human_is_white) start_engine();
                    }
                    if (btn_back.contains(mouse)) game_state = GameState::MENU;
                }
                else if (game_state == GameState::PLAYING) {
                    if (btn_toggle_sidebar.contains(mouse)) {
                        ui.sidebar_open = !ui.sidebar_open;
                        ui.target_offset_x = ui.sidebar_open ? 0.0f : 200.0f;
                        btn_toggle_sidebar.text.setString(ui.sidebar_open ? "<" : ">");
                    }
                    
                    if (ui.sidebar_open) {
                        toggle_analysis_sidebar.handle_click(mouse);
                        toggle_best_move_sidebar.handle_click(mouse);
                        toggle_legal_moves_sidebar.handle_click(mouse);
                        slider_time_sidebar.update(mouse, true);
                        slider_alpha_sidebar.update(mouse, true);
                        slider_beta_sidebar.update(mouse, true);
                        if (btn_preset_1s_sidebar.contains(mouse)) settings.search_time_ms = 1000;
                        if (btn_preset_3s_sidebar.contains(mouse)) settings.search_time_ms = 3000;
                        if (btn_preset_5s_sidebar.contains(mouse)) settings.search_time_ms = 5000;
                        if (btn_preset_10s_sidebar.contains(mouse)) settings.search_time_ms = 10000;

                        if (btn_new_game.contains(mouse)) {
                            board.parse_FEN("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
                            ui.move_history.clear();
                            ui.best_move_sequence.clear();
                            ui.eval_score = 0;
                            ui.selected_square = -1;
                            ui.game_over = false;
                            ui.game_status = "";
                            engine_best_move = 0;
                            if (!human_is_white) start_engine();
                        }
                    }

                    if (!ui.game_over && !engine_thinking && mouse.x >= ui.board_offset_x && mouse.x < ui.board_offset_x + BOARD_SIZE && mouse.y < BOARD_SIZE) {
                        int file = static_cast<int>((mouse.x - ui.board_offset_x) / CELL_SIZE);
                        int rank = 7 - static_cast<int>(mouse.y / CELL_SIZE);
                        int clicked_square = rank * 8 + file;

                        if (ui.selected_square == -1) {
                            ui.legal_moves_from_selection = get_legal_moves_from_square(board, clicked_square);
                            if (!ui.legal_moves_from_selection.empty()) ui.selected_square = clicked_square;
                        } else {
                            if (clicked_square == ui.selected_square) {
                                ui.selected_square = -1; ui.legal_moves_from_selection.clear();
                            } else if (std::find(ui.legal_moves_from_selection.begin(), ui.legal_moves_from_selection.end(), clicked_square) != ui.legal_moves_from_selection.end()) {
                                if (is_valid_move(board, ui.selected_square, clicked_square)) {
                                    // Проверяем, является ли это ходом пешки на последнюю горизонталь
                                    Piece moving_piece = board.get_piece(ui.selected_square);
                                    bool is_pawn = (moving_piece == WHITE_PAWN || moving_piece == BLACK_PAWN);
                                    bool is_promotion = is_pawn && (clicked_square >= 56 || clicked_square <= 7);

                                    if (is_promotion) {
                                        // Открываем диалог выбора фигуры
                                        ui.promotion_from = ui.selected_square;
                                        ui.promotion_to = clicked_square;

                                        // Определяем базовый флаг (взятие или тихий ход)
                                        Piece target_piece = board.get_piece(clicked_square);
                                        ui.promotion_base_flag = (target_piece != EMPTY) ? CAPTURE : SILENT_MOVE;

                                        game_state = GameState::PROMOTION_DIALOG;
                                        ui.selected_square = -1;
                                        ui.legal_moves_from_selection.clear();
                                    } else {
                                        // Обычный ход
                                        MoveList moves = board.generate_all_moves();
                                        for (Square i = 0; i < moves.count; i++) {
                                            if ((moves.moves[i] & 0x3F) == ui.selected_square && ((moves.moves[i] >> 6) & 0x3F) == clicked_square) {
                                                // Выполняем ход
                                                board.make_move(moves.moves[i]);
                                                ui.move_history.push_back(move_to_string(moves.moves[i]));

                                                // Сохраняем последний ход для подсветки
                                                ui.last_move_from = ui.selected_square;
                                                ui.last_move_to = clicked_square;

                                                ui.selected_square = -1;
                                                ui.legal_moves_from_selection.clear();
                                                engine_best_move = 0;

                                                // Проверяем окончание игры
                                                std::string game_result = check_game_over(board);
                                                if (!game_result.empty()) {
                                                    ui.game_over = true;
                                                    ui.game_status = game_result;
                                                } else {
                                                    start_engine();
                                                }
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
                else if (game_state == GameState::PROMOTION_DIALOG) {
                    // Обработка клика в диалоге выбора фигуры
                    if (mouse.x >= ui.board_offset_x && mouse.x < ui.board_offset_x + BOARD_SIZE && mouse.y < BOARD_SIZE) {
                        sf::Vector2i screen_pos = square_to_screen(ui.promotion_to, ui.board_offset_x);

                        Color side = board.get_side_to_move();
                        float dialog_x = screen_pos.x;
                        float dialog_y = screen_pos.y;
                        float dialog_height = CELL_SIZE * 4;

                        if (side == WHITE) {
                            dialog_y = screen_pos.y - dialog_height + CELL_SIZE;
                            if (dialog_y < 0) dialog_y = screen_pos.y;
                        } else {
                            if (dialog_y + dialog_height > BOARD_SIZE) {
                                dialog_y = screen_pos.y - dialog_height + CELL_SIZE;
                            }
                        }

                        // Проверяем, на какую фигуру кликнули
                        if (mouse.x >= dialog_x && mouse.x < dialog_x + CELL_SIZE &&
                            mouse.y >= dialog_y && mouse.y < dialog_y + dialog_height) {

                            int piece_index = static_cast<int>((mouse.y - dialog_y) / CELL_SIZE);
                            if (piece_index >= 0 && piece_index < 4) {
                                // Определяем флаг превращения
                                std::uint8_t promotion_flags[4];
                                if (ui.promotion_base_flag == CAPTURE) {
                                    promotion_flags[0] = QUEEN_PROMOTION_AND_CAPTURE;
                                    promotion_flags[1] = ROOK_PROMOTION_AND_CAPTURE;
                                    promotion_flags[2] = BISHOP_PROMOTION_AND_CAPTURE;
                                    promotion_flags[3] = KNIGHT_PROMOTION_AND_CAPTURE;
                                } else {
                                    promotion_flags[0] = QUEEN_PROMOTION;
                                    promotion_flags[1] = ROOK_PROMOTION;
                                    promotion_flags[2] = BISHOP_PROMOTION;
                                    promotion_flags[3] = KNIGHT_PROMOTION;
                                }

                                // Создаем и выполняем ход
                                Move promotion_move = (ui.promotion_from | (ui.promotion_to << 6) | (promotion_flags[piece_index] << 12));
                                board.make_move(promotion_move);
                                ui.move_history.push_back(move_to_string(promotion_move));

                                // Сохраняем последний ход для подсветки
                                ui.last_move_from = ui.promotion_from;
                                ui.last_move_to = ui.promotion_to;

                                engine_best_move = 0;

                                // Проверяем окончание игры
                                std::string game_result = check_game_over(board);
                                if (!game_result.empty()) {
                                    ui.game_over = true;
                                    ui.game_status = game_result;
                                } else {
                                    start_engine();
                                }

                                game_state = GameState::PLAYING;
                            }
                        }
                    }
                }
            }

            if (event.type == sf::Event::MouseMoved) {
                sf::Vector2f mouse = window.mapPixelToCoords(sf::Mouse::getPosition(window));
                if (game_state == GameState::SETTINGS) {
                    slider_time_settings.update(mouse, false);
                    slider_alpha_settings.update(mouse, false);
                    slider_beta_settings.update(mouse, false);
                }
                else if (game_state == GameState::PLAYING && ui.sidebar_open) {
                    slider_time_sidebar.update(mouse, false);
                    slider_alpha_sidebar.update(mouse, false);
                    slider_beta_sidebar.update(mouse, false);
                }

                btn_play.update_hover(mouse); btn_settings.update_hover(mouse);
                btn_load_fen.update_hover(mouse); btn_exit.update_hover(mouse);
                btn_white.update_hover(mouse); btn_black.update_hover(mouse);
                btn_back.update_hover(mouse); btn_apply_fen.update_hover(mouse);
                btn_train.update_hover(mouse); btn_toggle_sidebar.update_hover(mouse);

                btn_preset_1s_settings.update_hover(mouse); btn_preset_3s_settings.update_hover(mouse);
                btn_preset_5s_settings.update_hover(mouse); btn_preset_10s_settings.update_hover(mouse);
                btn_preset_1s_sidebar.update_hover(mouse); btn_preset_3s_sidebar.update_hover(mouse);
                btn_preset_5s_sidebar.update_hover(mouse); btn_preset_10s_sidebar.update_hover(mouse);
                btn_new_game.update_hover(mouse);
            }
        }

        btn_preset_1s_settings.active = (settings.search_time_ms == 1000);
        btn_preset_3s_settings.active = (settings.search_time_ms == 3000);
        btn_preset_5s_settings.active = (settings.search_time_ms == 5000);
        btn_preset_10s_settings.active = (settings.search_time_ms == 10000);

        btn_preset_1s_sidebar.active = (settings.search_time_ms == 1000);
        btn_preset_3s_sidebar.active = (settings.search_time_ms == 3000);
        btn_preset_5s_sidebar.active = (settings.search_time_ms == 5000);
        btn_preset_10s_sidebar.active = (settings.search_time_ms == 10000);

        // Плавная анимация смещения доски
        if (std::abs(ui.board_offset_x - ui.target_offset_x) > 0.5f) {
            ui.board_offset_x += (ui.target_offset_x - ui.board_offset_x) * 0.15f;
        } else {
            ui.board_offset_x = ui.target_offset_x;
        }

        // Обновляем позицию кнопки toggle
        float sidebar_base_x = ui.board_offset_x + BOARD_SIZE;
        if (ui.sidebar_open) {
            // Когда панель открыта - кнопка в правой части панели
            btn_toggle_sidebar.shape.setPosition(sidebar_base_x + SIDEBAR_WIDTH - 60, 20);
            auto bounds_toggle = btn_toggle_sidebar.text.getLocalBounds();
            btn_toggle_sidebar.text.setOrigin(bounds_toggle.left + bounds_toggle.width / 2.f, bounds_toggle.top + bounds_toggle.height / 2.f);
            btn_toggle_sidebar.text.setPosition(sidebar_base_x + SIDEBAR_WIDTH - 60 + 20, 20 + 30);
        } else {
            // Когда панель закрыта - кнопка впритык справа от доски
            btn_toggle_sidebar.shape.setPosition(sidebar_base_x + 5, 20);
            auto bounds_toggle = btn_toggle_sidebar.text.getLocalBounds();
            btn_toggle_sidebar.text.setOrigin(bounds_toggle.left + bounds_toggle.width / 2.f, bounds_toggle.top + bounds_toggle.height / 2.f);
            btn_toggle_sidebar.text.setPosition(sidebar_base_x + 5 + 20, 20 + 30);
        }

        // Обновляем позиции элементов sidebar
        toggle_analysis_sidebar.box.setPosition(sidebar_base_x + 40, 140);
        toggle_analysis_sidebar.text.setPosition(sidebar_base_x + 70, 138);
        toggle_best_move_sidebar.box.setPosition(sidebar_base_x + 40, 180);
        toggle_best_move_sidebar.text.setPosition(sidebar_base_x + 70, 178);
        toggle_legal_moves_sidebar.box.setPosition(sidebar_base_x + 40, 220);
        toggle_legal_moves_sidebar.text.setPosition(sidebar_base_x + 70, 218);

        slider_time_sidebar.bg.setPosition(sidebar_base_x + 40, 305);
        slider_time_sidebar.fill.setPosition(sidebar_base_x + 40, 305);
        slider_time_sidebar.label.setPosition(sidebar_base_x + 40, 280);
        slider_time_sidebar.value_text.setPosition(sidebar_base_x + 40 + 450 + 15, 300);

        btn_preset_1s_sidebar.shape.setPosition(sidebar_base_x + 40, 350);
        auto bounds_1s = btn_preset_1s_sidebar.text.getLocalBounds();
        btn_preset_1s_sidebar.text.setOrigin(bounds_1s.left + bounds_1s.width / 2.f, bounds_1s.top + bounds_1s.height / 2.f);
        btn_preset_1s_sidebar.text.setPosition(sidebar_base_x + 40 + 45, 350 + 20);

        btn_preset_3s_sidebar.shape.setPosition(sidebar_base_x + 160, 350);
        auto bounds_3s = btn_preset_3s_sidebar.text.getLocalBounds();
        btn_preset_3s_sidebar.text.setOrigin(bounds_3s.left + bounds_3s.width / 2.f, bounds_3s.top + bounds_3s.height / 2.f);
        btn_preset_3s_sidebar.text.setPosition(sidebar_base_x + 160 + 45, 350 + 20);

        btn_preset_5s_sidebar.shape.setPosition(sidebar_base_x + 280, 350);
        auto bounds_5s = btn_preset_5s_sidebar.text.getLocalBounds();
        btn_preset_5s_sidebar.text.setOrigin(bounds_5s.left + bounds_5s.width / 2.f, bounds_5s.top + bounds_5s.height / 2.f);
        btn_preset_5s_sidebar.text.setPosition(sidebar_base_x + 280 + 45, 350 + 20);

        btn_preset_10s_sidebar.shape.setPosition(sidebar_base_x + 400, 350);
        auto bounds_10s = btn_preset_10s_sidebar.text.getLocalBounds();
        btn_preset_10s_sidebar.text.setOrigin(bounds_10s.left + bounds_10s.width / 2.f, bounds_10s.top + bounds_10s.height / 2.f);
        btn_preset_10s_sidebar.text.setPosition(sidebar_base_x + 400 + 45, 350 + 20);

        slider_alpha_sidebar.bg.setPosition(sidebar_base_x + 40, 445);
        slider_alpha_sidebar.fill.setPosition(sidebar_base_x + 40, 445);
        slider_alpha_sidebar.label.setPosition(sidebar_base_x + 40, 420);
        slider_alpha_sidebar.value_text.setPosition(sidebar_base_x + 40 + 450 + 15, 440);

        slider_beta_sidebar.bg.setPosition(sidebar_base_x + 40, 515);
        slider_beta_sidebar.fill.setPosition(sidebar_base_x + 40, 515);
        slider_beta_sidebar.label.setPosition(sidebar_base_x + 40, 490);
        slider_beta_sidebar.value_text.setPosition(sidebar_base_x + 40 + 450 + 15, 510);

        btn_new_game.shape.setPosition(sidebar_base_x + 40, 800);
        auto bounds_new_game = btn_new_game.text.getLocalBounds();
        btn_new_game.text.setOrigin(bounds_new_game.left + bounds_new_game.width / 2.f, bounds_new_game.top + bounds_new_game.height / 2.f);
        btn_new_game.text.setPosition(sidebar_base_x + 40 + 100, 800 + 25);

        window.clear();

        if (game_state == GameState::MENU) {
            draw_menu_background(window);
            sf::Text title; title.setFont(font); title.setString("CHESS ENGINE"); title.setCharacterSize(80); title.setFillColor(ACCENT_COLOR);
            title.setPosition((WINDOW_WIDTH - title.getLocalBounds().width) / 2.f, 60.f); window.draw(title);
            btn_play.draw(window); btn_settings.draw(window); btn_load_fen.draw(window); btn_exit.draw(window);
        }
        else if (game_state == GameState::SIDE_SELECT) {
            draw_menu_background(window);
            sf::Text title; title.setFont(font); title.setString("Choose Your Side"); title.setCharacterSize(60); title.setFillColor(ACCENT_COLOR);
            title.setPosition((WINDOW_WIDTH - title.getLocalBounds().width) / 2.f, 100.f); window.draw(title);
            btn_white.draw(window); btn_black.draw(window); btn_back.draw(window);
        }
        else if (game_state == GameState::SETTINGS) {
            draw_menu_background(window);
            sf::Text title; title.setFont(font); title.setCharacterSize(60); title.setString("Settings"); title.setFillColor(ACCENT_COLOR);
            title.setPosition((WINDOW_WIDTH - title.getLocalBounds().width) / 2.f, 60); window.draw(title);

            toggle_analysis_settings.draw(window);
            toggle_best_move_settings.draw(window);
            toggle_legal_moves_settings.draw(window);
            slider_time_settings.draw(window);
            btn_preset_1s_settings.draw(window); btn_preset_3s_settings.draw(window);
            btn_preset_5s_settings.draw(window); btn_preset_10s_settings.draw(window);
            slider_alpha_settings.draw(window);
            slider_beta_settings.draw(window);

            sf::Text tuner_label; tuner_label.setFont(font); tuner_label.setString("Model Training Dataset (.epd)");
            tuner_label.setCharacterSize(22); tuner_label.setFillColor(ACCENT_COLOR); tuner_label.setPosition(450, 640);
            window.draw(tuner_label);

            dataset_input.draw(window); btn_train.draw(window); btn_back.draw(window);
        }
        else if (game_state == GameState::FEN_INPUT) {
            draw_menu_background(window);
            sf::Text title; title.setFont(font); title.setCharacterSize(50); title.setString("Load FEN"); title.setFillColor(ACCENT_COLOR);
            title.setPosition((WINDOW_WIDTH - title.getLocalBounds().width) / 2.f, 180); window.draw(title);
            fen_input.draw(window); btn_apply_fen.draw(window); btn_back.draw(window);
        }
        else if (game_state == GameState::PLAYING) {
            draw_board(window, ui.board_offset_x);

            // Подсветка последнего хода
            if (ui.last_move_from >= 0 && ui.last_move_to >= 0) {
                draw_last_move_highlight(window, ui.last_move_from, ui.last_move_to, ui.board_offset_x);
            }

            // Подсветка шаха
            draw_check_highlight(window, board, ui.board_offset_x);

            // Если включена визуализация мыслей и движок думает, рисуем visual_engine_board, иначе обычную board
            Position display_board;
            {
                std::lock_guard<std::mutex> lock(engine_mutex);
                display_board = (settings.show_analysis && engine_thinking) ? visual_engine_board : board;
            }

            // Рисуем фигуры
            draw_pieces(window, display_board, sprite_cache, ui.board_offset_x);

            if (settings.show_legal_moves) draw_legal_moves(window, ui.legal_moves_from_selection, ui.board_offset_x);
            if (settings.show_best_move) {
                std::lock_guard<std::mutex> lock(engine_mutex);
                draw_best_move_arrow(window, engine_best_move, ui.board_offset_x);
            }

            if (ui.sidebar_open) {
                sf::RectangleShape sidebar;
                sidebar.setPosition(ui.board_offset_x + BOARD_SIZE, 0);
                sidebar.setSize({SIDEBAR_WIDTH, WINDOW_HEIGHT});
                sidebar.setFillColor(PANEL_COLOR);
                window.draw(sidebar);

                sf::Text engine_text; engine_text.setFont(font); engine_text.setCharacterSize(18); engine_text.setFillColor(ACCENT_COLOR);
                {
                    std::lock_guard<std::mutex> lock(engine_mutex);
                    engine_text.setString(engine_thinking ? "Engine searching..." : engine_info);
                }
                engine_text.setPosition(ui.board_offset_x + BOARD_SIZE + 40, 40); window.draw(engine_text);

                sf::Text eval_text; eval_text.setFont(font); eval_text.setCharacterSize(16); eval_text.setFillColor(sf::Color(100, 200, 100));

                // Перевод Eval в стандартные пешки
                char eval_buf[32]; snprintf(eval_buf, sizeof(eval_buf), "Eval: %s%.2f", ui.eval_score >= 0 ? "+" : "", ui.eval_score / 100.0f);
                eval_text.setString(eval_buf); eval_text.setPosition(ui.board_offset_x + BOARD_SIZE + 40, 70); window.draw(eval_text);

                if (!ui.best_move_sequence.empty()) {
                    sf::Text best_moves_text; best_moves_text.setFont(font); best_moves_text.setCharacterSize(14); best_moves_text.setFillColor(TEXT_COLOR);
                    std::string best_line = "PV: ";
                    for (size_t i = 0; i < std::min(size_t(4), ui.best_move_sequence.size()); i++) best_line += ui.best_move_sequence[i] + " ";
                    best_moves_text.setString(best_line); best_moves_text.setPosition(ui.board_offset_x + BOARD_SIZE + 40, 100); window.draw(best_moves_text);
                }

                toggle_analysis_sidebar.draw(window);
                toggle_best_move_sidebar.draw(window);
                toggle_legal_moves_sidebar.draw(window);
                slider_time_sidebar.draw(window);
                btn_preset_1s_sidebar.draw(window); btn_preset_3s_sidebar.draw(window);
                btn_preset_5s_sidebar.draw(window); btn_preset_10s_sidebar.draw(window);
                slider_alpha_sidebar.draw(window);
                slider_beta_sidebar.draw(window);

                sf::Text move_header; move_header.setFont(font); move_header.setCharacterSize(18); move_header.setFillColor(ACCENT_COLOR);
                move_header.setString("Move History"); move_header.setPosition(ui.board_offset_x + BOARD_SIZE + 40, 570); window.draw(move_header);

                sf::Text history_text; history_text.setFont(font); history_text.setCharacterSize(16); history_text.setFillColor(sf::Color(200, 200, 200));
                std::stringstream history_ss;
                size_t start_idx = ui.move_history.size() > 24 ? ui.move_history.size() - 24 : 0;
                if (start_idx % 2 != 0) start_idx--;
                for (size_t i = start_idx; i < ui.move_history.size(); i++) {
                    if (i % 2 == 0) history_ss << (i / 2 + 1) << ". " << ui.move_history[i];
                    else history_ss << "  " << ui.move_history[i] << "\n";
                }
                history_text.setString(history_ss.str()); history_text.setPosition(ui.board_offset_x + BOARD_SIZE + 40, 610); window.draw(history_text);

                // Отрисовка статуса игры и кнопки New Game
                if (ui.game_over) {
                    sf::Text game_status_text; game_status_text.setFont(font); game_status_text.setCharacterSize(20);
                    game_status_text.setFillColor(sf::Color(255, 215, 0));
                    game_status_text.setString(ui.game_status);
                    game_status_text.setPosition(ui.board_offset_x + BOARD_SIZE + 40, 750);
                    window.draw(game_status_text);
                }
                btn_new_game.draw(window);
            }
            btn_toggle_sidebar.draw(window);
        }
        else if (game_state == GameState::PROMOTION_DIALOG) {
            // Рисуем доску и фигуры как в обычном состоянии
            draw_board(window, ui.board_offset_x);
            draw_pieces(window, board, sprite_cache, ui.board_offset_x);

            // Рисуем диалог выбора фигуры
            Color side = board.get_side_to_move();
            draw_promotion_dialog(window, sprite_cache, side, ui.promotion_to, ui.board_offset_x);

            // Рисуем кнопку toggle sidebar
            btn_toggle_sidebar.draw(window);
        }
        window.display();
    }
    settings.save_to_file("game_settings.json");
    return 0;
}
