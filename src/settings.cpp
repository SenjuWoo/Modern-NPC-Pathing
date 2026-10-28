#include "settings.h"

#include "ini.h"

namespace
{
    constexpr const char* INI_PATH = "Data/SKSE/Plugins/NPCPathingNG.ini";
    constexpr const char* PLUGIN_FILE = "NPCPathingNG.esp";

    Settings::CompatMode ParseCompatMode(const std::string& a_text, Settings::CompatMode a_default)
    {
        const auto v = Ini::Lower(a_text);
        if (v == "auto" || v == "0") {
            return Settings::CompatMode::Auto;
        }
        if (v == "force" || v == "1") {
            return Settings::CompatMode::Force;
        }
        if (v == "passive" || v == "2") {
            return Settings::CompatMode::Passive;
        }
        return a_default;
    }
}

void Settings::Load()
{
    const auto values = Ini::Parse(INI_PATH);
    if (values.empty()) {
        logger::warn("NPCPathingNG: {} missing or empty — built-in defaults in use", INI_PATH);
        return;
    }

    auto getBool = [&](const char* a_key, bool a_def) { return Ini::Bool(values, a_key, a_def); };
    auto getInt = [&](const char* a_key, int a_def) {
        auto it = values.find(a_key);
        return it != values.end() ? std::atoi(it->second.c_str()) : a_def;
    };
    auto getFloat = [&](const char* a_key, float a_def) {
        auto it = values.find(a_key);
        return it != values.end() ? static_cast<float>(std::atof(it->second.c_str())) : a_def;
    };

    enabled = getBool("general/benabled", enabled);
    checkInterval = std::max(0.05f, getFloat("general/fcheckinterval", checkInterval));
    stuckThreshold = std::max(1, getInt("general/istuckthreshold", stuckThreshold));
    stuckDistance = std::max(0.5f, getFloat("general/fstuckdistance", stuckDistance));
    cooldown = std::max(0.5f, getFloat("general/fcooldown", cooldown));
    actorsPerFrame = std::clamp(getInt("general/iactorsperframe", actorsPerFrame), 1, 100);

    frameBudgetMs = std::clamp(getFloat("performance/fframebudgetms", frameBudgetMs), 0.0f, 5.0f);
    processRadius = std::clamp(getFloat("performance/fprocessradius", processRadius), 0.0f, 20000.0f);

    if (const auto it = values.find("compat/smode"); it != values.end()) {
        compatMode = ParseCompatMode(it->second, compatMode);
    }

    enableLeap = getBool("leap/benableleap", enableLeap);
    leapMinHeight = std::clamp(getFloat("leap/fminheight", leapMinHeight), 20.0f, 120.0f);
    leapMaxHeight = std::clamp(getFloat("leap/fmaxheight", leapMaxHeight), leapMinHeight, 220.0f);
    leapMaxDrop = std::clamp(getFloat("leap/fmaxdrop", leapMaxDrop), 0.0f, 600.0f);
    leapMaxDistance = std::clamp(getFloat("leap/fmaxdistance", leapMaxDistance), 80.0f, 400.0f);
    leapMaxActive = std::clamp(getInt("leap/imaxactive", leapMaxActive), 1, 24);

    enableParkour = getBool("parkour/benableparkour", enableParkour);
    parkourIndoorMode = std::clamp(getInt("parkour/iindoormode", parkourIndoorMode), 0, 2);
    maxClimbHeight = std::clamp(getFloat("parkour/fmaxclimbheight", maxClimbHeight), 60.0f, 250.0f);
    parkourMaxPlayerDistance = std::clamp(
        getFloat("parkour/fparkourmaxplayerdistance", parkourMaxPlayerDistance), 0.0f, 8000.0f);
    enableEvgTraversal = getBool("parkour/benableevgtraversal", enableEvgTraversal);
    teleportEscalation = std::clamp(getInt("avoidance/iteleportescalation", teleportEscalation), 1, 10);

    followerReplay = getBool("followers/bfollowerreplay", followerReplay);

    enableTeleportFallback = getBool("avoidance/benableteleportfallback", enableTeleportFallback);
    snapDistance = std::clamp(getFloat("avoidance/fsnapdistance", snapDistance), 40.0f, 300.0f);

    excludeInCombat = getBool("filters/bexcludeincombat", excludeInCombat);
    excludeFollowers = getBool("filters/bexcludefollowers", excludeFollowers);
    excludeMounted = getBool("filters/bexcludemounted", excludeMounted);

    debugLogging = getBool("debug/bdebuglogging", debugLogging);

    logger::info("NPCPathingNG: INI loaded — enabled={}, leap={}, parkour={}, evg={}, compatMode={}, budget={}ms",
                 enabled, enableLeap, enableParkour, enableEvgTraversal, static_cast<int>(compatMode), frameBudgetMs);
}

void Settings::BindGlobals()
{
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!dataHandler) {
        return;
    }

    auto lookup = [&](RE::FormID a_localID) -> RE::TESGlobal* {
        auto* form = dataHandler->LookupForm(a_localID, PLUGIN_FILE);
        return form ? form->As<RE::TESGlobal>() : nullptr;
    };

    gEnabled          = lookup(0x800);
    gCheckInterval    = lookup(0x801);
    gStuckThreshold   = lookup(0x802);
    gStuckDistance    = lookup(0x803);
    gCooldown         = lookup(0x804);
    gActorsPerFrame   = lookup(0x805);
    gEnableParkour    = lookup(0x806);
    gIndoorMode       = lookup(0x807);
    gMaxClimbHeight   = lookup(0x808);
    gTeleportFallback = lookup(0x809);
    gSnapDistance     = lookup(0x80A);
    gExcludeInCombat  = lookup(0x80B);
    gExcludeFollowers = lookup(0x80C);
    gExcludeMounted   = lookup(0x80D);
    gFollowerReplay   = lookup(0x80E);
    gDebugLogging     = lookup(0x80F);
    gEvgTraversal     = lookup(0x811);
    gTeleportEscalation = lookup(0x812);
    gParkourMaxPlayerDistance = lookup(0x813);
    gEnableLeap       = lookup(0x814);
    gCompatMode       = lookup(0x815);
    gFrameBudgetMs    = lookup(0x816);

    if (gEnabled) {
        logger::info("NPCPathingNG: {} found — settings driven by MCM globals", PLUGIN_FILE);
        // Deliberately NOT Refresh() here: the globals still hold the ESP's compiled-in defaults,
        // so copying them into members would discard what Load() just read from the INI.
        // PushToGlobals() seeds the other way instead; see settings.h.
    } else {
        logger::info("NPCPathingNG: {} not present — using INI settings", PLUGIN_FILE);
    }
}

void Settings::PushToGlobals()
{
    if (!gEnabled) {
        return;  // no ESP — members already hold the INI values
    }

    auto setBool = [](RE::TESGlobal* g, bool v) {
        if (g) { g->value = v ? 1.0f : 0.0f; }
    };
    auto setNum = [](RE::TESGlobal* g, float v) {
        if (g) { g->value = v; }
    };

    setBool(gEnabled, enabled);
    setNum(gCheckInterval, checkInterval);
    setNum(gStuckThreshold, static_cast<float>(stuckThreshold));
    setNum(gStuckDistance, stuckDistance);
    setNum(gCooldown, cooldown);
    setNum(gActorsPerFrame, static_cast<float>(actorsPerFrame));

    setBool(gEnableParkour, enableParkour);
    setNum(gIndoorMode, static_cast<float>(parkourIndoorMode));
    setNum(gMaxClimbHeight, maxClimbHeight);
    setNum(gParkourMaxPlayerDistance, parkourMaxPlayerDistance);

    setBool(gTeleportFallback, enableTeleportFallback);
    setNum(gSnapDistance, snapDistance);
    setNum(gTeleportEscalation, static_cast<float>(teleportEscalation));

    setBool(gExcludeInCombat, excludeInCombat);
    setBool(gExcludeFollowers, excludeFollowers);
    setBool(gExcludeMounted, excludeMounted);

    setBool(gFollowerReplay, followerReplay);
    setBool(gDebugLogging, debugLogging);
    setBool(gEvgTraversal, enableEvgTraversal);

    setBool(gEnableLeap, enableLeap);
    setNum(gCompatMode, static_cast<float>(static_cast<int>(compatMode)));
    setNum(gFrameBudgetMs, frameBudgetMs);

    logger::info("NPCPathingNG: seeded MCM globals from INI — a loaded save overrides these");
}

void Settings::Refresh()
{
    if (!gEnabled) {
        return;  // no ESP — INI values stay
    }

    auto asBool = [](RE::TESGlobal* g, bool cur) { return g ? g->value != 0.0f : cur; };
    auto asInt = [](RE::TESGlobal* g, int cur) { return g ? static_cast<int>(g->value) : cur; };
    auto asFloat = [](RE::TESGlobal* g, float cur) { return g ? g->value : cur; };

    enabled = asBool(gEnabled, enabled);
    checkInterval = std::max(0.05f, asFloat(gCheckInterval, checkInterval));
    stuckThreshold = std::max(1, asInt(gStuckThreshold, stuckThreshold));
    stuckDistance = std::max(0.5f, asFloat(gStuckDistance, stuckDistance));
    cooldown = std::max(0.5f, asFloat(gCooldown, cooldown));
    actorsPerFrame = std::clamp(asInt(gActorsPerFrame, actorsPerFrame), 1, 100);

    enableParkour = asBool(gEnableParkour, enableParkour);
    parkourIndoorMode = std::clamp(asInt(gIndoorMode, parkourIndoorMode), 0, 2);
    maxClimbHeight = std::clamp(asFloat(gMaxClimbHeight, maxClimbHeight), 60.0f, 250.0f);
    parkourMaxPlayerDistance = std::clamp(
        asFloat(gParkourMaxPlayerDistance, parkourMaxPlayerDistance), 0.0f, 8000.0f);

    enableTeleportFallback = asBool(gTeleportFallback, enableTeleportFallback);
    snapDistance = std::clamp(asFloat(gSnapDistance, snapDistance), 40.0f, 300.0f);

    excludeInCombat = asBool(gExcludeInCombat, excludeInCombat);
    excludeFollowers = asBool(gExcludeFollowers, excludeFollowers);
    excludeMounted = asBool(gExcludeMounted, excludeMounted);

    followerReplay = asBool(gFollowerReplay, followerReplay);
    debugLogging = asBool(gDebugLogging, debugLogging);
    enableEvgTraversal = asBool(gEvgTraversal, enableEvgTraversal);
    teleportEscalation = std::clamp(asInt(gTeleportEscalation, teleportEscalation), 1, 10);

    enableLeap = asBool(gEnableLeap, enableLeap);
    compatMode = static_cast<CompatMode>(std::clamp(asInt(gCompatMode, static_cast<int>(compatMode)), 0, 2));
    frameBudgetMs = std::clamp(asFloat(gFrameBudgetMs, frameBudgetMs), 0.0f, 5.0f);
}
