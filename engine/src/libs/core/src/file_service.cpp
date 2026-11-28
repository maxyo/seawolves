#include "file_service.h"

#include <cctype>

#include "core_impl.h"
#include "string_compare.hpp"
#include "platform/platform.hpp"
#include "platform/checked_file_io.hpp"
#include "fs.h"
#include "save_trash.hpp"
#include "platform/resource_index.hpp"
#include "platform/path_fingerprint.hpp"

#include "platform/layer_files.hpp"
#include "platform/resource_overlay.hpp"
#include "platform/script_patches.hpp"
#include "platform/ui_overlay.hpp"
#include "platform/storage.hpp"

#include <exception>
#include <set>
#include <string>

#define COMMENT ';'
#define SECTION_A '['
#define SECTION_B ']'
#define INI_EQUAL '='
#define VOIDSYMS_NUM 2
#define INI_SIGNATURE ";[SE2IF]"
const char INI_LINEFEED[3] = {0xd, 0xa, 0};
const char INI_VOIDSYMS[VOIDSYMS_NUM] = {0x20, 0x9};

namespace
{
FILE_SERVICE file_service;

// True for the saves folder and everything below it (the file service resolves the game's "save" there). The folder
// is chosen before any script runs; without it this throws, never taking a path for "not a save" (a delete would then
// be final instead of going to the trash).
bool underSaveRoot(const std::filesystem::path &path)
{
    const auto relative = path.lexically_normal().lexically_relative(fs::GetSaveDataPath().lexically_normal());
    return !relative.empty() && *relative.begin() != "..";
}

// Every write of the file service lands in the player's folders; anything else, the game folder above all, is refused.
bool Writable(const std::filesystem::path &path, const char *what)
{
    if (storm::storage::writable(storm::storage::folders(), path))
        return true;
    spdlog::error("Refusing to {} '{}': it is not in the player's saves, settings or cache", what, path.string());
    return false;
}
}

extern VFILE_SERVICE *fio = &file_service;

FILE_SERVICE::FILE_SERVICE()
{
    Files_Num = 0;
    Max_File_Index = 0;
    for (uint32_t n = 0; n < _MAX_OPEN_INI_FILES; n++)
        OpenFiles[n] = nullptr;
}

FILE_SERVICE::~FILE_SERVICE()
{
    Close();
}

std::fstream FILE_SERVICE::_CreateFile(const char *filename, std::ios::openmode mode)
{
    const auto resolved = filename ? ConvertPathResource(filename) : std::string();
    if (PatchedScripts.contains(resolved) || LayerFiles.contains(resolved))
    {
        spdlog::error("'{}' is not a file on disk ({}): read it with ReadVirtualFile, not a stream", filename,
                      resolved);
        return {};
    }
    const auto path = std::filesystem::u8path(resolved);
    if ((mode & (std::ios::out | std::ios::app | std::ios::trunc)) && !Writable(path, "write"))
        return {};
    errno = 0;
    std::fstream fileS(path, mode);
    const int error = errno;
    if (!fileS.is_open() && (mode & std::ios::out))
        spdlog::error("Cannot open writable file '{}' (resolved '{}'): errno={}",
                      filename ? filename : "", path.string(), error);
    return fileS;
}

void FILE_SERVICE::_CloseFile(std::fstream &fileS)
{
    if (!fileS.is_open())
        return; // callers close unconditionally; close() on a stream that never opened sets failbit
    const auto result = storm::io::checked(fileS, [&] { fileS.close(); });
    if (!result.ok)
        spdlog::error("Failed to CloseFile: state={} errno={}", static_cast<unsigned>(result.state), result.error);
}

void FILE_SERVICE::_SetFilePointer(std::fstream &fileS, std::streamoff off, std::ios::seekdir dir)
{
    const auto result = storm::io::checked(fileS, [&] { fileS.seekp(off, dir); });
    if (!result.ok)
        spdlog::error("Failed to SetFilePointer: state={} errno={}", static_cast<unsigned>(result.state), result.error);
}

bool FILE_SERVICE::_DeleteFile(const char *filename)
{
    std::filesystem::path path = std::filesystem::u8path(ConvertPathResource(filename));
    if (!Writable(path, "delete"))
        return false;
    // Saves are not removed at once: the game overwrites a slot by deleting it
    // and then writing a new save, and must not lose it if it dies in between.
    // See save_trash.hpp for when the delete becomes final.
    if (underSaveRoot(path) && !storm::save::isReservedName(path.filename().string()))
    {
        std::string undeferred;
        const bool deleted = storm::save::trash().discard(path, &undeferred);
        if (!undeferred.empty())
            spdlog::error("Save '{}' could not be set aside ({}): it was deleted at once, {}", path.string(),
                          undeferred, deleted ? "and a failed save will not bring it back" : "which failed too");
        return deleted;
    }
    std::error_code error;
    const bool removed = std::filesystem::remove(path, error);
    if (error) spdlog::error("Cannot delete '{}': {}", path.string(), error.message());
    return !error && removed;
}

bool FILE_SERVICE::_WriteFile(std::fstream &fileS, const void *s, std::streamsize count)
{
    const auto result = storm::io::checked(fileS, [&] { fileS.write(reinterpret_cast<const char *>(s), count); });
    if (!result.ok)
        spdlog::error("Failed to WriteFile: state={} errno={}", static_cast<unsigned>(result.state), result.error);
    return result.ok;
}

bool FILE_SERVICE::_ReadFile(std::fstream &fileS, void *s, std::streamsize count)
{
    const auto result = storm::io::checked(fileS, [&] { fileS.read(reinterpret_cast<char *>(s), count); });
    if (!result.ok)
        spdlog::error("Failed to ReadFile: state={} errno={}", static_cast<unsigned>(result.state), result.error);
    return result.ok;
}

bool FILE_SERVICE::_FileOrDirectoryExists(const char *p)
{
    const auto resolved = ConvertPathResource(p);
    if (PatchedScripts.contains(resolved) || LayerFiles.contains(resolved))
        return true;
    std::filesystem::path path = std::filesystem::u8path(resolved);
    auto ec = std::error_code{};
    bool result = std::filesystem::exists(path, ec);
    if (ec)
    {
        spdlog::error("Failed to to check if {} exists: {}", p, ec.message());
        return false;
    }

    return result;
}

std::vector<std::string> FILE_SERVICE::_GetPathsOrFilenamesByMask(const char *sourcePath, const char *mask,
                                                                  bool getPaths, bool onlyDirs, bool onlyFiles,
                                                                  bool recursive)
{
    std::vector<std::string> result;

    const auto fsPaths = _GetFsPathsByMask(sourcePath, mask, getPaths, onlyDirs, onlyFiles, recursive);
    for (std::filesystem::path curPath : fsPaths)
    {
        auto u8Path = curPath.u8string();
        result.emplace_back(u8Path.begin(), u8Path.end());
    }

    return result;
}

template <typename DirIterator>
std::vector<std::filesystem::path> iter_directory(DirIterator &it, std::error_code &ec, const char *mask,
                                                  bool getPaths, bool onlyDirs, bool onlyFiles)
{
    std::vector<std::filesystem::path> result;

    if (ec)
    {
        spdlog::warn("Failed to open save folder: {}", ec.message());
        return result;
    }

    std::filesystem::path curPath;
    for (; it != DirIterator(); it.increment(ec))
    {
        if (ec) break;
        const auto &dirEntry = *it;
        const auto type = dirEntry.symlink_status(ec);
        if (ec) break;
#ifdef __ANDROID__
        // Save enumeration must not expose links outside the selected namespace.
        if (std::filesystem::is_symlink(type)) continue;
        bool thisIsDir = std::filesystem::is_directory(type);
#else
        bool thisIsDir = dirEntry.is_directory(ec);
        if (ec) break;
#endif
        if ((onlyFiles && thisIsDir) || (onlyDirs && !thisIsDir))
        {
            continue;
        }
        curPath = dirEntry.path();
        if (mask == nullptr || storm::wildicmp(mask, curPath.filename().u8string().c_str()))
        {
            if (getPaths)
            {
                result.push_back(curPath);
            }
            else
            {
                result.push_back(curPath.filename());
            }
        }
    }

    if (ec)
    {
        spdlog::error("Failed to enumerate folder: {}", ec.message());
        result.clear();
    }

    return result;
}

std::vector<std::filesystem::path> FILE_SERVICE::_GetFsPathsByMask(const char *sourcePath, const char *mask,
                                                                   bool getPaths, bool onlyDirs, bool onlyFiles,
                                                                   bool recursive)
{
    std::filesystem::path srcPath;
    if (sourcePath == nullptr || sourcePath[0] =='\0')
    {
        srcPath = std::filesystem::current_path();
    }
    else
    {
        srcPath = std::filesystem::u8path(ConvertPathResource(sourcePath));
    }

    std::error_code ec;
    std::vector<std::filesystem::path> result;
    const bool saves = underSaveRoot(srcPath);
    if (saves)
    {
        // Bring back saves whose deletion was interrupted by a process death.
        if (const int restored = storm::save::trash().recover(srcPath); restored > 0)
            spdlog::warn("Restored {} save(s) in '{}' whose overwrite did not complete", restored, srcPath.string());
        using Done = storm::save::Trash::Leftover::Done;
        for (const auto &[leftover, done, why] : storm::save::trash().sweep(srcPath))
            if (done == Done::Removed)
                spdlog::warn("Removed '{}', left by a save write that did not finish", leftover.string());
            else if (done == Done::Named)
                spdlog::warn("'{}', left by a save write that did not finish, is a complete save: it takes its name",
                             leftover.string());
            else
                spdlog::error("'{}', left by a save write that did not finish, stays: {}", leftover.string(), why);
    }
    if (recursive)
    {
        auto it = std::filesystem::recursive_directory_iterator(srcPath, ec);
        result = iter_directory(it, ec, mask, getPaths, onlyDirs, onlyFiles);
    }
    else
    {
        auto it = std::filesystem::directory_iterator(srcPath, ec);
        result = iter_directory(it, ec, mask, getPaths, onlyDirs, onlyFiles);
    }
    if (saves)
    {
        // Temporary, backup and deleted files of the atomic save machinery are not saves.
        std::erase_if(result, [](const std::filesystem::path &entry) {
            return storm::save::isReservedName(entry.filename().string());
        });
    }
    return result;
}

std::time_t FILE_SERVICE::_ToTimeT(std::filesystem::file_time_type tp)
{
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(tp - std::filesystem::file_time_type::clock::now() +
                                                        system_clock::now());
    return system_clock::to_time_t(sctp);
}

std::filesystem::file_time_type FILE_SERVICE::_GetLastWriteTime(const char *filename)
{
    std::filesystem::path path = std::filesystem::u8path(ConvertPathResource(filename));
    std::error_code error;
    const auto time = std::filesystem::last_write_time(path, error);
    if (error) spdlog::error("Cannot get timestamp '{}': {}", path.string(), error.message());
    return error ? std::filesystem::file_time_type{} : time;
}

void FILE_SERVICE::_FlushFileBuffers(std::fstream &fileS)
{
    const auto result = storm::io::checked(fileS, [&] { fileS.flush(); });
    if (!result.ok)
        spdlog::error("Failed to FlushFileBuffers: state={} errno={}", static_cast<unsigned>(result.state), result.error);
}

std::string FILE_SERVICE::_GetCurrentDirectory()
{
    const auto curPath = std::filesystem::current_path().u8string();
    std::string result(curPath.begin(), curPath.end());
    return result;
}

// The game directory (engine.ini, Program, Resource) with a trailing separator.
std::string FILE_SERVICE::_GetExecutableDirectory()
{
    return (storm::storage::folders().game / "").string();
}

std::uintmax_t FILE_SERVICE::_GetFileSize(const char *filename)
{
    const auto resolved = ConvertPathResource(filename);
    if (const auto patched = PatchedScripts.find(resolved); patched != PatchedScripts.end())
        return patched->second.size();
    if (LayerFiles.contains(resolved))
    {
        spdlog::error("'{}' is not a file on disk ({}): read it with ReadVirtualFile", filename, resolved);
        return 0;
    }
    std::filesystem::path path = std::filesystem::u8path(resolved);
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) spdlog::error("Cannot get file size '{}': {}", path.string(), error.message());
    return error ? 0 : size;
}

void FILE_SERVICE::_SetCurrentDirectory(const char *pathName)
{
    std::filesystem::path path = std::filesystem::u8path(ConvertPathResource(pathName));
    std::error_code error;
    std::filesystem::current_path(path, error);
    if (error) spdlog::error("Cannot change directory '{}': {}", path.string(), error.message());
}

bool FILE_SERVICE::_CreateDirectory(const char *pathName)
{
    std::filesystem::path path = std::filesystem::u8path(ConvertPathResource(pathName));
    if (!Writable(path, "create the directory"))
        return false;
    std::error_code error;
    const bool created = std::filesystem::create_directories(path, error);
    if (error) spdlog::error("Cannot create directory '{}': {}", path.string(), error.message());
    return !error && created;
}

std::uintmax_t FILE_SERVICE::_RemoveDirectory(const char *pathName)
{
    std::filesystem::path path = std::filesystem::u8path(ConvertPathResource(pathName));
    if (!Writable(path, "remove the directory"))
        return 0;
    std::error_code error;
    const auto removed = std::filesystem::remove_all(path, error);
    if (error) spdlog::error("Cannot remove directory '{}': {}", path.string(), error.message());
    return error ? 0 : removed;
}

// inifile objects managment

std::unique_ptr<INIFILE> FILE_SERVICE::CreateIniFile(const char *file_name, bool fail_if_exist)
{
    auto fileS = _CreateFile(file_name, std::ios::binary | std::ios::in);
    if (fileS.is_open() && fail_if_exist)
    {
        _CloseFile(fileS);
        return nullptr;
    }
    _CloseFile(fileS);
    fileS = _CreateFile(file_name, std::ios::binary | std::ios::out);
    if (!fileS.is_open())
    {
        spdlog::error("Can't create ini file: {}", file_name);
        return nullptr;
    }
    _CloseFile(fileS);
    return OpenIniFile(file_name);
}

std::unique_ptr<INIFILE> FILE_SERVICE::OpenIniFile(const char *file_name)
{
    for (auto n = 0; n <= Max_File_Index; n++)
    {
        if (OpenFiles[n] == nullptr || OpenFiles[n]->GetFileName() == nullptr)
            continue;
        if (storm::iEquals(OpenFiles[n]->GetFileName(), file_name))
        {
            OpenFiles[n]->IncReference();

            auto v = std::make_unique<INIFILE_T>(OpenFiles[n]);
            if (!v)
                throw std::runtime_error("Failed to create INIFILE_T");
            return v;
        }
    }

    for (auto n = 0; n < _MAX_OPEN_INI_FILES; n++)
    {
        if (OpenFiles[n] != nullptr)
            continue;

        OpenFiles[n] = new IFS(this);
        if (OpenFiles[n] == nullptr)
            throw std::runtime_error("Failed to create IFS");
        if (!OpenFiles[n]->LoadFile(file_name))
        {
            delete OpenFiles[n];
            OpenFiles[n] = nullptr;
            return nullptr;
        }
        if (Max_File_Index < n)
            Max_File_Index = n;
        OpenFiles[n]->IncReference();
        // INIFILE_T object belonged to entity and must be deleted by entity

        auto v = std::make_unique<INIFILE_T>(OpenFiles[n]);
        if (!v)
            throw std::runtime_error("Failed to create INIFILE_T");
        return v;
    }
    return nullptr;
}

void FILE_SERVICE::RefDec(INIFILE *ini_obj)
{
    for (uint32_t n = 0; n <= Max_File_Index; n++)
    {
        if (OpenFiles[n] != ini_obj)
            continue;
        if (OpenFiles[n]->GetReference() == 0)
            throw std::runtime_error("Reference error");
        OpenFiles[n]->DecReference();
        if (OpenFiles[n]->GetReference() == 0)
        {
            delete OpenFiles[n];
            OpenFiles[n] = nullptr;
        }
        return;
    }
    throw std::runtime_error("bad inifile object");
}

void FILE_SERVICE::Close()
{
    for (uint32_t n = 0; n < _MAX_OPEN_INI_FILES; n++)
    {
        if (OpenFiles[n] == nullptr)
            continue;
        delete OpenFiles[n];
        OpenFiles[n] = nullptr;
    }
}

bool FILE_SERVICE::LoadFile(const char *file_name, char **ppBuffer, uint32_t *dwSize)
{
    if (ppBuffer == nullptr)
        return false;
    if (const auto text = ReadVirtualFile(file_name))
    {
        if (dwSize)
            *dwSize = static_cast<uint32_t>(text->size());
        *ppBuffer = new char[text->size()];
        memcpy(*ppBuffer, text->data(), text->size());
        return true;
    }

    auto fileS = fio->_CreateFile(file_name, std::ios::binary | std::ios::in);
    if (!fileS.is_open())
    {
        spdlog::trace("Can't load file: {}", file_name);
        return false;
    }
    const auto dwLowSize = _GetFileSize(file_name);
    if (dwSize)
    {
        *dwSize = dwLowSize;
    }
    if (dwLowSize == 0)
    {
        *ppBuffer = nullptr;
        return false;
    }

    *ppBuffer = new char[dwLowSize];
    _ReadFile(fileS, *ppBuffer, dwLowSize);
    _CloseFile(fileS);
    return true;
}

// Resource paths

void terminate_with_char(std::string &buffer, const char chr)
{
    if (!buffer.empty() && buffer[buffer.length() - 1] != chr)
    {
        // Append to end and store
        buffer += chr;
    }
}

void string_replace(std::string &input, const char *find, const char *paste)
{
    size_t pos = 0;
    while (true)
    {
        pos = input.find(find, pos);
        if (pos >= input.size())
            break;
        input.replace(pos, strlen(find), paste);
        pos += strlen(paste);
    }
}

std::string convert_path(const char *path)
{
    std::string conv;
    size_t size = strlen(path);
    for (int i = 0; i < size; ++i)
    {
        conv.push_back(path[i] == WRONG_PATH_SEP ? PATH_SEP : path[i]);
    }
    return conv;
}

void FILE_SERVICE::ScanResourcePaths()
{
    if (ResourcePathsFirstScan)
    {
        // Seems like if static code calls us we need to do this manually to avoid any bugs
        ResourcePaths = std::unordered_map<std::string, std::string>();
    }
    ResourcePaths.clear();
    PlayerFiles.reset();
    // The working directory is the game directory; see platform/resource_index.hpp.
    storm::resource_index::scan(ResourcePaths, fio->_GetExecutableDirectory());
    // The layers over the game's files, indexed from their manifests and read when a file is opened; a platform
    // without one has none.
    LayerFiles.clear();
    const auto &layers = storm::storage::folders().layers;
    // A layer file that replaces one of the game's is used only over the version it was written for.
    auto overlay = storm::script_patches::applyUiOverlay(layers.mobileUi, ResourcePaths, LayerFiles);
    if (layers.engineShaders)
    {
        const auto techniques = storm::overlay::layerKeys(*layers.engineShaders, storm::overlay::isEngineShaderOverlayPath);
        for (const auto &[key, file] : techniques)
            storm::overlay::lay(layers.engineShaders, key, file, ResourcePaths, LayerFiles);
        spdlog::info("Engine techniques: {} files from {}", techniques.size(), layers.engineShaders->where(""));
    }
    // Fixes to the player's own scripts, held in memory (ReadVirtualFile); throws LayerError if the layer is damaged.
    auto &report = storm::script_patches::lastReport();
    report = storm::script_patches::applyLayer(layers.scriptPatches.get(), ResourcePaths, PatchedScripts,
                                               overlay.overlaid);
    report.info.insert(report.info.end(), overlay.skipped.begin(), overlay.skipped.end());
    ResourcePathsFirstScan = false;
}

std::string FILE_SERVICE::ConvertPathResource(const char *path)
{
    // Saves and settings files live in the player's folders, never in the game resource index, even for reads.
    // A path there that resolve() rejects (escape, symlink, ambiguous name) throws: the operation stops
    // here instead of going on with a substitute path.
    if (!PlayerFiles)
    {
        PlayerFiles.emplace(storm::storage::folders());
        PlayerFiles->seedSettings();
    }
    if (const auto resolved = PlayerFiles->resolve(path))
        return resolved->string();
    if (ResourcePathsFirstScan)
    {
        ScanResourcePaths();
    }
    std::string conv = convert_path(path);
    std::string path_lwr = conv; // save original string if we will need to create new file in existing folder
    std::ranges::for_each(path_lwr, [](char &c) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    std::filesystem::path tmp_path = std::filesystem::u8path(path_lwr).lexically_normal(); // remove relative paths
    path_lwr = tmp_path.string();
    std::string result = ResourcePaths[path_lwr];
    if (result.empty())
    {
        // if we need to create new file in existing folder, then we need to check ResourcePaths[parent_folder]
        result = ResourcePaths[tmp_path.parent_path().string()];
        if (result.empty())
        {
            // no such parent folder
            return path_lwr;
        }
        else
        {
            // parent folder found
            result = result + PATH_SEP + std::filesystem::u8path(conv).filename().string();
            ResourcePaths[path_lwr] = result;
            return result;
        }
    }
    else
    {
        return result;
    }
}

std::optional<std::string> FILE_SERVICE::ReadVirtualFile(const char *filename)
{
    if (filename == nullptr || (PatchedScripts.empty() && LayerFiles.empty()))
        return std::nullopt;
    const auto resolved = ConvertPathResource(filename);
    if (const auto patched = PatchedScripts.find(resolved); patched != PatchedScripts.end())
        return patched->second;
    const auto layer = LayerFiles.find(resolved);
    if (layer == LayerFiles.end())
        return std::nullopt;
    auto text = layer->second.layer->read(layer->second.name);
    // The manifest names the file: the build that made the layer is broken.
    if (!text)
        throw std::runtime_error("Cannot read " + layer->second.layer->where(layer->second.name));
    return text;
}

uint64_t FILE_SERVICE::GetPathFingerprint(const std::filesystem::path &path)
{
    // Hash of (relative path, size, mtime) per file; see platform/path_fingerprint.hpp.
    uint64_t result = storm::fingerprint::tree(path);
    // The interface layer overrides files of the program tree, and the script patch layer changes others.
    const auto normal = path.lexically_normal();
    const auto leaf = normal.filename().empty() ? normal.parent_path().filename() : normal.filename();
    const auto &layers = storm::storage::folders().layers;
    if (storm::AsciiLower(leaf.generic_string()) == "program")
        for (const auto *layer : {layers.mobileUi.get(), layers.scriptPatches.get()})
            if (layer)
                result += storm::fingerprint::mix(layer->fingerprint());
    return result;
}


INIFILE_T::~INIFILE_T()
{
    if (auto fileService = dynamic_cast<FILE_SERVICE *>(fio); fileService)
    {
        try
        {
            fileService->RefDec(ifs_PTR);
        }
        catch (const std::exception &e)
        {
            spdlog::error(e.what());
        }
    }
}

void INIFILE_T::AddString(const char *section_name, const char *key_name, const char *string)
{
    ifs_PTR->AddString(section_name, key_name, string);
}

// write string to file, overwrite data if exist, throw EXS exception object if failed
void INIFILE_T::WriteString(const char *section_name, const char *key_name, const char *string)
{
    ifs_PTR->WriteString(section_name, key_name, string);
}

// write int32_t value of key in pointed section if section and key exist, throw EXS object otherwise
void INIFILE_T::WriteLong(const char *section_name, const char *key_name, int32_t value)
{
    ifs_PTR->WriteLong(section_name, key_name, value);
}

// write double value of key in pointed section if section and key exist, throw EXS object otherwise
void INIFILE_T::WriteDouble(const char *section_name, const char *key_name, double value)
{
    ifs_PTR->WriteDouble(section_name, key_name, value);
}

// fill buffer with key value, throw EXS exception object if failed or if section or key doesnt exist
void INIFILE_T::ReadString(const char *section_name, const char *key_name, char *buffer, size_t buffer_size)
{
    ifs_PTR->ReadString(&Search, section_name, key_name, buffer, buffer_size);
}

// fill buffer with key value if section and key exist, otherwise fill with def_string and return false
bool INIFILE_T::ReadString(const char *section_name, const char *key_name, char *buffer, size_t buffer_size,
                           const char *def_string)
{
    return ifs_PTR->ReadString(&Search, section_name, key_name, buffer, buffer_size, def_string);
}

// continue search from key founded in previous call this function or to function ReadString
// fill buffer with key value if section and key exist, otherwise return false
bool INIFILE_T::ReadStringNext(const char *section_name, const char *key_name, char *buffer, size_t buffer_size)
{
    return ifs_PTR->ReadStringNext(&Search, section_name, key_name, buffer, buffer_size);
}

// return int32_t value of key in pointed section if section and key exist, throw EXS object otherwise
int32_t INIFILE_T::GetInt(const char *section_name, const char *key_name)
{
    return ifs_PTR->GetInt(&Search, section_name, key_name);
}

// return int32_t value of key in pointed section if section and key exist, if not - return def_value
int32_t INIFILE_T::GetInt(const char *section_name, const char *key_name, int32_t def_val)
{
    return ifs_PTR->GetInt(&Search, section_name, key_name, def_val);
}

// return double value of key in pointed section if section and key exist, throw EXS object otherwise
double INIFILE_T::GetDouble(const char *section_name, const char *key_name)
{
    return ifs_PTR->GetDouble(&Search, section_name, key_name);
}

// return double value of key in pointed section if section and key exist, if not - return def_value
double INIFILE_T::GetDouble(const char *section_name, const char *key_name, double def_val)
{
    return ifs_PTR->GetDouble(&Search, section_name, key_name, def_val);
}

bool INIFILE_T::GetIntNext(const char *section_name, const char *key_name, int32_t *val)
{
    return ifs_PTR->GetIntNext(&Search, section_name, key_name, val);
}

bool INIFILE_T::GetDoubleNext(const char *section_name, const char *key_name, double *val)
{
    return ifs_PTR->GetDoubleNext(&Search, section_name, key_name, val);
}

// return double value of key in pointed section if section and key exist, throw EXS object otherwise
float INIFILE_T::GetFloat(const char *section_name, const char *key_name)
{
    return ifs_PTR->GetFloat(&Search, section_name, key_name);
}

// return float value of key in pointed section if section and key exist, if not - return def_value
float INIFILE_T::GetFloat(const char *section_name, const char *key_name, float def_val)
{
    return ifs_PTR->GetFloat(&Search, section_name, key_name, def_val);
}

bool INIFILE_T::GetFloatNext(const char *section_name, const char *key_name, float *val)
{
    return ifs_PTR->GetFloatNext(&Search, section_name, key_name, val);
}

void INIFILE_T::DeleteKey(const char *section_name, const char *key_name)
{
    ifs_PTR->DeleteKey(section_name, key_name);
}

void INIFILE_T::DeleteKey(const char *section_name, const char *key_name, const char *key_value)
{
    ifs_PTR->DeleteKey(section_name, key_name, key_value);
}

bool INIFILE_T::TestKey(const char *section_name, const char *key_name, const char *key_value)
{
    return ifs_PTR->TestKey(section_name, key_name, key_value);
}

void INIFILE_T::DeleteSection(const char *section_name)
{
    ifs_PTR->DeleteSection(section_name);
}

bool INIFILE_T::GetSectionName(char *section_name_buffer, int32_t buffer_size)
{
    return ifs_PTR->GetSectionName(section_name_buffer, buffer_size);
}

bool INIFILE_T::GetSectionNameNext(char *section_name_buffer, int32_t buffer_size)
{
    return ifs_PTR->GetSectionNameNext(section_name_buffer, buffer_size);
}

void INIFILE_T::Flush()
{
    ifs_PTR->Flush();
}

bool INIFILE_T::Reload()
{
    return ifs_PTR->Reload();
}

bool INIFILE_T::CaseSensitive(bool v)
{
    return ifs_PTR->CaseSensitive(v);
}

bool INIFILE_T::TestSection(const char *section_name)
{
    return ifs_PTR->TestSection(section_name);
};
