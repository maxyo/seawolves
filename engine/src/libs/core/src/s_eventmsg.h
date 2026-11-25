#pragma once

#include "message.h"

#include <string_view>
#include <utility>
#include <vector>
#include "platform/platform.hpp"

class S_EVENTMSG
{
  public:
    uint32_t nTime;
    uint32_t nPeriod;

  public:
    MESSAGE *pMessageClass;
    char *pEventName;
    bool bProcess;
    bool bInvalide;
    // The attribute parameters of the message: the position in the format, the attribute as it was posted.
    std::vector<std::pair<size_t, ATTRIBUTES::Handle>> attributes;

    S_EVENTMSG()
    {
        bInvalide = false;
        bProcess = false;
        pMessageClass = nullptr;
        nTime = 0;
        nPeriod = 0;
        pEventName = nullptr;
    };

    S_EVENTMSG(const char *_name, MESSAGE *_pc, uint32_t _period)
    {
        bInvalide = false;
        bProcess = false;
        pMessageClass = _pc;
        nTime = 0;
        nPeriod = _period;

        const auto len = strlen(_name) + 1;
        pEventName = new char[len];
        strcpy_s(pEventName, len, _name);
    };

    ~S_EVENTMSG()
    {
        if (pMessageClass)
            delete pMessageClass;
        if (pEventName)
            delete[] pEventName;
    };

    bool ProcessTime(uint32_t _DeltaTime)
    {
        nTime += _DeltaTime;
        bProcess = true;
        if (nPeriod == 0)
            return true;
        if (nTime >= nPeriod)
            return true;
        return false;
    };

    void Invalidate()
    {
        bInvalide = true;
    };

    // The message's attribute arguments as posted: they may be deleted before the event's time.
    void NoteAttributes()
    {
        if (!pMessageClass)
            return;
        const std::string_view format = pMessageClass->GetFormat();
        for (size_t n = 0; n < format.size(); n++)
            if (ATTRIBUTES *attribute = format[n] == 'a' ? pMessageClass->AttributeAt(n) : nullptr)
                attributes.emplace_back(n, attribute->GetHandle());
    }

    // An attribute argument deleted since the event was posted is handed on as an empty aref, and report(position)
    // says so.
    template <class Report> void ForgetDeletedAttributes(Report &&report)
    {
        for (const auto &[position, handle] : attributes)
        {
            if (ATTRIBUTES::IsAlive(handle))
                continue;
            report(position);
            pMessageClass->SetAttributeAt(position, nullptr);
        }
    }
};
