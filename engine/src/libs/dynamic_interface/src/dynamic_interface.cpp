#include "dynamic_interface.h"

#include "core.h"
#include "vma.hpp"

#include <spdlog/spdlog.h>

CREATE_CLASS(DynamicInterfaceService)

bool DynamicInterfaceService::Init()
{
    const auto id = [](const char *name, int32_t &value) {
        const auto define = core.GetScriptIntegerDefine(name);
        if (!define)
            spdlog::error("DynamicInterfaceService: the scripts define no {}", name);
        value = define.value_or(-1);
        return define.has_value();
    };
    return id("UI_GROUP_REGISTER", register_) & id("UI_GROUP_REFRESH", refresh_) & id("UI_GROUP_REMOVE", remove_) &
           id("UI_GROUP_EXISTS", exists_);
}

uint64_t DynamicInterfaceService::ProcessMessage(MESSAGE &message)
{
    const int32_t code = message.Long();
    const std::string name = message.String();
    if (code == register_)
    {
        Group group;
        group.root = message.AttributePointer();
        group.hosts = message.Long();
        const int32_t mode = message.Long();
        if (mode != 0 && mode != 1)
        {
            spdlog::error("DynamicInterface: invalid update mode {} for group '{}'", mode, name);
            return 0;
        }
        group.every_frame = mode == 1;
        if (groups_.empty())
            spdlog::warn("DynamicInterfaceService: groups are registered but not drawn (not implemented)");
        groups_[name] = group;
        return 1;
    }
    const auto found = groups_.find(name);
    if (code == exists_)
        return found != groups_.end();
    if (code == refresh_)
    {
        if (found != groups_.end())
            found->second.refresh = true;
        return found != groups_.end();
    }
    if (code == remove_)
    {
        if (found != groups_.end())
            groups_.erase(found);
        return 1;
    }
    spdlog::error("DynamicInterfaceService: unknown message {}", code);
    return 0;
}
