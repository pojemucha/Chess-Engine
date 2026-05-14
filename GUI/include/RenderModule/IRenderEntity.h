#pragma once

#include <SFML/Graphics.hpp>

/**
/**
 * @class IRenderEntity
 * @brief Base interface for all renderable engine objects.
 * 
 * Provides spatial layering properties (Z-index, View association, View priority) 
 * to allow the RenderManager to sort and cull objects before drawing.
 * 
 * @note This class does not own the View or the underlying Drawable. 
 * The lifetime of these pointers/references must be managed externally.
 */
class IRenderEntity : public sf::Drawable {
protected:
    sf::View*       m_view         = nullptr; /// Target view for rendering (nullptr = fallback to default window view).
    unsigned int    m_viewPriority = 0;       /// Sorting priority of the View (e.g., Background = 0, HUD = 10).
    unsigned int    m_zIndex       = 0;       /// Depth within the view (default 0).

public:
    /** @brief Default constructor. */
    IRenderEntity() = default;

    /** @brief Virtual destructor to ensure proper cleanup of derived classes. */
    virtual ~IRenderEntity() = default;

    /** @brief Default copy constructor. */
    IRenderEntity(const IRenderEntity&) = default;
    
    /** @brief Default assignment operator. */
    IRenderEntity& operator=(const IRenderEntity&) = default;

    /** 
     * @brief Retrieves the global bounding rectangle of the entity. 
     * @return sf::FloatRect The world-space bounds, used for frustum culling.
     */
    virtual sf::FloatRect getBounds() const = 0;
    
    /** 
     * @brief Standard sf::Drawable draw function.
     * @param target Render target to draw to.
     * @param states Current render states.
     */
    virtual void draw(sf::RenderTarget& target, sf::RenderStates states) const override = 0;
    
    /** 
     * @brief Retrieves the assigned View of the object. 
     * @return sf::View* Pointer to the current View, or nullptr if using default.
     */
    const sf::View* getView() const noexcept {
        return m_view;
    }
    
    /**
     * @brief Retrieves the view priority of the object.
     * @return unsigned int The priority level (lower values are rendered first).
     */
    unsigned int getViewPriority() const noexcept {
        return m_viewPriority;
    } 
    
    /**
     * @brief Retrieves the current Z-index of the object.
     * @return unsigned int The depth index. The larger the value, the closer the object appears to the foreground.
     */
    unsigned int getZIndex() const noexcept {
        return m_zIndex;
    }
    
    /**
     * @brief Assigns the entity to a specific sf::View.
     * @param view Pointer to the sf::View.
     */
    void setView(sf::View* view) noexcept {
        m_view = view;
    }
    
    /**
     * @brief Sets the rendering priority for the associated view. 
     * @param priority The new priority level. Objects with lower priority are rendered first.
     */
    void setViewPriority(unsigned int priority) noexcept {
        m_viewPriority = priority;
    }
    
    /**
     * @brief Sets the draw order (depth) of the object within its view.
     * @param zIndex The new Z-index. The larger the value, the closer the object appears to the foreground.
     */
    void setZIndex(unsigned int zIndex) noexcept {
        m_zIndex = zIndex;
    }
};