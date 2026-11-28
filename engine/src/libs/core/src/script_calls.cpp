#include "compiler.h"

// core.CallFunction: the function receives the message's values in its parameters, as an event handler does.
std::optional<VDATA *> COMPILER::CallFunction(const char *name, MESSAGE &arguments)
{
    const uint32_t code = FuncTab.FindFunc(name);
    if (code == INVALID_FUNC_CODE)
        return std::nullopt;
    const uint32_t stack = SStack.GetDataNum();
    arguments.Move2Start();
    uint32_t passed = 0;
    for (; arguments.GetCurrentFormatType() != 0; passed++)
        if (!PushEventValue(arguments))
        {
            SStack.InvalidateFrom(stack);
            return nullptr;
        }
    DATA *result = nullptr;
    BC_Execute(code, result, nullptr, passed);
    VDATA *value = result ? result->GetVarPointer() : nullptr;
    if (result)
        SStack.Pop();
    if (SStack.GetDataNum() != stack)
    {
        SStack.InvalidateFrom(stack);
        SetError("%s left the stack unbalanced", name);
        return nullptr;
    }
    return value;
}
