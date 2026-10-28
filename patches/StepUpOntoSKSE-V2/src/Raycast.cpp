#include "Raycast.h"

namespace StepUp::Raycast
{
	namespace
	{
		// Resolved once on first use; the game address is constant for the process lifetime.
		float GetWorldScale()
		{
			static float* const kWorldScale =
				reinterpret_cast<float*>(RELOCATION_ID(231896, 188105).address());
			return *kWorldScale;
		}

		bool CastRayImpl(
			RE::bhkWorld*              a_world,
			std::uint32_t              a_filterInfo,
			const RE::NiPoint3&        a_from,
			const RE::NiPoint3&        a_to,
			RE::hkpWorldRayCastOutput& a_outHit)
		{
			auto* hkworld = a_world ? a_world->GetWorld1() : nullptr;
			if (!hkworld) {
				return false;
			}

			const float scale = GetWorldScale();
			RE::hkpWorldRayCastInput input{};
			input.filterInfo.filter = a_filterInfo;
			input.from = RE::hkVector4(a_from.x * scale, a_from.y * scale, a_from.z * scale, 0.f);
			input.to   = RE::hkVector4(a_to.x   * scale, a_to.y   * scale, a_to.z   * scale, 0.f);

			a_world->worldLock.LockForRead();
			hkworld->CastRay(input, a_outHit);
			a_world->worldLock.UnlockForRead();

			return a_outHit.HasHit();
		}
	}

	bool CastRayWorldFraction(
		RE::bhkWorld*       a_world,
		std::uint32_t       a_filterInfo,
		const RE::NiPoint3& a_from,
		const RE::NiPoint3& a_to,
		float&              a_outFraction)
	{
		RE::hkpWorldRayCastOutput hitLocal{};
		if (!CastRayImpl(a_world, a_filterInfo, a_from, a_to, hitLocal)) {
			return false;
		}
		a_outFraction = hitLocal.hitFraction;
		return true;
	}

	bool CastRayWorld(
		RE::bhkWorld*              a_world,
		std::uint32_t              a_filterInfo,
		const RE::NiPoint3&        a_from,
		const RE::NiPoint3&        a_to,
		RE::hkpWorldRayCastOutput& a_outHit)
	{
		return CastRayImpl(a_world, a_filterInfo, a_from, a_to, a_outHit);
	}

	std::uint32_t GetPlayerFilterInfo(RE::Actor* a_actor)
	{
		// Cached: the player's collision group is set at load and never changes.
		static std::uint32_t s_filter = ~0u;
		if (s_filter != ~0u) {
			return s_filter;
		}

		if (!a_actor) {
			return 0u;
		}
		auto* node = a_actor->Get3D();
		if (!node) {
			return 0u;
		}
		auto* col = node->GetCollisionObject();
		if (!col) {
			return 0u;
		}
		auto* rb = col->GetRigidBody();
		if (!rb || !rb->referencedObject) {
			return 0u;
		}
		const auto* entity = static_cast<RE::hkpEntity*>(rb->referencedObject.get());
		const std::uint16_t group =
			static_cast<std::uint16_t>(entity->collidable.broadPhaseHandle.collisionFilterInfo.filter >> 16);

		s_filter = (static_cast<std::uint32_t>(group) << 16) | 0x28u;
		return s_filter;
	}
}
