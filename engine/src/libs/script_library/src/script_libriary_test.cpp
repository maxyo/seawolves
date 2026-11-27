#include "script_libriary_test.h"
#include "vano_files.h"
#include "core.h"
#include "s_import_func.h"
#include "v_s_stack.h"
#include "vma.hpp"

CREATE_SCRIPTLIBRIARY(SCRIPT_LIBRIARY_TEST)

namespace
{
// Caribbean Legend's developer start modes (safe mode without mods, skipping the intro, a developer quick load):
// this engine has none of them, so each is off.
uint32_t Off(VS_STACK *stack)
{
    auto *value = stack->Push();
    if (!value)
        return IFUNCRESULT_FAILED;
    value->Set(int32_t{0});
    return IFUNCRESULT_OK;
}

uint32_t NoSave(VS_STACK *stack)
{
    auto *value = stack->Push();
    if (!value)
        return IFUNCRESULT_FAILED;
    value->Set("");
    return IFUNCRESULT_OK;
}
} // namespace

bool SCRIPT_LIBRIARY_TEST::Init()
{
    Vano_Init();
    for (const char *name : {"IsInSafeMode", "IsDevSkipIntro", "IsDevQuickLoad"})
    {
        IFUNCINFO info;
        info.pFuncName = name;
        info.pReturnValueName = "int";
        info.nArguments = 0;
        info.pFuncAddress = Off;
        core.SetScriptFunction(&info);
    }
    IFUNCINFO info;
    info.pFuncName = "GetDevQuickLoadSave";
    info.pReturnValueName = "string";
    info.nArguments = 0;
    info.pFuncAddress = NoSave;
    core.SetScriptFunction(&info);
    return true;
};
