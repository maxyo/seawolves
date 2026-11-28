#pragma once

#include "script_libriary.h"

class ScriptLocationLibrary : public SCRIPT_LIBRIARY
{
  public:
    ScriptLocationLibrary(){};

    ~ScriptLocationLibrary() override{};

    bool Init() override;
};

// CharacterVisual_FadeOut / CharacterVisual_FadeIn (ref character, float seconds): the character's model fades out or
// back in; false when the character has no model in the loaded location.
class ScriptCharacterVisualLibrary : public SCRIPT_LIBRIARY
{
  public:
    bool Init() override;
};
