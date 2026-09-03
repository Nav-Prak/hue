// engine/ecs/include/hue/ecs/ecs.h
//
// Week 8 sparse-set ECS. Entities are 32-bit index + 32-bit generation
// handles; each component type lives in its own sparse-set pool (dense
// arrays for iteration, sparse table for O(1) lookup). Structural changes
// made during iteration are deferred into per-pool command lists and a
// world-level destroy list, then applied by World::flush().
//
// No exceptions, no RTTI: component types get stable indices from a
// monotonic counter, and the world tracks pools through a small table of
// function pointers rather than virtual dispatch.

#pragma once

#include "hue/core/array.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"

#include <cstdint>
#include <new>
#include <utility>

namespace hue::ecs {

// ------------------------------------------------------------------ entity

using Entity = std::uint64_t;

inline constexpr Entity kInvalidEntity = 0;
inline constexpr std::uint32_t kMaxEntities = 64u * 1024u;

[[nodiscard]] constexpr std::uint32_t entity_index(Entity entity) noexcept {
    return static_cast<std::uint32_t>(entity & 0xffffffffu);
}
[[nodiscard]] constexpr std::uint32_t entity_generation(Entity entity) noexcept {
    return static_cast<std::uint32_t>(entity >> 32);
}
[[nodiscard]] constexpr Entity make_entity(std::uint32_t index, std::uint32_t generation) noexcept {
    return (static_cast<Entity>(generation) << 32) | index;
}

// Generation 0 is never issued, so a zero-initialized Entity is invalid.
class EntityRegistry {
public:
    EntityRegistry()
        : m_generations(MemoryTag::kEcs), m_free_indices(MemoryTag::kEcs) {}

    [[nodiscard]] Result<Entity> create() noexcept {
        if (!m_free_indices.empty()) {
            const std::uint32_t index = m_free_indices[m_free_indices.size() - 1];
            (void)m_free_indices.pop_back();
            ++m_alive_count;
            return make_entity(index, m_generations[index]);
        }
        if (m_generations.size() >= kMaxEntities) {
            return ErrorCode::kOutOfMemory;
        }
        const std::uint32_t index = static_cast<std::uint32_t>(m_generations.size());
        if (!m_generations.push_back(1u)) {
            return ErrorCode::kOutOfMemory;
        }
        ++m_alive_count;
        return make_entity(index, 1u);
    }

    // Bumps the slot generation so stale handles are rejected forever.
    [[nodiscard]] Result<void> destroy(Entity entity) noexcept {
        if (!alive(entity)) {
            return ErrorCode::kInvalidArgument;
        }
        const std::uint32_t index = entity_index(entity);
        ++m_generations[index];
        if (!m_free_indices.push_back(index)) {
            return ErrorCode::kOutOfMemory;
        }
        --m_alive_count;
        return {};
    }

    [[nodiscard]] bool alive(Entity entity) const noexcept {
        const std::uint32_t index = entity_index(entity);
        return entity != kInvalidEntity && index < m_generations.size() &&
               m_generations[index] == entity_generation(entity);
    }

    [[nodiscard]] std::size_t alive_count() const noexcept { return m_alive_count; }

private:
    Array<std::uint32_t> m_generations;  // per-slot current generation
    Array<std::uint32_t> m_free_indices; // recycled slots
    std::size_t m_alive_count = 0;
};

// -------------------------------------------------------------------- pool

namespace detail {

inline constexpr std::uint32_t kNullDense = 0xffffffffu;

[[nodiscard]] inline std::uint32_t next_type_index() noexcept {
    static std::uint32_t counter = 0;
    return counter++;
}

template <typename T> [[nodiscard]] std::uint32_t type_index() noexcept {
    static const std::uint32_t index = next_type_index();
    return index;
}

} // namespace detail

// Sparse-set component storage. Dense arrays iterate cache-friendly; the
// sparse table maps entity index -> dense slot. Removal swap-pops so the
// dense arrays stay tightly packed.
template <typename T> class ComponentPool {
public:
    ComponentPool()
        : m_sparse(MemoryTag::kEcs), m_entities(MemoryTag::kEcs), m_components(MemoryTag::kEcs),
          m_pending_add_entities(MemoryTag::kEcs), m_pending_add_values(MemoryTag::kEcs),
          m_pending_removes(MemoryTag::kEcs) {}

    [[nodiscard]] Result<void> add(Entity entity, T value) noexcept {
        const std::uint32_t index = entity_index(entity);
        if (index >= kMaxEntities) {
            return ErrorCode::kInvalidArgument;
        }
        auto grown = ensure_sparse(index);
        if (!grown) {
            return grown.error();
        }
        if (m_sparse[index] != detail::kNullDense) {
            m_components[m_sparse[index]] = std::move(value); // overwrite in place
            m_entities[m_sparse[index]] = entity;
            return {};
        }
        const std::uint32_t dense = static_cast<std::uint32_t>(m_entities.size());
        if (!m_entities.push_back(entity)) {
            return ErrorCode::kOutOfMemory;
        }
        if (!m_components.push_back(std::move(value))) {
            (void)m_entities.pop_back();
            return ErrorCode::kOutOfMemory;
        }
        m_sparse[index] = dense;
        return {};
    }

    bool remove(Entity entity) noexcept {
        const std::uint32_t index = entity_index(entity);
        if (index >= m_sparse.size() || m_sparse[index] == detail::kNullDense ||
            m_entities[m_sparse[index]] != entity) {
            return false;
        }
        const std::uint32_t dense = m_sparse[index];
        const std::uint32_t last = static_cast<std::uint32_t>(m_entities.size()) - 1;
        if (dense != last) {
            m_entities[dense] = m_entities[last];
            m_components[dense] = std::move(m_components[last]);
            m_sparse[entity_index(m_entities[dense])] = dense;
        }
        (void)m_entities.pop_back();
        (void)m_components.pop_back();
        m_sparse[index] = detail::kNullDense;
        return true;
    }

    [[nodiscard]] bool has(Entity entity) const noexcept {
        const std::uint32_t index = entity_index(entity);
        return index < m_sparse.size() && m_sparse[index] != detail::kNullDense &&
               m_entities[m_sparse[index]] == entity;
    }

    [[nodiscard]] T* get(Entity entity) noexcept {
        return has(entity) ? &m_components[m_sparse[entity_index(entity)]] : nullptr;
    }
    [[nodiscard]] const T* get(Entity entity) const noexcept {
        return has(entity) ? &m_components[m_sparse[entity_index(entity)]] : nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_entities.size(); }
    [[nodiscard]] Entity entity_at(std::size_t dense) const noexcept {
        return m_entities[dense];
    }
    [[nodiscard]] T& component_at(std::size_t dense) noexcept { return m_components[dense]; }
    [[nodiscard]] const T& component_at(std::size_t dense) const noexcept {
        return m_components[dense];
    }

    // Deferred structural changes: safe to call while iterating this pool.
    [[nodiscard]] Result<void> deferred_add(Entity entity, T value) noexcept {
        if (!m_pending_add_entities.push_back(entity)) {
            return ErrorCode::kOutOfMemory;
        }
        if (!m_pending_add_values.push_back(std::move(value))) {
            (void)m_pending_add_entities.pop_back();
            return ErrorCode::kOutOfMemory;
        }
        return {};
    }

    [[nodiscard]] Result<void> deferred_remove(Entity entity) noexcept {
        return m_pending_removes.push_back(entity);
    }

    [[nodiscard]] Result<void> flush(const EntityRegistry& registry) noexcept {
        for (std::size_t i = 0; i < m_pending_removes.size(); ++i) {
            (void)remove(m_pending_removes[i]);
        }
        m_pending_removes.clear();
        for (std::size_t i = 0; i < m_pending_add_entities.size(); ++i) {
            if (!registry.alive(m_pending_add_entities[i])) {
                continue; // destroyed before the flush; drop the add
            }
            auto added = add(m_pending_add_entities[i], std::move(m_pending_add_values[i]));
            if (!added) {
                m_pending_add_entities.clear();
                m_pending_add_values.clear();
                return added.error();
            }
        }
        m_pending_add_entities.clear();
        m_pending_add_values.clear();
        return {};
    }

private:
    [[nodiscard]] Result<void> ensure_sparse(std::uint32_t index) noexcept {
        while (m_sparse.size() <= index) {
            if (!m_sparse.push_back(detail::kNullDense)) {
                return ErrorCode::kOutOfMemory;
            }
        }
        return {};
    }

    Array<std::uint32_t> m_sparse;
    Array<Entity> m_entities;
    Array<T> m_components;
    Array<Entity> m_pending_add_entities;
    Array<T> m_pending_add_values;
    Array<Entity> m_pending_removes;
};

// ------------------------------------------------------------------- world

inline constexpr std::uint32_t kMaxComponentTypes = 32;

// Owns the entity registry and one lazily-created pool per component type.
// Pools are stored type-erased with function-pointer thunks (no virtuals).
class World {
public:
    World() : m_pending_destroys(MemoryTag::kEcs) {}
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World& operator=(World&&) = delete;

    World(World&& other) noexcept
        : m_entities(std::move(other.m_entities)),
          m_pending_destroys(std::move(other.m_pending_destroys)) {
        for (std::uint32_t i = 0; i < kMaxComponentTypes; ++i) {
            m_pools[i] = other.m_pools[i];
            other.m_pools[i] = {};
        }
    }

    ~World() {
        for (std::uint32_t i = 0; i < kMaxComponentTypes; ++i) {
            if (m_pools[i].pool != nullptr) {
                m_pools[i].destroy(m_pools[i].pool);
            }
        }
    }

    [[nodiscard]] Result<Entity> create() noexcept { return m_entities.create(); }

    [[nodiscard]] bool alive(Entity entity) const noexcept { return m_entities.alive(entity); }
    [[nodiscard]] std::size_t alive_count() const noexcept { return m_entities.alive_count(); }

    // Immediate destroy: removes the entity's components from every pool.
    // Not safe during iteration; use destroy_deferred there.
    [[nodiscard]] Result<void> destroy(Entity entity) noexcept {
        auto destroyed = m_entities.destroy(entity);
        if (!destroyed) {
            return destroyed.error();
        }
        for (std::uint32_t i = 0; i < kMaxComponentTypes; ++i) {
            if (m_pools[i].pool != nullptr) {
                m_pools[i].remove(m_pools[i].pool, entity);
            }
        }
        return {};
    }

    [[nodiscard]] Result<void> destroy_deferred(Entity entity) noexcept {
        return m_pending_destroys.push_back(entity);
    }

    template <typename T> [[nodiscard]] Result<ComponentPool<T>*> pool() noexcept {
        const std::uint32_t type = detail::type_index<T>();
        if (type >= kMaxComponentTypes) {
            return ErrorCode::kOutOfMemory;
        }
        if (m_pools[type].pool == nullptr) {
            auto memory =
                heap_allocate(sizeof(ComponentPool<T>), alignof(ComponentPool<T>),
                              MemoryTag::kEcs);
            if (!memory) {
                return memory.error();
            }
            m_pools[type].pool = new (memory.value()) ComponentPool<T>();
            m_pools[type].destroy = [](void* pool) {
                static_cast<ComponentPool<T>*>(pool)->~ComponentPool<T>();
                (void)heap_free(pool);
            };
            m_pools[type].remove = [](void* pool, Entity entity) {
                (void)static_cast<ComponentPool<T>*>(pool)->remove(entity);
            };
            m_pools[type].flush = [](void* pool, const EntityRegistry& registry) {
                return static_cast<ComponentPool<T>*>(pool)->flush(registry);
            };
        }
        return static_cast<ComponentPool<T>*>(m_pools[type].pool);
    }

    template <typename T> [[nodiscard]] Result<void> add(Entity entity, T value) noexcept {
        if (!m_entities.alive(entity)) {
            return ErrorCode::kInvalidArgument;
        }
        auto p = pool<T>();
        if (!p) {
            return p.error();
        }
        return p.value()->add(entity, std::move(value));
    }

    template <typename T> [[nodiscard]] T* get(Entity entity) noexcept {
        auto p = pool<T>();
        return p ? p.value()->get(entity) : nullptr;
    }

    // Applies deferred pool changes (removes, then adds) and then deferred
    // entity destroys. Call once per frame, outside any iteration.
    [[nodiscard]] Result<void> flush() noexcept {
        for (std::uint32_t i = 0; i < kMaxComponentTypes; ++i) {
            if (m_pools[i].pool != nullptr) {
                auto flushed = m_pools[i].flush(m_pools[i].pool, m_entities);
                if (!flushed) {
                    return flushed.error();
                }
            }
        }
        for (std::size_t i = 0; i < m_pending_destroys.size(); ++i) {
            if (m_entities.alive(m_pending_destroys[i])) {
                auto destroyed = destroy(m_pending_destroys[i]);
                if (!destroyed) {
                    return destroyed.error();
                }
            }
        }
        m_pending_destroys.clear();
        return {};
    }

private:
    struct PoolEntry {
        void* pool = nullptr;
        void (*destroy)(void*) = nullptr;
        void (*remove)(void*, Entity) = nullptr;
        Result<void> (*flush)(void*, const EntityRegistry&) = nullptr;
    };

    EntityRegistry m_entities;
    PoolEntry m_pools[kMaxComponentTypes];
    Array<Entity> m_pending_destroys;
};

// ------------------------------------------------------------------ queries

// Iterate all entities that have every listed component. The first pool
// drives iteration (pass the smallest first); the rest are probed. The
// callback receives (Entity, A&, B&, ...). Structural changes during the
// walk must go through the deferred APIs.
template <typename A, typename Fn> void for_each(ComponentPool<A>& a, Fn&& fn) {
    for (std::size_t i = a.size(); i-- > 0;) {
        fn(a.entity_at(i), a.component_at(i));
    }
}

template <typename A, typename B, typename Fn>
void for_each(ComponentPool<A>& a, ComponentPool<B>& b, Fn&& fn) {
    for (std::size_t i = a.size(); i-- > 0;) {
        const Entity entity = a.entity_at(i);
        B* other = b.get(entity);
        if (other != nullptr) {
            fn(entity, a.component_at(i), *other);
        }
    }
}

template <typename A, typename B, typename C, typename Fn>
void for_each(ComponentPool<A>& a, ComponentPool<B>& b, ComponentPool<C>& c, Fn&& fn) {
    for (std::size_t i = a.size(); i-- > 0;) {
        const Entity entity = a.entity_at(i);
        B* second = b.get(entity);
        C* third = c.get(entity);
        if (second != nullptr && third != nullptr) {
            fn(entity, a.component_at(i), *second, *third);
        }
    }
}

} // namespace hue::ecs
