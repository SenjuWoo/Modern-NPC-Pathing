#include "Menu.h"
#include "Settings.h"
#include "StepUpManager.h"

namespace Menu
{
	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::warn("SKSE Menu Framework not found; install SKSEMenuFramework.dll for in-game options");
			return;
		}
		SKSEMenuFramework::SetSection("StepUpOnto SKSE");
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		npcPerfOverlayWindow = SKSEMenuFramework::AddWindow(RenderNPCPerfOverlay);
		if (npcPerfOverlayWindow) {
			npcPerfOverlayWindow->IsOpen.store(Settings::GetSingleton()->showNPCPerfOverlay);
		}
		logger::info("StepUpOnto: registered with SKSE Menu Framework");
	}

	void __stdcall Render()
	{
		auto* s = Settings::GetSingleton();

		ImGui::TextWrapped(
			"UE5-style step-up: walk onto small ledges and stairs without jumping. Uses Havok raycasts and upward velocity on the character controller.");
		ImGui::Separator();

		if (ImGui::Checkbox("Enable mod", &s->enableMod)) {
			s->Save();
		}

		if (ImGui::Checkbox("Debug logging", &s->enableDebugLogging)) {
			s->Save();
		}

		ImGui::Separator();
		ImGui::Text("Detection");
		if (ImGui::SliderFloat("Max step height", &s->maxStepHeight, 10.f, 120.f, "%.0f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Largest vertical rise (game units) allowed per step.");
		}

		if (ImGui::SliderFloat("Min step height", &s->minStepHeight, 0.f, 30.f, "%.1f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::SliderFloat("Forward detection", &s->forwardDetectionDistance, 20.f, 150.f, "%.0f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::SliderFloat("Shin ray height", &s->shinHeight, 5.f, 40.f, "%.0f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::SliderFloat("Forward past riser", &s->stepForwardPastRiser, 2.f, 24.f, "%.1f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("How far past the low-ray hit to probe for the tread (top of step).");
		}
		if (ImGui::SliderFloat("Same-wall epsilon", &s->sameWallDistanceEpsilon, 1.f, 20.f, "%.1f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("If the high forward ray hits within this distance of the low ray, the wall is treated as too tall.");
		}

		ImGui::Separator();
		ImGui::Text("Motion / safety");
		if (ImGui::Checkbox("Completion forward nudge", &s->enableCompletionNudge)) {
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Push the player/NPC slightly forward past the step edge on completion to prevent getting stuck at the riser boundary.");
		}
		if (ImGui::SliderFloat("Max step velocity", &s->maxStepVelocity, 50.f, 800.f, "%.0f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::SliderFloat("Step cooldown (s)", &s->stepCooldown, 0.05f, 1.f, "%.2f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::SliderFloat("Max slope (deg)", &s->maxSlopeAngle, 15.f, 75.f, "%.0f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::SliderFloat("Displacement safety margin", &s->displacementSafetyMargin, 1.0f, 2.0f, "%.2f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Abort if vertical travel exceeds max step height times this factor (anti-fling).");
		}

		ImGui::Separator();
		ImGui::Text("Experimental: Nearby NPC Step-Up");
		if (ImGui::Checkbox("Enable nearby NPC step-up", &s->enableNearbyNPCStepUp)) {
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Applies step-up to nearby NPCs. Experimental and disabled by default.");
		}

		if (ImGui::SliderFloat("NPC range", &s->npcRange, 300.f, 10000.f, "%.0f")) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Only NPCs within this distance from the player are considered.");
		}

		if (ImGui::SliderInt("NPC limit (0 = unlimited)", &s->npcMaxActors, 0, 64)) {
			s->Clamp();
			s->Save();
		}
		if (ImGui::Checkbox("NPC completion forward nudge", &s->enableNPCCompletionNudge)) {
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Push NPCs slightly forward past the step edge on completion. Disable if NPCs appear to be nudged sideways.");
		}
		if (ImGui::Checkbox("Combat with player bypasses NPC limit", &s->npcCombatBypassLimit)) {
			s->Save();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("If enabled, hostile NPCs in combat with the player are always processed within range even when the NPC limit is reached.");
		}

		const auto perf = StepUp::StepUpManager::GetSingleton()->GetNpcPerfStats();
		ImGui::Text("NPC Perf (live)");
		ImGui::Text("Scanned: %u  In range: %u  Processed: %u  Stepping: %u",
			perf.scannedActors, perf.inRangeActors, perf.processedActors, perf.steppedActors);
		ImGui::Text("Combat bypass: %u  Active sessions: %u",
			perf.combatBypassActors, perf.activeSessions);
		ImGui::Text("Cost: total %.3f ms  scan %.3f ms  step %.3f ms",
			perf.totalMs, perf.scanMs, perf.stepMs);
		if (ImGui::Checkbox("Show NPC perf overlay window", &s->showNPCPerfOverlay)) {
			s->Save();
			if (npcPerfOverlayWindow) {
				npcPerfOverlayWindow->IsOpen.store(s->showNPCPerfOverlay);
			}
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Shows a standalone live performance window so you can profile while playing without keeping this settings page open.");
		}

		ImGui::Separator();
		if (ImGui::Button("Save INI")) {
			s->Clamp();
			s->Save();
			RE::SendHUDMessage::ShowHUDMessage("StepUpOnto: settings saved");
		}
		ImGui::SameLine();
		if (ImGui::Button("Reload INI")) {
			s->Load();
			RE::SendHUDMessage::ShowHUDMessage("StepUpOnto: settings reloaded");
		}
		ImGui::SameLine();
		if (ImGui::Button("Defaults")) {
			*s = Settings{};
			s->Clamp();
			s->Save();
			if (npcPerfOverlayWindow) {
				npcPerfOverlayWindow->IsOpen.store(false);
			}
			RE::SendHUDMessage::ShowHUDMessage("StepUpOnto: restored defaults");
		}
	}

	void __stdcall RenderNPCPerfOverlay()
	{
		auto* s = Settings::GetSingleton();
		if (!s->showNPCPerfOverlay) {
			if (npcPerfOverlayWindow) {
				npcPerfOverlayWindow->IsOpen.store(false);
			}
			return;
		}

		bool open = s->showNPCPerfOverlay;
		ImGuiIO* io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(io->DisplaySize.x - 360.0f, 50.0f), ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(340.0f, 0.0f), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowBgAlpha(0.78f);

		if (ImGui::Begin("StepUpOnto NPC Perf", &open, ImGuiWindowFlags_NoCollapse)) {
			const auto perf = StepUp::StepUpManager::GetSingleton()->GetNpcPerfStats();
			ImGui::Text("Nearby NPC Step-Up");
			ImGui::Separator();
			ImGui::Text("Scanned: %u", perf.scannedActors);
			ImGui::Text("In range: %u", perf.inRangeActors);
			ImGui::Text("Processed: %u", perf.processedActors);
			ImGui::Text("Stepping this frame: %u", perf.steppedActors);
			ImGui::Text("Combat bypass: %u", perf.combatBypassActors);
			ImGui::Text("Active sessions: %u", perf.activeSessions);
			ImGui::Separator();
			ImGui::Text("Total: %.3f ms", perf.totalMs);
			ImGui::Text("Scan: %.3f ms", perf.scanMs);
			ImGui::Text("Step: %.3f ms", perf.stepMs);
		}
		ImGui::End();

		if (open != s->showNPCPerfOverlay) {
			s->showNPCPerfOverlay = open;
			s->Save();
			if (npcPerfOverlayWindow) {
				npcPerfOverlayWindow->IsOpen.store(open);
			}
		}
	}
}
