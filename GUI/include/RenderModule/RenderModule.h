#pragma once

#include <SFML/Graphics.hpp>
#include <tuple>

/**
 * @struct RenderCommand
 * @brief Lightweight structure representing a single draw instruction.
 */
struct RenderCommand {
    const sf::Drawable*   drawing       = nullptr;  /// Pointer to the object to be rendered.
    const sf::View*       view          = nullptr;  /// Target view for rendering.
    unsigned int          viewPriority  = 0;        /// Priority of the View (Background < HUD).
    unsigned int          zIndex        = 0;        /// Depth index within the view.
};
/**
 * @brief Sorting criteria for draw calls.
 * 
 * Priority order:
 * 
 * 1. View Priority (Layering different cameras/HUDs).
 * 
 * 2. View Address (Grouping by View to minimize costly sf::RenderTarget::setView calls).
 * 
 * 3. Z-Index (Ordering objects within a single View).
 */
inline constexpr auto RenderCommandComparator = 
    [](const RenderCommand& a, const RenderCommand& b) 
{ 
    return a.viewPriority != b.viewPriority 
        ? a.viewPriority < b.viewPriority 
        : (a.view != b.view ? a.view < b.view : a.zIndex < b.zIndex); 
};