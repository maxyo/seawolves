#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "entity_manager.h"
#include "vma.hpp"

#include <catch2/catch.hpp>

namespace
{
struct Probe final : Entity
{
    static inline EntityManager *manager;
    static inline bool initResult = true;
    static inline entid_t lastInitialized;
    static inline entid_t eraseDuringDestruction;
    static inline entid_t destroyTrigger;
    static inline std::vector<entid_t> destroyed;

    bool Init() override
    {
        lastInitialized = GetId();
        return initResult;
    }
    void ProcessStage(Stage, uint32_t) override
    {
    }
    ~Probe() override
    {
        destroyed.push_back(GetId());
        if (GetId() == destroyTrigger)
            manager->EraseEntity(eraseDuringDestruction);
    }
};
CREATE_CLASS(Probe)

uint32_t index(entid_t id)
{
    return static_cast<uint32_t>(id);
}
uint32_t generation(entid_t id)
{
    return static_cast<uint32_t>(id >> 32);
}

void lifecycle()
{
    EntityManager manager;
    Probe::manager = &manager;
    Probe::destroyed.clear();
    const auto a = manager.CreateEntity("Probe");
    const auto b = manager.CreateEntity("pRoBe");
    const auto c = manager.CreateEntity("Probe");
    REQUIRE((index(a) == 0 && index(b) == 1 && index(c) == 2));                // initial indices
    REQUIRE((generation(a) == 1 && generation(c) == 3));                       // initial generations
    REQUIRE(manager.GetClassCode(a) == MakeHashValue("Probe"));                // class code
    REQUIRE(manager.GetEntityIds("Probe") == std::vector<entid_t>({a, b, c})); // cache initial fill
    manager.SetLayerType(0, layer_type_t::execute);
    manager.AddToLayer(0, c, 20);
    manager.AddToLayer(0, a, 10);
    manager.AddToLayer(0, b, 10);
    REQUIRE(manager.GetEntityIds(layer_index_t{0}) == std::vector<entid_t>({a, b, c})); // layer priorities
    auto *oldB = manager.GetEntityPointer(b);
    manager.EraseEntity(b);
    manager.EraseEntity(b); // repeated erase must not queue a second destructor
    manager.EraseEntity(a);
    REQUIRE((!manager.IsEntityValid(b) && manager.GetEntityPointer(b) == oldB)); // pending deletion visibility
    REQUIRE(Probe::destroyed.empty());                                           // destruction must be deferred
    REQUIRE(manager.GetEntityIds("Probe") == std::vector<entid_t>({c})); // cache invalidation before destruction
    std::vector<entid_t> visited;
    manager.ForEachEntity([&](Entity *entity) { visited.push_back(entity->GetId()); });
    REQUIRE(visited == std::vector<entid_t>({c})); // ForEach skips pending deletions
    const auto d = manager.CreateEntity("Probe");
    REQUIRE(index(d) == 3); // pending slots must not be reused
    manager.NewLifecycle();
    REQUIRE(Probe::destroyed == std::vector<entid_t>({a, b}));                    // deferred destruction LIFO
    REQUIRE(manager.GetEntityPointer(b) == nullptr);                              // destroyed pointer
    REQUIRE(manager.GetEntityIds(layer_index_t{0}) == std::vector<entid_t>({c})); // layer cleanup
    const auto e = manager.CreateEntity("Probe");
    const auto f = manager.CreateEntity("Probe");
    REQUIRE((index(e) == index(b) && index(f) == index(a)));                           // double LIFO reuse order
    REQUIRE((generation(e) == 5 && generation(f) == 6));                               // generation survives slot reuse
    REQUIRE((!manager.IsEntityValid(a) && !manager.IsEntityValid(b)));                 // stale generations rejected
    REQUIRE((manager.GetEntityPointer(a) == nullptr && manager.GetClassCode(b) == 0)); // stale lookups rejected
    manager.EraseEntity(b);
    REQUIRE(manager.IsEntityValid(e)); // stale erase must not erase replacement
    REQUIRE(manager.GetEntityIds("Probe") == std::vector<entid_t>({c, d, e, f})); // cache order after slot reuse
    manager.NewLifecycle();
    REQUIRE(Probe::destroyed.size() == 2); // empty lifecycle
    manager.EraseAll();
    REQUIRE(Probe::destroyed == std::vector<entid_t>({a, b, f, e, c, d})); // EraseAll visits occupied slots once
    REQUIRE((manager.GetEntityIds("Probe").empty() &&
             manager.GetEntityIds(layer_index_t{0}).empty())); // EraseAll clears cache and layers
    const auto reset = manager.CreateEntity("Probe");
    REQUIRE((index(reset) == index(a) &&
             generation(reset) > generation(f))); // EraseAll reuses slots without reusing generations
    REQUIRE((!manager.IsEntityValid(a) && manager.GetEntityPointer(a) == nullptr &&
             manager.GetClassCode(a) == 0)); // handle retained across EraseAll must not resolve to a new entity
    manager.EraseEntity(a);
    manager.NewLifecycle();
    REQUIRE((manager.IsEntityValid(reset) &&
             manager.GetEntityId("Probe") == reset)); // stale handle must not erase replacement after EraseAll
    manager.EraseAll();
    manager.EraseAll(); // empty resets must not rewind the generation counter either
    const auto again = manager.CreateEntity("Probe");
    REQUIRE((index(again) == index(reset) &&
             generation(again) > generation(reset))); // generation survives repeated and empty EraseAll
    REQUIRE((!manager.IsEntityValid(reset) &&
             manager.GetEntityPointer(reset) == nullptr)); // previous reset generation stays invalid
    manager.EraseEntity(reset);
    manager.EraseEntity(a);
    manager.NewLifecycle();
    REQUIRE(
        (manager.IsEntityValid(again) &&
         manager.GetEntityIds("Probe") == std::vector<entid_t>({again}))); // all earlier reset handles remain harmless
    manager.EraseAll();
}

void failedInitializationAndReentrantDelete()
{
    EntityManager manager;
    Probe::manager = &manager;
    Probe::destroyed.clear();
    Probe::initResult = false;
    REQUIRE(manager.CreateEntity("Probe") == invalid_entity); // failed Init return value
    const auto failed = Probe::lastInitialized;
    REQUIRE((!manager.IsEntityValid(failed) && manager.GetEntityPointer(failed))); // failed Init awaits lifecycle
    REQUIRE(manager.GetEntityIds("Probe").empty());                                // failed Init removed from cache
    Probe::initResult = true;
    const auto survivor = manager.CreateEntity("Probe");
    REQUIRE(index(survivor) == 1); // failed Init slot not reused early
    manager.NewLifecycle();
    REQUIRE(Probe::destroyed == std::vector<entid_t>({failed})); // failed Init destroyed once
    const auto trigger = manager.CreateEntity("Probe");
    REQUIRE((index(trigger) == index(failed) && generation(trigger) == 3)); // failed Init slot reuse
    Probe::destroyTrigger = trigger;
    Probe::eraseDuringDestruction = survivor;
    manager.EraseEntity(trigger);
    manager.NewLifecycle();
    REQUIRE(
        Probe::destroyed ==
        std::vector<entid_t>({failed, trigger, survivor})); // destructor-enqueued deletion drained in same lifecycle
    const auto next = manager.CreateEntity("Probe");
    REQUIRE(index(next) == index(survivor)); // reentrant delete free order
    Probe::destroyTrigger = invalid_entity;
    manager.EraseAll();
}

void growth()
{
    EntityManager manager;
    Probe::manager = &manager;
    Probe::destroyed.clear();
    constexpr size_t count = 10000;
    std::vector<entid_t> ids;
    std::vector<entid_t> erased;
    for (size_t i = 0; i < count; ++i)
        ids.push_back(manager.CreateEntity("Probe"));
    // Fill the real deletion and free stacks across many allocation boundaries.
    for (size_t parity = 0; parity < 2; ++parity)
        for (size_t i = parity; i < count; i += 2)
        {
            manager.EraseEntity(ids[i]);
            erased.push_back(ids[i]);
        }
    manager.NewLifecycle();
    REQUIRE(std::equal(erased.rbegin(), erased.rend(), Probe::destroyed.begin(),
                       Probe::destroyed.end())); // growth destruction order
    for (size_t i = 0; i < count; ++i)
    {
        const auto replacement = manager.CreateEntity("Probe");
        REQUIRE(index(replacement) == index(erased[i]));   // growth reuse order
        REQUIRE(generation(replacement) == count + i + 1); // growth generation
        REQUIRE(!manager.IsEntityValid(erased[i]));        // growth stale ID
    }
    manager.EraseAll();
    REQUIRE(Probe::destroyed.size() == count * 2); // growth no leaks or duplicate deletion
    // EraseAll also clears queued deletions and existing free indices.
    const auto first = manager.CreateEntity("Probe");
    const auto second = manager.CreateEntity("Probe");
    manager.EraseEntity(first);
    manager.NewLifecycle();
    manager.EraseEntity(second);
    manager.EraseAll();
    manager.NewLifecycle();
    const auto reset = manager.CreateEntity("Probe");
    REQUIRE((index(reset) == index(ids[0]) &&
             generation(reset) > generation(second))); // reset after mixed pending/free slots preserves generation
    manager.EraseAll();
}
} // namespace

TEST_CASE("std_storage.entity_manager")
{
    Probevmaci.SetHash(MakeHashValue("Probe"));
    lifecycle();
    failedInitializationAndReentrantDelete();
    growth();
    std::cout << "PASS: real EntityManager lifecycle, cache/layers, generations, reuse and growth\n";
}
