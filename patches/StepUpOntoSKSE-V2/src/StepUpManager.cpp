#include "StepUpManager.h"
#include "Raycast.h"
#include "Settings.h"
#include <chrono>

namespace StepUp
{
	namespace
	{
		constexpr float kPi = 3.14159265f;

		// NPCPathingNG / SkyParkour compatibility: while a parkour animation is
		// driving an actor, its char controller sim is disabled (kNoSim) — the
		// actor reads as grounded with near-zero speed, which is exactly this
		// mod's step-up trigger. Warping them mid-animation fights the parkour
		// mod's position alignment and glitches the climb.
		bool IsParkourAnimationDriving(RE::Actor* a_actor)
		{
			bool skyParkourOngoing = false;
			a_actor->GetGraphVariableBool("SkyParkourOngoing", skyParkourOngoing);
			return skyParkourOngoing;
		}
	}

	float StepUpManager::HitNormalUpDot(const RE::hkVector4& a_normal)
	{
		const float len = a_normal.Length3();
		if (len < 1e-6f) {
			return 0.f;
		}
		return a_normal.Dot3(RE::hkVector4(0.f, 0.f, 1.f, 0.f)) / len;
	}

	bool StepUpManager::CanPlayerAttemptStep(RE::PlayerCharacter* a_player) const
	{
		auto* ui = RE::UI::GetSingleton();
		if (!ui || ui->GameIsPaused() || ui->numPausesGame > 0) {
			return false;
		}

		auto* iface = RE::InterfaceStrings::GetSingleton();
		if (iface && (ui->IsMenuOpen(iface->dialogueMenu) ||
		              ui->IsMenuOpen(iface->GetRuntimeData().loadWaitSpinner))) {
			return false;
		}

		auto* ast = a_player->AsActorState();
		if (!ast) {
			return false;
		}

		if (ast->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
			return false;
		}
		if (ast->IsSwimming() || ast->IsFlying()) {
			return false;
		}
		if (ast->actorState1.lifeState != RE::ACTOR_LIFE_STATE::kAlive) {
			return false;
		}
		if (ast->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
			return false;
		}
		if (IsParkourAnimationDriving(a_player)) {
			return false;
		}

		return true;
	}

	bool StepUpManager::CanNPCAttemptStep(RE::Actor* a_actor)
	{
		if (!a_actor || a_actor->IsPlayerRef()) {
			return false;
		}

		// Only humanoid NPCs – animals/creatures have different body
		// proportions, slow wander speeds that trip the stuck detector,
		// and idle animations (grazing, lying down) that look like being
		// stuck.  ActorTypeNPC (0x13794) is on every humanoid race.
		static auto* kActorTypeNPC = RE::TESForm::LookupByID<RE::BGSKeyword>(0x13794);
		if (!kActorTypeNPC || !a_actor->HasKeyword(kActorTypeNPC)) {
			return false;
		}

		auto* ast = a_actor->AsActorState();
		if (!ast) {
			return false;
		}
		if (ast->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
			return false;
		}
		if (ast->IsSwimming() || ast->IsFlying()) {
			return false;
		}
		if (ast->actorState1.lifeState != RE::ACTOR_LIFE_STATE::kAlive) {
			return false;
		}
		if (ast->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) {
			return false;
		}
		if (a_actor->IsDead() || a_actor->IsDisabled()) {
			return false;
		}

		// Don't interfere when the animation system is driving the actor's
		// position (furniture transitions, scripted scenes, kill moves, etc.)
		bool animDriven = false;
		a_actor->GetGraphVariableBool("bAnimationDriven", animDriven);
		if (animDriven) {
			return false;
		}

		// Don't interfere with SkyParkour/NPCPathingNG parkour animations.
		if (IsParkourAnimationDriving(a_actor)) {
			return false;
		}

		// Don't interfere while the NPC is mid-attack (melee swing, power
		// attack, bash).  Step-up should only assist navigation, not combat.
		if (a_actor->IsInCombat()) {
			bool isAttacking = false;
			a_actor->GetGraphVariableBool("IsAttacking", isAttacking);
			if (isAttacking) {
				return false;
			}
		}

		return true;
	}

	RE::NiPoint3 StepUpManager::GetWorldMoveDirection(RE::PlayerCharacter* a_player)
	{
		auto* pc = RE::PlayerControls::GetSingleton();
		if (!pc) {
			return { 0.f, 0.f, 0.f };
		}

		const auto& input = pc->data.moveInputVec;
		if (input.x == 0.f && input.y == 0.f) {
			return { 0.f, 0.f, 0.f };
		}

		// Use actor facing direction rather than camera direction so that mods
		// like True Directional Movement (TDM) work correctly.  In vanilla
		// Skyrim the character always rotates to face their movement direction,
		// so actor yaw ≈ movement direction.  In TDM target-lock mode the actor
		// faces the locked target; pressing "forward" moves toward that target
		// (actor-relative), so actor yaw is still the correct reference frame.
		// Camera-relative direction breaks in TDM target-lock because the
		// camera can be positioned anywhere around the player.
		const float yaw = a_player->GetAngleZ();
		const float sinY = std::sin(yaw);
		const float cosY = std::cos(yaw);

		// Actor forward (+Y when yaw=0) and right (+X when yaw=0) in world space
		const RE::NiPoint3 forward{  sinY,  cosY, 0.f };
		const RE::NiPoint3 right{    cosY, -sinY, 0.f };

		RE::NiPoint3 dir = forward * input.y + right * input.x;
		const float  len = dir.Length();
		if (len < 1e-5f) {
			return { 0.f, 0.f, 0.f };
		}
		return dir / len;
	}

	void StepUpManager::TryApplyStepUp(
		RE::PlayerCharacter*        a_player,
		RE::bhkCharacterController* a_controller,
		RE::bhkWorld*               a_world,
		float                       a_deltaTime)
	{
		auto* s = Settings::GetSingleton();
		const float maxDispAllowed = s->maxStepHeight * s->displacementSafetyMargin;

		const RE::NiPoint3 playerPos = a_player->GetPosition();
		const float        footZ     = playerPos.z;

		if (m_stepBoostActive) {
			m_stepSessionTime += a_deltaTime;

			constexpr float kMaxSessionTime = 0.5f;
			if (m_stepSessionTime > kMaxSessionTime) {
				if (s->enableDebugLogging) {
					logger::debug("StepUp: session timeout ({:.2f}s)", static_cast<double>(m_stepSessionTime));
				}
				m_cooldownRemaining = s->stepCooldown;
				EndStep();
				return;
			}

			const float traveledZ = footZ - m_stepStartZ;
			if (traveledZ > maxDispAllowed || traveledZ < -5.f) {
				if (s->enableDebugLogging) {
					logger::debug("StepUp: abort session (displacement {:.2f})", traveledZ);
				}
				m_cooldownRemaining = s->stepCooldown;
				EndStep();
				return;
			}
		}

		const RE::NiPoint3 moveDir = GetWorldMoveDirection(a_player);
		if (moveDir.x == 0.f && moveDir.y == 0.f) {
			EndStep();
			return;
		}

		// Don't start a NEW step-up if the player is going downhill.
		// Terrain noise on declines/stairs can create false step-up candidates
		// that zero gravity and trap the player.
		constexpr float kDeclineThreshold = -15.f;  // units/sec
		if (!m_stepBoostActive && m_zVelocity < kDeclineThreshold) {
			return;
		}

		const std::uint32_t filter = Raycast::GetPlayerFilterInfo(a_player);

		// Ground normal check: if the player is standing on a slope, don't
		// try to step up.  Steps only exist on mostly-flat ground.  Slopes
		// have tilted surface normals.  This prevents jagged slope meshes
		// from repeatedly triggering false-positive step-ups.
		{
			constexpr float kMinGroundNormalZ = 0.95f;  // ~18° tolerance
			const RE::NiPoint3 gndStart{ playerPos.x, playerPos.y, footZ + 10.f };
			const RE::NiPoint3 gndEnd{ playerPos.x, playerPos.y, footZ - 30.f };
			RE::hkpWorldRayCastOutput hitGnd{};
			if (Raycast::CastRayWorld(a_world, filter, gndStart, gndEnd, hitGnd)) {
				m_lastGroundNormalZ = HitNormalUpDot(hitGnd.normal);
				if (m_lastGroundNormalZ < kMinGroundNormalZ) {
					EndStep();
					return;
				}
			}
		}

		// Low forward ray: detect the riser face and get its surface normal.
		const RE::NiPoint3 lowStart{ playerPos.x, playerPos.y, footZ + s->shinHeight };
		const RE::NiPoint3 lowEnd = lowStart + moveDir * s->forwardDetectionDistance;

		RE::hkpWorldRayCastOutput hitLow{};
		if (!Raycast::CastRayWorld(a_world, filter, lowStart, lowEnd, hitLow)) {
			EndStep();
			return;
		}
		const float fracLow = hitLow.hitFraction;
		const float dLow    = fracLow * s->forwardDetectionDistance;

		// A step riser is nearly vertical (hit normal ≈ horizontal, Z ≈ 0).
		// A slope has significant upward normal.  Reject slopes so we only
		// activate on actual step/ledge geometry, not smooth inclines.
		constexpr float kMaxRiserSlopeZ = 0.5f;
		m_lastRiserNormalZ = HitNormalUpDot(hitLow.normal);
		if (std::abs(m_lastRiserNormalZ) > kMaxRiserSlopeZ) {
			EndStep();
			return;
		}

		// High forward ray: confirm riser is shorter than maxStepHeight
		const RE::NiPoint3 highStart{ playerPos.x, playerPos.y, footZ + s->maxStepHeight };
		const RE::NiPoint3 highEnd = highStart + moveDir * s->forwardDetectionDistance;

		float fracHigh = 0.f;
		const bool highHit = Raycast::CastRayWorldFraction(a_world, filter, highStart, highEnd, fracHigh);
		if (highHit) {
			const float dHigh = fracHigh * s->forwardDetectionDistance;
			if (dHigh <= dLow + s->sameWallDistanceEpsilon) {
				EndStep();
				return;
			}
		}

		// Down ray: find tread (top surface of the step)
		RE::NiPoint3 overStep = lowStart + moveDir * (dLow + s->stepForwardPastRiser);
		overStep.z = footZ + s->maxStepHeight + 15.f;

		const float           downStartZ = overStep.z;
		const float           downEndZ   = footZ - 100.f;
		const RE::NiPoint3    downStart{ overStep.x, overStep.y, downStartZ };
		const RE::NiPoint3    downEnd{   overStep.x, overStep.y, downEndZ   };

		RE::hkpWorldRayCastOutput hitDown{};
		if (!Raycast::CastRayWorld(a_world, filter, downStart, downEnd, hitDown)) {
			EndStep();
			return;
		}

		const float targetZ = downStartZ + (downEndZ - downStartZ) * hitDown.hitFraction;
		const float deltaZ  = targetZ - footZ;

		if (deltaZ < s->minStepHeight || deltaZ > s->maxStepHeight) {
			EndStep();
			return;
		}

		const float minNormalZ = std::cos(s->maxSlopeAngle * kPi / 180.f);
		if (HitNormalUpDot(hitDown.normal) < minNormalZ) {
			EndStep();
			return;
		}

		// Begin or continue a step session
		if (!m_stepBoostActive) {
			m_stepStartZ      = footZ;
			m_smoothLiftSpeed = 30.f;
			m_stepMoveDir     = moveDir;
		}
		m_stepBoostActive = true;

		const float headroom = s->maxStepHeight - (footZ - m_stepStartZ);
		constexpr float kMaxSmoothSpeed = 200.f;
		float vWant = std::clamp(deltaZ * 10.f, 40.f, std::min(kMaxSmoothSpeed, s->maxStepVelocity));
		vWant       = std::min(vWant, std::max(40.f, headroom * 50.f));

		constexpr float kRampRate = 10.f;
		m_smoothLiftSpeed += (vWant - m_smoothLiftSpeed) * std::min(1.f, a_deltaTime * kRampRate);

		constexpr float kMaxLiftPerFrame = 5.f;
		float liftPerFrame = std::min({ m_smoothLiftSpeed * a_deltaTime, deltaZ, kMaxLiftPerFrame });

		// Disable gravity on the controller while stepping.  The kOnGround
		// state handler normally snaps the character to the detected ground
		// surface each physics step.  This fights our position warp, creating
		// a warp-up / snap-down oscillation that triggers FP Inertia springs.
		// Zeroing gravity and setting kNoGravityOnGround prevents the
		// downward correction, letting our warp persist across the step.
		if (!m_gravityDisabled) {
			m_savedGravity     = a_controller->gravity;
			m_hadNoGravityFlag = a_controller->flags.any(RE::CHARACTER_FLAGS::kNoGravityOnGround);
			m_hadOnStairsFlag  = a_controller->flags.any(RE::CHARACTER_FLAGS::kOnStairs);
			m_gravityDisabled  = true;
		}
		a_controller->gravity = 0.f;
		a_controller->flags.set(RE::CHARACTER_FLAGS::kNoGravityOnGround);
		a_controller->flags.set(RE::CHARACTER_FLAGS::kOnStairs);

		// Warp the Havok body upward
		const float worldScale = RE::bhkWorld::GetWorldScale();
		RE::hkVector4 hkPos;
		a_controller->GetPositionImpl(hkPos, false);
		auto* posF = reinterpret_cast<float*>(&hkPos);
		posF[2] += liftPerFrame * worldScale;
		a_controller->SetPositionImpl(hkPos, false, true);

		// Keep game-level position in sync
		a_player->data.location.z += liftPerFrame;

		// Zero all vertical velocity components so other mods (FP Inertia,
		// FP Weapon Adjust, etc.) don't see a velocity spike.
		auto* outF    = reinterpret_cast<float*>(&a_controller->outVelocity);
		auto* initF   = reinterpret_cast<float*>(&a_controller->initialVelocity);
		auto* velModF = reinterpret_cast<float*>(&a_controller->velocityMod);
		outF[2]    = 0.f;
		initF[2]   = 0.f;
		velModF[2] = 0.f;

		RE::hkVector4 linVel;
		a_controller->GetLinearVelocityImpl(linVel);
		auto* linF = reinterpret_cast<float*>(&linVel);
		linF[2] = 0.f;
		a_controller->SetLinearVelocityImpl(linVel);

		if (s->enableDebugLogging) {
			logger::debug("StepUp: dz={:.1f} lift={:.1f} smooth={:.1f} dLow={:.1f} gZ={:.2f} rZ={:.2f}",
				static_cast<double>(deltaZ),
				static_cast<double>(liftPerFrame),
				static_cast<double>(m_smoothLiftSpeed),
				static_cast<double>(dLow),
				static_cast<double>(m_lastGroundNormalZ),
				static_cast<double>(m_lastRiserNormalZ));
		}

		if (footZ + liftPerFrame >= targetZ - 2.f || deltaZ < s->minStepHeight + 2.f) {
			if (s->enableCompletionNudge) {
				constexpr float kCompletionNudge = 4.f;
				RE::hkVector4 hkPosNudge;
				a_controller->GetPositionImpl(hkPosNudge, false);
				auto* nudgeF = reinterpret_cast<float*>(&hkPosNudge);
				nudgeF[0] += m_stepMoveDir.x * kCompletionNudge * worldScale;
				nudgeF[1] += m_stepMoveDir.y * kCompletionNudge * worldScale;
				a_controller->SetPositionImpl(hkPosNudge, false, true);
				a_player->data.location.x += m_stepMoveDir.x * kCompletionNudge;
				a_player->data.location.y += m_stepMoveDir.y * kCompletionNudge;
			}

			m_cooldownRemaining = s->stepCooldown;
			EndStep();
		}
	}

	void StepUpManager::Update(RE::PlayerCharacter* a_player, float a_deltaTime)
	{
		if (!a_player || a_deltaTime <= 0.f) {
			return;
		}
		if (!Settings::GetSingleton()->enableMod) {
			return;
		}
		m_npcClock += a_deltaTime;

		// Track the player's vertical velocity (units/sec, smoothed).
		// Used to suppress step-up when the player is descending a slope
		// or stairs — terrain noise can create false step-up candidates
		// that zero gravity and trap the player.
		{
			const float footZ = a_player->GetPosition().z;
			if (m_hasFootZHistory && a_deltaTime > 1e-6f) {
				float instantV = (footZ - m_prevFootZ) / a_deltaTime;
				constexpr float kSmoothing = 0.25f;
				m_zVelocity = m_zVelocity * (1.f - kSmoothing) + instantV * kSmoothing;
			} else {
				m_hasFootZHistory = true;
			}
			m_prevFootZ = footZ;
		}

		auto* controller = a_player->GetCharController();
		if (!controller) {
			return;
		}

		// Only start step-up while grounded.  Allow continuing an active
		// step if the state briefly flips to kInAir (our gravity zeroing
		// can cause this for a frame or two).
		const auto charState = controller->context.currentState;
		if (charState != RE::hkpCharacterStateTypes::kOnGround) {
			bool allowContinue = m_stepBoostActive &&
				charState == RE::hkpCharacterStateTypes::kInAir;
			if (!allowContinue) {
				EndStep();
				RestoreGravity(controller);
				m_cooldownRemaining = 0.f;
				return;
			}
		}

		auto* cell = a_player->GetParentCell();
		auto* bhkWorld = cell ? cell->GetbhkWorld() : nullptr;
		if (!bhkWorld) {
			EndStep();
			RestoreGravity(controller);
			m_cooldownRemaining = 0.f;
			return;
		}

		UpdateNearbyNPCs(a_player, bhkWorld, a_deltaTime);

		if (!CanPlayerAttemptStep(a_player)) {
			EndStep();
			RestoreGravity(controller);
			m_cooldownRemaining = 0.f;
			return;
		}

		m_cooldownRemaining -= a_deltaTime;
		if (m_cooldownRemaining > 0.f && !m_stepBoostActive) {
			RestoreGravity(controller);
			return;
		}

		TryApplyStepUp(a_player, controller, bhkWorld, a_deltaTime);

		if (m_stepBoostActive) {
			// Force the character controller to look fully grounded during an
			// active step-up.  The physics step may have flipped the state to
			// kInAir because our position warp left the character momentarily
			// unsupported.  By correcting all state indicators before the
			// animation system processes them (hook order: our Update → original
			// UpdateAnimation), we prevent false fall/jump animations, fall
			// damage accumulation, and other mods from reacting to the flicker.
			controller->context.currentState  = RE::hkpCharacterStateType::kOnGround;
			controller->context.previousState = RE::hkpCharacterStateType::kOnGround;
			controller->surfaceInfo.supportedState = RE::hkpSurfaceInfo::SupportedState::kSupported;
			controller->fallTime        = 0.f;
			controller->fallStartHeight = a_player->GetPosition().z;
		} else {
			RestoreGravity(controller);
		}

		// Hard safety: gravity must never stay disabled for more than 1 second
		// regardless of session state.  Catches any edge case that leaves the
		// player floating with zero gravity.
		if (m_gravityDisabled) {
			m_gravityDisabledTime += a_deltaTime;
			constexpr float kMaxGravityDisableTime = 0.75f;
			if (m_gravityDisabledTime > kMaxGravityDisableTime) {
				if (Settings::GetSingleton()->enableDebugLogging) {
					logger::debug("StepUp: gravity safety timeout ({:.2f}s)", static_cast<double>(m_gravityDisabledTime));
				}
				EndStep();
				RestoreGravity(controller);
				m_cooldownRemaining = Settings::GetSingleton()->stepCooldown;
			}
		} else {
			m_gravityDisabledTime = 0.f;
		}
	}

	void StepUpManager::UpdateNearbyNPCs(RE::PlayerCharacter* a_player, RE::bhkWorld* a_world, float a_deltaTime)
	{
		m_npcPerf = {};

		auto* s = Settings::GetSingleton();
		if (!s->enableNearbyNPCStepUp || !a_world || !a_player) {
			m_npcStates.clear();
			return;
		}

		using Clock = std::chrono::steady_clock;
		const auto totalStart = Clock::now();

		auto* processLists = RE::ProcessLists::GetSingleton();
		if (!processLists) {
			return;
		}

		const RE::NiPoint3 playerPos = a_player->GetPosition();
		const float rangeSq = s->npcRange * s->npcRange;

		constexpr float kMovingThreshold     = 30.f;   // units/sec – above this the NPC is moving normally
		constexpr float kStuckDwellTime      = 0.25f;  // seconds of near-zero speed before we intervene
		constexpr float kRecentlyMovingWindow = 2.0f;  // only help NPCs that were moving within this window

		for (auto& actorHandle : processLists->highActorHandles) {
			++m_npcPerf.scannedActors;

			auto actor = actorHandle.get();
			if (!actor || actor->IsPlayerRef() || !CanNPCAttemptStep(actor.get())) {
				continue;
			}

			auto* controller = actor->GetCharController();
			if (!controller) {
				continue;
			}

			if (controller->context.currentState != RE::hkpCharacterStateTypes::kOnGround) {
				continue;
			}

			const RE::NiPoint3 actorPos = actor->GetPosition();
			const float dx = actorPos.x - playerPos.x;
			const float dy = actorPos.y - playerPos.y;
			const float dz = actorPos.z - playerPos.z;
			if ((dx * dx + dy * dy + dz * dz) > rangeSq) {
				continue;
			}
			++m_npcPerf.inRangeActors;

			const bool combatBypass = s->npcCombatBypassLimit &&
				actor->IsInCombat() &&
				actor->IsHostileToActor(a_player);
			if (combatBypass) {
				++m_npcPerf.combatBypassActors;
			}
			if (!combatBypass && s->npcMaxActors > 0 &&
				m_npcPerf.processedActors >= static_cast<std::uint32_t>(s->npcMaxActors)) {
				continue;
			}

			auto& state = m_npcStates[actor->GetFormID()];
			state.lastSeenTime = m_npcClock;
			state.cooldownRemaining -= a_deltaTime;

			float horizSpeed = 0.f;
			if (state.hasPosHistory && a_deltaTime > 1e-6f) {
				const float ddx = actorPos.x - state.prevPos.x;
				const float ddy = actorPos.y - state.prevPos.y;
				horizSpeed = std::sqrt(ddx * ddx + ddy * ddy) / a_deltaTime;
			}
			state.prevPos = actorPos;
			state.hasPosHistory = true;

			if (horizSpeed > kMovingThreshold) {
				state.stuckTimer = 0.f;
				state.lastMovingTime = m_npcClock;
				continue;
			}

			if ((m_npcClock - state.lastMovingTime) > kRecentlyMovingWindow) {
				state.stuckTimer = 0.f;
				continue;
			}

			state.stuckTimer += a_deltaTime;

			if (state.stuckTimer < kStuckDwellTime || state.cooldownRemaining > 0.f) {
				if (state.stuckTimer >= kStuckDwellTime) {
					++m_npcPerf.activeSessions;
				}
				continue;
			}

			++m_npcPerf.processedActors;
			const auto stepStart = Clock::now();
			TryApplyStepUpNPC(actor.get(), controller, a_world, state);
			const auto stepEnd = Clock::now();
			m_npcPerf.stepMs += std::chrono::duration<float, std::milli>(stepEnd - stepStart).count();

			if (state.cooldownRemaining > 0.f) {
				++m_npcPerf.steppedActors;
				state.stuckTimer = 0.f;
			}
		}

		for (auto it = m_npcStates.begin(); it != m_npcStates.end();) {
			if ((m_npcClock - it->second.lastSeenTime) > 3.f) {
				it = m_npcStates.erase(it);
			} else {
				++it;
			}
		}

		const auto totalEnd = Clock::now();
		m_npcPerf.totalMs = std::chrono::duration<float, std::milli>(totalEnd - totalStart).count();
		m_npcPerf.scanMs = std::max(0.f, m_npcPerf.totalMs - m_npcPerf.stepMs);
	}

	void StepUpManager::TryApplyStepUpNPC(
		RE::Actor*                  a_actor,
		RE::bhkCharacterController* a_controller,
		RE::bhkWorld*               a_world,
		ActorStepState&             a_state)
	{
		if (!a_actor || !a_controller || !a_world) {
			return;
		}

		auto* s = Settings::GetSingleton();
		const RE::NiPoint3 actorPos = a_actor->GetPosition();
		const float footZ = actorPos.z;

		const float yaw = a_actor->GetAngleZ();
		const RE::NiPoint3 moveDir{ std::sin(yaw), std::cos(yaw), 0.f };

		const std::uint32_t filter = Raycast::GetPlayerFilterInfo(a_actor);

		{
			constexpr float kMinGroundNormalZ = 0.95f;
			const RE::NiPoint3 gndStart{ actorPos.x, actorPos.y, footZ + 10.f };
			const RE::NiPoint3 gndEnd{ actorPos.x, actorPos.y, footZ - 30.f };
			RE::hkpWorldRayCastOutput hitGnd{};
			if (Raycast::CastRayWorld(a_world, filter, gndStart, gndEnd, hitGnd)) {
				if (HitNormalUpDot(hitGnd.normal) < kMinGroundNormalZ) {
					return;
				}
			}
		}

		const RE::NiPoint3 lowStart{ actorPos.x, actorPos.y, footZ + s->shinHeight };
		const RE::NiPoint3 lowEnd = lowStart + moveDir * s->forwardDetectionDistance;
		RE::hkpWorldRayCastOutput hitLow{};
		if (!Raycast::CastRayWorld(a_world, filter, lowStart, lowEnd, hitLow)) {
			return;
		}

		// Only step onto static geometry (terrain, architecture).  Actors
		// have unique non-zero collision groups; terrain uses group 0.
		if (hitLow.rootCollidable) {
			const std::uint32_t hitFilter = hitLow.rootCollidable->broadPhaseHandle.collisionFilterInfo.filter;
			const std::uint16_t hitGroup  = static_cast<std::uint16_t>(hitFilter >> 16);
			if (hitGroup != 0) {
				return;
			}
		}

		const float dLow = hitLow.hitFraction * s->forwardDetectionDistance;

		constexpr float kMaxRiserSlopeZ = 0.5f;
		if (std::abs(HitNormalUpDot(hitLow.normal)) > kMaxRiserSlopeZ) {
			return;
		}

		const RE::NiPoint3 highStart{ actorPos.x, actorPos.y, footZ + s->maxStepHeight };
		const RE::NiPoint3 highEnd = highStart + moveDir * s->forwardDetectionDistance;
		float fracHigh = 0.f;
		if (Raycast::CastRayWorldFraction(a_world, filter, highStart, highEnd, fracHigh)) {
			const float dHigh = fracHigh * s->forwardDetectionDistance;
			if (dHigh <= dLow + s->sameWallDistanceEpsilon) {
				return;
			}
		}

		RE::NiPoint3 overStep = lowStart + moveDir * (dLow + s->stepForwardPastRiser);
		overStep.z = footZ + s->maxStepHeight + 15.f;
		const RE::NiPoint3 downStart{ overStep.x, overStep.y, overStep.z };
		const RE::NiPoint3 downEnd{ overStep.x, overStep.y, footZ - 100.f };

		RE::hkpWorldRayCastOutput hitDown{};
		if (!Raycast::CastRayWorld(a_world, filter, downStart, downEnd, hitDown)) {
			return;
		}

		const float targetZ = downStart.z + (downEnd.z - downStart.z) * hitDown.hitFraction;
		const float deltaZ = targetZ - footZ;
		if (deltaZ < s->minStepHeight || deltaZ > s->maxStepHeight) {
			return;
		}

		const float minNormalZ = std::cos(s->maxSlopeAngle * kPi / 180.f);
		if (HitNormalUpDot(hitDown.normal) < minNormalZ) {
			return;
		}

		constexpr float kLandingMargin = 2.f;
		constexpr float kForwardNudge  = 4.f;
		const float lift = (targetZ + kLandingMargin) - footZ;
		const bool  nudge = s->enableNPCCompletionNudge;

		const float worldScale = RE::bhkWorld::GetWorldScale();
		RE::hkVector4 hkPos;
		a_controller->GetPositionImpl(hkPos, false);
		auto* posF = reinterpret_cast<float*>(&hkPos);
		if (nudge) {
			posF[0] += moveDir.x * kForwardNudge * worldScale;
			posF[1] += moveDir.y * kForwardNudge * worldScale;
		}
		posF[2] += lift * worldScale;
		a_controller->SetPositionImpl(hkPos, false, true);

		if (nudge) {
			a_actor->data.location.x += moveDir.x * kForwardNudge;
			a_actor->data.location.y += moveDir.y * kForwardNudge;
		}
		a_actor->data.location.z += lift;

		constexpr float kWarpCooldown = 0.35f;
		a_state.cooldownRemaining = kWarpCooldown;
	}

	void StepUpManager::RestoreGravity(RE::bhkCharacterController* a_controller)
	{
		if (!m_gravityDisabled || !a_controller) {
			return;
		}
		a_controller->gravity = m_savedGravity;
		if (!m_hadNoGravityFlag) {
			a_controller->flags.reset(RE::CHARACTER_FLAGS::kNoGravityOnGround);
		}
		if (!m_hadOnStairsFlag) {
			a_controller->flags.reset(RE::CHARACTER_FLAGS::kOnStairs);
		}
		m_gravityDisabled = false;
	}
}
