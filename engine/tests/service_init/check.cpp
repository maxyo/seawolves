// A service whose Init refuses or throws is not kept (InitService, service.h): the next request tries again instead
// of being handed the singleton CreateClass cached before Init.
#include "service.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string_view>

struct Probe : SERVICE
{
    enum class Init
    {
        Refuse,
        Throw,
        Succeed
    };
    static inline Init mode = Init::Refuse;
    static inline int attempts = 0, alive = 0;
    Probe()
    {
        ++alive;
    }
    ~Probe() override
    {
        --alive;
    }
    bool Init() override
    {
        ++attempts;
        if (mode == Init::Throw)
            throw std::runtime_error("graphics device unavailable");
        return mode == Init::Succeed;
    }
};
CREATE_SERVICE(Probe)

int main()
{
    for (int i = 0; i < 2; ++i)
    {
        assert(InitService(Probevmaci) == nullptr);
        assert(Probe::alive == 0 && Probevmaci.GetReference() == 0);
    }
    assert(Probe::attempts == 2);
    Probe::mode = Probe::Init::Throw;
    for (int i = 0; i < 2; ++i)
    {
        try
        {
            InitService(Probevmaci);
            assert(false);
        }
        catch (const std::runtime_error &e)
        {
            assert(std::string_view(e.what()) == "graphics device unavailable");
        }
        assert(Probe::alive == 0 && Probevmaci.GetReference() == 0);
    }
    assert(Probe::attempts == 4);
    Probe::mode = Probe::Init::Succeed;
    auto *ready = InitService(Probevmaci);
    assert(ready && Probe::alive == 1 && Probe::attempts == 5 && Probevmaci.GetReference() == 1);
    Probevmaci.Clear();
    assert(Probe::alive == 0);
    std::cout << "PASS: a refused or throwing Init drops the singleton; a later request tries again\n";
}
