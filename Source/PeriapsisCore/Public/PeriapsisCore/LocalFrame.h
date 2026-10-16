// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/CelestialSystem.h"

namespace Periapsis::Core
{
	// The local physics frame of the spec (3.4 d), where the physics bubble is simulated.
	//
	// Its origin free-falls on a Keplerian orbit around a body and the frame does not rotate.
	// Anything coasting next to the origin stays nearly still in it, whatever its orbital
	// speed: the physics engine only ever sees small positions and velocities, and gravity in
	// the frame is the small difference between the pull on an object and the pull on the
	// origin. Bodies that move away from the origin are brought back by rebasing it (the
	// floating origin of the spec, 3.5).
	//
	// Changing between body-relative and local coordinates is a translation in position and
	// velocity, exact in double precision: entering and leaving the bubble adds no error.
	class LocalFrame
	{
	public:
		LocalFrame() = default;
		// Origin is relative to BodyIndex, at Time.
		PERIAPSISCORE_API LocalFrame(const CelestialSystem& InSystem, int InBodyIndex, const StateVector& InOrigin, double InTime);

		bool IsValid() const { return System != nullptr; }
		int BodyIndex() const { return Body; }
		double Time() const { return CurrentTime; }
		// The origin relative to BodyIndex() at Time().
		const StateVector& Origin() const { return CurrentOrigin; }

		// Moves the origin along its orbit to a new time (either direction).
		PERIAPSISCORE_API void AdvanceTo(double NewTime);

		// Re-expresses the origin relative to another body, for when the bubble crosses a
		// sphere of influence. The origin itself does not move, so local coordinates keep
		// their meaning; only the gravity that defines "free fall" changes.
		PERIAPSISCORE_API void ChangeBody(int NewBodyIndex);

		// Body-relative (BodyIndex(), at Time()) to local, and back.
		StateVector ToLocal(const StateVector& BodyRelative) const
		{
			return {BodyRelative.Position - CurrentOrigin.Position, BodyRelative.Velocity - CurrentOrigin.Velocity};
		}
		StateVector ToBody(const StateVector& Local) const
		{
			return {Local.Position + CurrentOrigin.Position, Local.Velocity + CurrentOrigin.Velocity};
		}

		// Acceleration of a free object at a local position, as seen from the frame: the
		// body's pull there minus its pull on the origin.
		PERIAPSISCORE_API Vec3 RelativeGravity(const Vec3& LocalPosition) const;

		// The relative gravity to hold constant over a physics step of Dt starting from Local:
		// evaluated at the middle of the step (origin and object), which makes a held
		// acceleration second-order accurate instead of first.
		PERIAPSISCORE_API Vec3 StepGravity(const StateVector& Local, double Dt) const;

		// Puts the origin on NewOrigin (given in local coordinates) and returns it. Every
		// object in the frame must have the returned shift subtracted from its local state.
		PERIAPSISCORE_API StateVector Rebase(const StateVector& NewOrigin);

	private:
		const CelestialSystem* System = nullptr;
		int Body = kNoBody;
		StateVector EpochOrigin; // propagation always starts here, so steps do not accumulate error
		double Epoch = 0.0;
		StateVector CurrentOrigin;
		double CurrentTime = 0.0;
	};

	// Limits of the physics bubble (spec 3.3 and 3.5), in metres and metres per second.
	struct BubbleSettings
	{
		// Other vessels are loaded into the bubble closer than this to the active one...
		double LoadDistance = 2200.0;
		// ...and unloaded beyond this. The gap keeps a vessel at the edge from flickering.
		double UnloadDistance = 2500.0;
		// The origin is moved onto the active vessel when it gets this far from it...
		double RebaseDistance = 5000.0;
		// ...or this fast relative to it (a burn builds up speed in the frame).
		double RebaseSpeed = 100.0;
	};

	// Whether a vessel at Distance from the active one belongs in the bubble, given where it is now.
	inline bool BelongsInBubble(bool bInBubble, double Distance, const BubbleSettings& Settings)
	{
		return bInBubble ? Distance <= Settings.UnloadDistance : Distance < Settings.LoadDistance;
	}

	// Whether the active vessel's local state calls for moving the origin onto it.
	inline bool NeedsRebase(const StateVector& ActiveLocal, const BubbleSettings& Settings)
	{
		return Norm(ActiveLocal.Position) > Settings.RebaseDistance || Norm(ActiveLocal.Velocity) > Settings.RebaseSpeed;
	}
}
