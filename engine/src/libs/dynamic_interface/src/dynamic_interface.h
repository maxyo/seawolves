#pragma once

// DynamicInterfaceService: the interface groups a game's scripts describe in attribute trees and register by name
// (UI_GROUP_REGISTER "sall": name, root, hosts, update mode). The message ids are the game's own #defines.

#include "entity.h"

#include <cstdint>
#include <map>
#include <string>

class ATTRIBUTES;

class DynamicInterfaceService : public Entity
{
  public:
    struct Group
    {
        ATTRIBUTES *root = nullptr;
        int32_t hosts = 0; // UI_GROUP_LAND | SEA | WM | LOG of the game's header
        bool every_frame = false;
        bool refresh = false; // asked for by UI_GROUP_REFRESH
    };

    bool Init() override;
    uint64_t ProcessMessage(MESSAGE &message) override;
    void ProcessStage(Stage, uint32_t) override
    {
    }

  private:
    int32_t register_ = 0, refresh_ = 0, remove_ = 0, exists_ = 0;
    std::map<std::string, Group> groups_;
};
