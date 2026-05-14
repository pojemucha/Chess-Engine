#pragma once

#include <SFML/Graphics.hpp>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "RenderEntity.h"
#include "RenderModule.h"

/**
 * @class RenderManager
 * @brief High-level orchestration of the rendering pipeline.
 * 
 * Performs the following steps every frame:
 * 
 * 1. Frustum Culling: Skips entities outside the current View bounds.
 * 
 * 2. Batch Sorting: Groups entities by ViewPriority and View address to minimize state changes.
 * 
 * 3. Layering: Respects Z-Index within each view group.
 */
class RenderManager {
private:
    std::vector<RenderCommand>                          m_commands;         /// Container for draw commands to determine rendering sequence.
    std::unordered_map<const sf::View*, sf::FloatRect>  m_cachedViewBounds; /// Caches calculated View bounds to avoid redundant math during culling.
    sf::View                                            m_defaultView;      /// Fallback view (matches window's default view) for entities without an assigned view.

    /**
     * @brief Calculates the world-space bounding box of a given sf::View.
     * @param view The sf::View to calculate.
     * @return sf::FloatRect The visible area of the world.
     */
    static sf::FloatRect calculateViewBounds(const sf::View& view) 
    {
        const sf::Vector2f size     = view.getSize();
        const sf::Vector2f center   = view.getCenter();

        return { center.x - size.x * 0.5f, center.y - size.y * 0.5f, size.x, size.y };
    };

public:
    /** 
     * @brief Constructs the RenderManager.
     * @param window The target window, used to initialize the default view.
     */
    explicit RenderManager(sf::RenderWindow& window) : m_defaultView(window.getDefaultView()) {}
    ~RenderManager() = default;
    
    RenderManager(const RenderManager& other) = delete;
    RenderManager& operator=(const RenderManager& other) = delete;

    RenderManager(RenderManager&&) = default;
    RenderManager& operator=(RenderManager&&) = default;

    /** 
     * @brief Submits an entity for the current frame's render queue. 
     * 
     * Per-frame submission allows for dynamic frustum culling and depth sorting.
     * @param entity The entity to be drawn. It must remain valid until display() is called.
     */
    void submit(const IRenderEntity& entity) {
        const sf::View* view = entity.getView() ? entity.getView() : &m_defaultView;
        auto [it, inserted] = m_cachedViewBounds.try_emplace(view, calculateViewBounds(*view));
        
        if (!it->second.intersects(entity.getBounds())) return;

        m_commands.push_back( { &entity, view, entity.getViewPriority(), entity.getZIndex() } );
    }
    
    /**
     * @brief Sorts and dispatches all submitted render commands to the window.
     * 
     * Processes the render queue and automatically clears it upon completion.
     * @param window The sf::RenderWindow to draw to.
     */
    void display(sf::RenderWindow& window) {
        if (m_commands.empty())
            return;
        
        const sf::View windowView = window.getView();
        std::sort(m_commands.begin(), m_commands.end(), RenderCommandComparator);
        
        const sf::View* lastView = nullptr;
        for(auto& cmd : m_commands) {
            if (cmd.view != lastView) {
                lastView = cmd.view;
                window.setView(*lastView);
            }
            window.draw(*cmd.drawing);
        }

        window.setView(windowView);
        clearFrame();
    }
    /**
     * @brief Clears the render queue and view cache.
     * 
     * Prepares the manager for the next frame. This is called automatically by display().
     */
    void clearFrame() {
        m_commands.clear();
        m_cachedViewBounds.clear();
    }
};