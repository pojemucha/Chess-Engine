#pragma once

#include <type_traits>

#include "IRenderEntity.h"

/**
 * @class RenderEntity
 * @brief A type-safe wrapper that adds rendering properties to standard SFML Drawables.
 * 
 * @tparam T The SFML object type (e.g., sf::Sprite, sf::Text). Must inherit from sf::Drawable.
 */
template <typename T>
class RenderEntity : public IRenderEntity {
    static_assert(std::is_base_of<sf::Drawable, T>::value, "T must be a child of sf::Drawable to be wrapped in RenderEntity");

private:
    const T& m_drawable; /// Reference to the target Drawable object (managed externally).

public:
    /**
     * @brief Constructs a RenderEntity wrapper around an existing SFML Drawable.
     * @param drawable The target object to be rendered. Must remain valid while this wrapper exists.
     */
    RenderEntity(const T& drawable) : m_drawable(drawable) {}
    
    /** @brief Retrieves the global bounding rectangle of the wrapped object. */
    sf::FloatRect getBounds() const override {
        return m_drawable.getGlobalBounds();    
    }

    /** @brief Dispatches the draw call to the wrapped SFML object. */
    void draw(sf::RenderTarget& target, sf::RenderStates states) const override {
        target.draw(m_drawable, states);
    }
};