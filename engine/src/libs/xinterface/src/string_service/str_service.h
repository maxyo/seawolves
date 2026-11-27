#pragma once

#include "vma.hpp"

#include "script_libriary.h"
#include "../string_service.h"
#include "text_templates.hpp"
#include "common_strings.hpp"
#include "user_string_files.hpp"

#include <string>
#include <unordered_map>

//-----------SDEVICE-----------
class STRSERVICE : public VSTRSERVICE, public storm::strings::TemplateTexts
{
  public:
    STRSERVICE();
    ~STRSERVICE() override;

    bool Init() override;
    bool LoadState(ENTITY_STATE *state) override;
    bool CreateState(ENTITY_STATE_GEN *state_gen) override;
    void RunStart() override;
    void RunEnd() override;

    void SetLanguage(const char *sLanguage) override;
    char *GetLanguage() override;

    char *GetString(const char *stringName, char *sBuffer = nullptr, std::size_t bufferSize = 0) override;
    int32_t GetStringNum(const char *stringName) override;
    char *GetString(int32_t strNum) override;
    char *GetStringName(int32_t strNum) override;

    int32_t OpenUsersStringFile(const char *fileName) override;
    void CloseUsersStringFile(int32_t id) override;
    char *TranslateFromUsers(int32_t id, const char *inStr) override;
    void MergeCommonStrings(const char *fileName) override;

    void SetDialogSourceFile(const char *fileName);

    // the localisation templates (text_templates.hpp)
    std::optional<std::string> Value(std::string_view file, std::string_view key) override;
    std::optional<std::string> KeyValue(std::string_view key) override;
    int Random(int bound) override;
    void Report(std::string_view problem) override;
    // LanguageGetFileStringsQuantity; -1 for an id that is not open
    int32_t UsersStringCount(int32_t id) const
    {
        return m_usersFiles.Count(id);
    }
    // __RefreshLocalization: the texts are read again from the files
    void RefreshTexts();

    char *TranslateForDialog(const char *str)
    {
        return TranslateFromUsers(m_nDialogSourceFile, (char *)str);
    }

  protected:
    void LoadIni();
    std::optional<storm::strings::UserStringFiles::Rows> LoadUsersStringFile(const std::string &fileName) const;
    bool GetNextUsersString(char *src, int32_t &idx, std::optional<std::string> *strName,
                            std::optional<std::string> *strData) const;

  protected:
    char *m_sLanguage;
    char *m_sIniFileName;
    char *m_sLanguageDir;

    storm::strings::CommonStrings m_common;
    std::vector<std::string> m_mergedFiles; // merged into every language's common strings
    bool ReadCommonStrings(const std::string &path, bool merge);

    // Kept parsed after their last close: the interface scripts reopen the same few files
    // every frame (user_string_files.hpp).
    storm::strings::UserStringFiles m_usersFiles{32};

    int32_t m_nDialogSourceFile;

    // StringFromKey: the files under Localization_Assets, open while the language is, by folded key
    std::vector<int32_t> m_keyFiles;
    std::unordered_map<std::string, int32_t> m_keyIndex;
    bool m_keyFilesLoaded = false;
    void CloseKeyFiles();
    storm::strings::TextTemplateLanguage m_templates{*this};
};

class SCRIPT_INTERFACE_FUNCTIONS : public SCRIPT_LIBRIARY
{
  public:
    SCRIPT_INTERFACE_FUNCTIONS(){};

    ~SCRIPT_INTERFACE_FUNCTIONS() override{};
    bool Init() override;
};
