#include "PresetManager.h"
#include "UI.h"

#include "../3rdparty/json.hpp"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#  include <windows.h>
#  include <shlobj.h>
#elif defined(__APPLE__)
#  include <unistd.h>
#  include <pwd.h>
#else
// Linux / other POSIX
#  include <unistd.h>
#  include <pwd.h>
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// ── Factory presets ────────────────────────────────────────────────────────
// Name, RoomSize, Damping, PreDelay, HiDamp, LoCut, EarlyRef, Mix, Level
//
// Tuned against the faithful (RE-01) DSP:
//   * comb feedback   g = 0.40 + 0.58*(1 - Damping/100)   -> [0.40, 0.98]
//   * delay scale     0.968 * sqrt(Size/0.625)  (ER/AP/comb all scale)
//   * wet level       scaled by sqrt(1-g), so low damping = quiet, long tail
//   * RT60 ≈ 0.0197 * sqrt(Size/0.625) / (-ln g)   seconds
//     (broadband average-comb estimate; the longest comb stretches the very
//      end of the tail by roughly 10-20%)
// The damping values therefore sit lower than RE-04's perceptual-curve
// presets for comparable decay times.
//
// Listed in ascending RT60 order; the trailing comment gives the estimated
// decay and the intended production scenario.
static const Preset kFactoryPresets[] = {
    //  name             size   damp  predelay hiDamp loCut  ER    mix  lvl
    {"Slap Room",        0.625f, 60,   0,      25,   160,  -6.0f, 22, 0.0f}, // ~0.04 s - tight percussion slap
    {"Announce Booth",   2.5f,   55,   0,      75,   130, -14.0f, 14, 0.0f}, // ~0.10 s - dry voice-over / podcast booth
    {"Tiled Bathroom",   1.25f,  35,   0,      15,   170,  -1.0f, 38, 0.0f}, // ~0.11 s - bright micro space, foley
    {"Bass Cab Room",    5.0f,   60,   4,      70,    55,  -8.0f, 24, 0.0f}, // ~0.12 s - dark bass ambience
    {"Drum Room",        2.5f,   40,   3,      30,   150,  -3.0f, 30, 0.0f}, // ~0.14 s - punchy drums
    {"Drum Bus Room",   10.0f,   48,   6,      45,   110,  -4.0f, 48, 0.0f}, // ~0.22 s - parallel drum-bus glue
    {"Small Room",      10.0f,   42,   8,      35,   110,  -5.0f, 32, 0.0f}, // ~0.26 s - general small room
    {"Snare Splash",     5.0f,   30,   2,      20,   160,  -2.0f, 42, 0.0f}, // ~0.26 s - bright parallel snare verb
    {"Clean Guitar",    20.0f,   45,  12,      50,    90,  -6.0f, 30, 0.0f}, // ~0.34 s - natural guitar room
    {"Lo-Fi Space",     40.0f,   55,   8,      95,    30, -16.0f, 40, 0.0f}, // ~0.38 s - dark vintage wash
    {"Vocal Space",     40.0f,   48,  25,      60,   100,  -5.0f, 34, 0.0f}, // ~0.45 s - lead vocal reverb
    {"Studio Chamber",  40.0f,   40,  15,      55,    90,  -6.0f, 36, 0.0f}, // ~0.54 s - neutral studio chamber
    {"Warm Chamber",    80.0f,   45,  20,      75,    70,  -8.0f, 38, 0.0f}, // ~0.68 s - dark warm chamber
    {"Vocal Throw",     80.0f,   40, 120,      35,   120, -10.0f, 45, 0.0f}, // ~0.77 s - delay throws, big pre-delay
    {"Dark Ambience",  200.0f,   35,  12,      85,    55,  -8.0f, 35, 0.0f}, // ~1.4 s - dark underscore wash
    {"Piano Hall",     160.0f,   25,  25,      40,    70,  -8.0f, 32, 0.0f}, // ~1.75 s - acoustic piano
    {"Concert Stage",  320.0f,   32,  18,      55,    75,  -5.0f, 34, 0.0f}, // ~1.9 s - live ensemble stage
    {"Plate",          160.0f,   18,   0,      45,   140, -10.0f, 40, 0.0f}, // ~2.4 s - classic plate
    {"Grand Hall",     640.0f,   30,  40,      45,    80,  -9.0f, 42, 0.0f}, // ~2.9 s - orchestral hall
    {"String Hall",    320.0f,   18,  35,      65,    50, -12.0f, 38, 0.0f}, // ~3.4 s - smooth strings
    {"Dub Chamber",    640.0f,   20,  90,      80,    45, -14.0f, 50, 0.0f}, // ~4.3 s - dub / electronic echoes
    {"Cathedral",      500.0f,   14,  75,      30,    45, -12.0f, 45, 0.0f}, // ~5.2 s - huge sacred space
    {"Bloom Pad",      640.0f,    8,  50,      70,    40, -12.0f, 45, 0.0f}, // ~9.2 s - evolving pads
    {"Ambient Swells", 640.0f,    6, 100,      85,    40, -40.0f, 55, 0.0f}, // ~11 s - guitar swells / FX, ER off
    {"Endless Hall",   640.0f,    2,  60,      40,    60, -14.0f, 50, 0.0f}, // ~20 s - freeze-like drone tail
};
static constexpr int kFactoryPresetsCount = (int)(sizeof(kFactoryPresets) / sizeof(kFactoryPresets[0]));

// ── Constructor ────────────────────────────────────────────────────────────
PresetManager::PresetManager(ClassicReverbUI* ui)
    : fUI(ui)
{
}

// ── Factory preset accessors ───────────────────────────────────────────────
int PresetManager::factoryPresetCount() const
{
    return kFactoryPresetsCount;
}

const Preset& PresetManager::factoryPreset(int index) const
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < kFactoryPresetsCount, kFactoryPresets[0])
    return kFactoryPresets[index];
}

// ── User preset accessors ──────────────────────────────────────────────────
int PresetManager::userPresetCount() const
{
    return (int)fUserPresets.size();
}

const Preset& PresetManager::userPreset(int index) const
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < (int)fUserPresets.size(), fImportedPreset)
    return fUserPresets[index];
}

// ── Preset selection ───────────────────────────────────────────────────────
void PresetManager::loadDefaultPreset()
{
    DISTRHO_SAFE_ASSERT_RETURN(fUI != nullptr, )
    for (uint32_t i = 0; i < kParamCount; ++i)
        _triggerParamUpdate(i, kParamRanges[i].def);
    syncPluginState(PresetType::Factory, -1, false);
}

void PresetManager::selectFactoryPreset(int index)
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < kFactoryPresetsCount, )
    _applyPreset(kFactoryPresets[index]);
    syncPluginState(PresetType::Factory, index, false);
}

void PresetManager::selectUserPreset(int index)
{
    DISTRHO_SAFE_ASSERT_RETURN(index >= 0 && index < (int)fUserPresets.size(), )
    _applyPreset(fUserPresets[index]);
    syncPluginState(PresetType::User, index, false);
}

void PresetManager::selectImportedPreset()
{
    _applyPreset(fImportedPreset);
    syncPluginState(PresetType::Imported, -1, false);
}

// ── User preset CRUD ──────────────────────────────────────────────────────
bool PresetManager::nameExists(const std::string& name) const
{
    for (const auto& p : fUserPresets)
        if (p.name == name) return true;
    return false;
}

bool PresetManager::saveAsNew(const std::string& name)
{
    if (nameExists(name))
        return false;

    Preset p  = snapshotFromUI();
    p.name    = name;
    fUserPresets.push_back(p);

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(PresetType::User, (int)fUserPresets.size() - 1, false);
    return ok;
}

bool PresetManager::overwriteCurrent()
{
    if (fCurrentType != PresetType::User)
        return false;
    DISTRHO_SAFE_ASSERT_RETURN(fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size(), false)

    const std::string name = fUserPresets[fCurrentIndex].name;
    Preset p = snapshotFromUI();
    p.name   = name;
    fUserPresets[fCurrentIndex] = p;

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(false);
    return ok;
}

bool PresetManager::deleteCurrent()
{
    if (fCurrentType != PresetType::User)
        return false;
    DISTRHO_SAFE_ASSERT_RETURN(fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size(), false)

    fUserPresets.erase(fUserPresets.begin() + fCurrentIndex);
    PresetType newType;
    int        newIndex;
    if (fUserPresets.empty()) {
        newType  = PresetType::Factory;
        newIndex = 0;
    } else {
        newType  = PresetType::User;
        newIndex = std::min(fCurrentIndex, (int)fUserPresets.size() - 1);
    }

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(newType, newIndex, false);
    return ok;
}

bool PresetManager::renameCurrent(const std::string& newName)
{
    if (fCurrentType != PresetType::User)
        return false;
    DISTRHO_SAFE_ASSERT_RETURN(fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size(), false)

    // No-op: preset already has this name.
    if (fUserPresets[fCurrentIndex].name == newName)
        return true;
    // Reject: another preset already uses this name.
    if (nameExists(newName))
        return false;

    fUserPresets[fCurrentIndex].name = newName;

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(fModified);
    return ok;
}

// ── Import / Export ────────────────────────────────────────────────────────
bool PresetManager::hasImported() const
{
    return !fImportedPreset.name.empty();
}

const Preset* PresetManager::importedPreset() const
{
    return hasImported() ? &fImportedPreset : nullptr;
}

bool PresetManager::importFromFile(const std::string& filePath)
{
    try {
        std::ifstream f(filePath);
        if (!f.is_open()) return false;
        json j = json::parse(f);
        fImportedPreset.name     = j.value("name",      "Imported");
        fImportedPreset.roomSize = j.value("room_size",  80.0f);
        fImportedPreset.damping  = j.value("damping",    40.0f);
        fImportedPreset.preDelay = j.value("pre_delay",   0.0f);
        fImportedPreset.hiDamp   = j.value("hi_damp",    30.0f);
        fImportedPreset.loCut    = j.value("lo_cut",     80.0f);
        fImportedPreset.earlyRef = j.value("early_ref",  -6.0f);
        fImportedPreset.mix      = j.value("mix",        35.0f);
        fImportedPreset.level    = j.value("level",       0.0f);
        selectImportedPreset();
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::exportCurrentToFile(const std::string& filePath)
{
    const Preset* p = currentPreset();
    if (!p)
        return false;

    try {
        json j;
        j["name"]      = p->name;
        j["room_size"] = p->roomSize;
        j["damping"]   = p->damping;
        j["pre_delay"] = p->preDelay;
        j["hi_damp"]   = p->hiDamp;
        j["lo_cut"]    = p->loCut;
        j["early_ref"] = p->earlyRef;
        j["mix"]       = p->mix;
        j["level"]     = p->level;
        std::ofstream f(filePath);
        if (!f.is_open()) return false;
        f << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::commitImported(const std::string& name)
{
    Preset p = fImportedPreset;
    p.name   = name;
    fUserPresets.push_back(p);

    const bool ok = saveUserPresetsToDisk();
    syncPluginState(PresetType::User, (int)fUserPresets.size() - 1, false);
    return ok;
}

// ── Disk I/O ───────────────────────────────────────────────────────────────
bool PresetManager::loadUserPresetsFromDisk()
{
    const std::string path = _getUserPresetsFilePath();
    try {
        std::ifstream f(path);
        if (!f.is_open()) return true; // File not found yet — OK
        json j = json::parse(f);
        fUserPresets.clear();
        for (auto& entry : j["presets"]) {
            Preset p;
            p.name     = entry.value("name",       "");
            p.roomSize = entry.value("room_size",   80.0f);
            p.damping  = entry.value("damping",     40.0f);
            p.preDelay = entry.value("pre_delay",    0.0f);
            p.hiDamp   = entry.value("hi_damp",     30.0f);
            p.loCut    = entry.value("lo_cut",      80.0f);
            p.earlyRef = entry.value("early_ref",   -6.0f);
            p.mix      = entry.value("mix",         35.0f);
            p.level    = entry.value("level",        0.0f);
            if (!p.name.empty())
                fUserPresets.push_back(std::move(p));
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool PresetManager::saveUserPresetsToDisk()
{
    if (!_ensureDataDirExists())
        return false;

    const std::string path = _getUserPresetsFilePath();
    try {
        json arr = json::array();
        for (const auto& p : fUserPresets) {
            json entry;
            entry["name"]      = p.name;
            entry["room_size"] = p.roomSize;
            entry["damping"]   = p.damping;
            entry["pre_delay"] = p.preDelay;
            entry["hi_damp"]   = p.hiDamp;
            entry["lo_cut"]    = p.loCut;
            entry["early_ref"] = p.earlyRef;
            entry["mix"]       = p.mix;
            entry["level"]     = p.level;
            arr.push_back(entry);
        }
        json j;
        j["version"] = 1;
        j["presets"] = arr;
        std::ofstream f(path);
        if (!f.is_open()) return false;
        f << j.dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

// ── State accessors ────────────────────────────────────────────────────────
const Preset* PresetManager::currentPreset() const
{
    switch (fCurrentType) {
    case PresetType::Factory:
        if (fCurrentIndex >= 0 && fCurrentIndex < kFactoryPresetsCount)
            return &kFactoryPresets[fCurrentIndex];
        break;
    case PresetType::User:
        if (fCurrentIndex >= 0 && fCurrentIndex < (int)fUserPresets.size())
            return &fUserPresets[fCurrentIndex];
        break;
    case PresetType::Imported:
        return &fImportedPreset;
    }
    return nullptr;
}

void PresetManager::markModified()
{
    if (!fModified)
        syncPluginState(true);
}

void PresetManager::clearModified()
{
    if (fModified)
        syncPluginState(false);
}

void PresetManager::syncPluginState(PresetType type, int index, bool modified)
{
    fCurrentType  = type;
    fCurrentIndex = index;
    syncPluginState(modified);
}

void PresetManager::syncPluginState(bool modified)
{
    fModified = modified;
    DISTRHO_SAFE_ASSERT_RETURN(fUI != nullptr, )

    const Preset* p      = currentPreset();
    const char*   name   = p ? p->name.c_str() : "";
    const char*   mod    = fModified ? "true" : "false";
    const char*   typeStr;

    switch (fCurrentType) {
    case PresetType::Factory:  typeStr = "Factory";  break;
    case PresetType::User:     typeStr = "User";     break;
    case PresetType::Imported: typeStr = "Imported"; break;
    default:                   typeStr = "Factory";  break;
    }

    fUI->setState(STATE_PRESET_NAME,     name);
    fUI->setState(STATE_PRESET_MODIFIED, mod);
    fUI->setState(STATE_PRESET_TYPE,     typeStr);
}

Preset PresetManager::snapshotFromUI() const
{
    Preset p;
    p.roomSize = fUI->fParams[kParamRoomSize];
    p.damping  = fUI->fParams[kParamDamping];
    p.preDelay = fUI->fParams[kParamPreDelay];
    p.hiDamp   = fUI->fParams[kParamHiDamp];
    p.loCut    = fUI->fParams[kParamLoCut];
    p.earlyRef = fUI->fParams[kParamEarlyRef];
    p.mix      = fUI->fParams[kParamMix];
    p.level    = fUI->fParams[kParamLevel];
    return p;
}

void PresetManager::restoreFromState(const std::string& typeStr,
                                     const std::string& nameStr,
                                     bool modified)
{
    // Restore type
    if (typeStr == "User")          fCurrentType = PresetType::User;
    else if (typeStr == "Imported") fCurrentType = PresetType::Imported;
    else                            fCurrentType = PresetType::Factory;

    // Restore index by searching for the name in the appropriate list.
    // Factory fallback is -1 ("Default") — not index 0 — so that an empty
    // preset_name on first launch resolves to Default, not the first factory preset.
    fCurrentIndex = -1;
    if (fCurrentType == PresetType::Factory) {
        for (int i = 0; i < kFactoryPresetsCount; ++i) {
            if (kFactoryPresets[i].name == nameStr) { fCurrentIndex = i; break; }
        }
    } else if (fCurrentType == PresetType::User) {
        fCurrentIndex = -1;
        for (int i = 0; i < (int)fUserPresets.size(); ++i) {
            if (fUserPresets[i].name == nameStr) { fCurrentIndex = i; break; }
        }
        // If the user preset no longer exists (e.g. deleted after state was saved),
        // fall back to Factory / -1 (= Default) so the plugin is in a defined state.
        if (fCurrentIndex == -1) {
            fCurrentType  = PresetType::Factory;
            fModified     = false;
        }
    }
    // For Imported, index stays -1 (the name is in fImportedPreset which we can't restore)

    fModified = modified;
}

// ── Private: parameter application ────────────────────────────────────────
void PresetManager::_applyPreset(const Preset& preset)
{
    DISTRHO_SAFE_ASSERT_RETURN(fUI != nullptr, )
    _triggerParamUpdate(kParamRoomSize, preset.roomSize);
    _triggerParamUpdate(kParamDamping,  preset.damping);
    _triggerParamUpdate(kParamPreDelay, preset.preDelay);
    _triggerParamUpdate(kParamHiDamp,   preset.hiDamp);
    _triggerParamUpdate(kParamLoCut,    preset.loCut);
    _triggerParamUpdate(kParamEarlyRef, preset.earlyRef);
    _triggerParamUpdate(kParamMix,      preset.mix);
    _triggerParamUpdate(kParamLevel,    preset.level);
}

void PresetManager::_triggerParamUpdate(uint32_t index, float value)
{
    DISTRHO_SAFE_ASSERT_RETURN(index < kParamCount, )
    fUI->setParameterValue(index, value);
    fUI->parameterChanged(index, value);
}

// ── Private: platform-specific data directory ─────────────────────────────
std::string PresetManager::_getUserDataDir() const
{
#if defined(_WIN32)
    PWSTR wpath = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &wpath))) {
        int len = WideCharToMultiByte(CP_UTF8, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<size_t>(len - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wpath, -1, &result[0], len, nullptr, nullptr);
        CoTaskMemFree(wpath);
        return result + "\\" CLASSIC_REVERB_APPDATA_DIR_NAME;
    }
    // Fallback
    const char* appdata = getenv("APPDATA");
    return std::string(appdata ? appdata : ".") + "\\" CLASSIC_REVERB_APPDATA_DIR_NAME;
#elif defined(__APPLE__)
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    return std::string(home ? home : ".") + "/Library/Application Support/" CLASSIC_REVERB_APPDATA_DIR_NAME;
#else
    // Linux / other POSIX
    const char* xdgData = getenv("XDG_DATA_HOME");
    if (xdgData && xdgData[0] != '\0')
        return std::string(xdgData) + "/" CLASSIC_REVERB_APPDATA_DIR_NAME;
    const char* home = getenv("HOME");
    if (!home) {
        struct passwd* pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : nullptr;
    }
    return std::string(home ? home : ".") + "/.local/share/" CLASSIC_REVERB_APPDATA_DIR_NAME;
#endif
}

std::string PresetManager::_getUserPresetsFilePath() const
{
#if defined(_WIN32)
    return _getUserDataDir() + "\\" CLASSIC_REVERB_PRESET_FILE_NAME;
#else
    return _getUserDataDir() + "/" CLASSIC_REVERB_PRESET_FILE_NAME;
#endif
}

bool PresetManager::_ensureDataDirExists() const
{
    try {
        fs::create_directories(_getUserDataDir());
        return true;
    } catch (...) {
        return false;
    }
}
