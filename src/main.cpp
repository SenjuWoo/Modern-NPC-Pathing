#include "compat.h"
#include "evg_traversal.h"
#include "hooks.h"
#include "pathing.h"
#include "settings.h"
#include "version.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace
{
    void InitializeLogging()
    {
        auto logPath = SKSE::log::log_directory();
        if (!logPath) {
            return;
        }
        auto logFile = *logPath / "NPCPathingNG.log";
        auto logger = spdlog::basic_logger_mt("NPCPathingNG", logFile.string(), true);
        logger->set_level(spdlog::level::info);
        logger->flush_on(spdlog::level::info);
        spdlog::set_default_logger(std::move(logger));
    }

    // Re-read the INI whenever the journal/system menu closes, so non-MCM users can tweak
    // settings without restarting, and re-read Fluid Motion's INI (it re-reads on game load).
    // MCM users don't need this — their changes flow through the ESP globals.
    class MenuListener : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static MenuListener* GetSingleton()
        {
            static MenuListener instance;
            return &instance;
        }

        static void Register()
        {
            if (auto* ui = RE::UI::GetSingleton()) {
                ui->AddEventSink(GetSingleton());
            }
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (a_event && !a_event->opening && a_event->menuName == RE::JournalMenu::MENU_NAME) {
                Settings::GetSingleton()->Load();
                Compat::Reload();
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void OnMessage(SKSE::MessagingInterface::Message* a_msg)
    {
        switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostLoad:
            // Every plugin DLL is loaded now, whatever its load order: look for the others.
            Compat::Detect();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            // Forms exist now — safe to cache keywords, bind MCM globals, install the frame hook.
            Settings::GetSingleton()->Load();
            Settings::GetSingleton()->BindGlobals();
            // kDataLoaded runs before any save is deserialized, so this makes the INI the
            // starting MCM state on a new game or fresh install without clobbering an
            // existing save's saved settings.
            Settings::GetSingleton()->PushToGlobals();
            Compat::Reload();
            logger::info("compat: {}", Compat::Summary());
            EvgTraversal::CacheForms();
            PathingManager::GetSingleton()->CacheKeywords();
            if (!Hooks::InstallFrameHook()) {
                logger::error("NPCPathingNG is inactive: the frame hook could not be installed safely");
            }
            MenuListener::Register();
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
        case SKSE::MessagingInterface::kNewGame:
            PathingManager::GetSingleton()->Reset();
            // Give EVG marker scanning a fresh chance after a load — the scan
            // latch is a per-cell heuristic, not a permanent verdict.
            EvgTraversal::ResetRouteState();
            break;
        case SKSE::MessagingInterface::kPostLoadGame:
            // Fluid Motion re-reads its INI on every load; so do we.
            Compat::Reload();
            break;
        default:
            break;
        }
    }
}

// One DLL for SE 1.5.97, AE 1.6.x / 1.7.x (and VR, which loads inactive-safe):
// Address Library V5 flag + struct independence. SKSE 2.3+ rejects an Address Library plugin
// without the V5 flag unless the PE stamp is recent (the build uses /Brepro).
SKSEPluginInfo(
    .Version = REL::Version{ PluginVersion::Major, PluginVersion::Minor, PluginVersion::Patch, 0 },
    .Name = "NPCPathingNG"sv,
    .Author = "GennyWoo"sv,  // public author name only — never the local Windows account name
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* a_skse)
{
    if (!a_skse || a_skse->IsEditor()) {
        return false;
    }
    SKSE::Init(a_skse);
    InitializeLogging();

    logger::info("NPCPathingNG v{} loading (runtime {})", PluginVersion::String,
                 REL::Module::get().version().string());

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        logger::error("NPCPathingNG: no messaging interface");
        return false;
    }
    messaging->RegisterListener(OnMessage);

    logger::info("NPCPathingNG loaded");
    return true;
}
