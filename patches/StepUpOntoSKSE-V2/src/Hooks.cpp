#include "Hooks.h"
#include "Settings.h"
#include "StepUpManager.h"

namespace
{
	// ── Step-up: animation update hook ──────────────────────────────────────────

	using UpdateAnimation_t = void(RE::PlayerCharacter*, float);
	REL::Relocation<UpdateAnimation_t> g_originalUpdateAnimation;

	void UpdateAnimation_Hook(RE::PlayerCharacter* a_this, float a_deltaTime)
	{
		StepUp::StepUpManager::GetSingleton()->Update(a_this, a_deltaTime);
		g_originalUpdateAnimation(a_this, a_deltaTime);
	}
}

namespace Hooks
{
	void Install()
	{
		if (REL::Module::IsVR()) {
			logger::info("StepUpOnto: VR vtable layout differs; hook not installed");
			return;
		}
		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_PlayerCharacter[0] };
		g_originalUpdateAnimation = vtbl.write_vfunc(0x7D, UpdateAnimation_Hook);
		logger::info("StepUpOnto: PlayerCharacter UpdateAnimation hook installed");
	}
}
