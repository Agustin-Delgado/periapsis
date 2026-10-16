// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/CelestialSystem.h"

namespace Periapsis::Core
{
	struct BubbleSettings;

	// The local physics frame of the spec (3.4 d), where the physics bubble is simulated. It
	// comes in two kinds (spec 3.4, open decision, settled in ADR 0004):
	//
	// - Free-falling, in space. The origin free-falls on a Keplerian orbit around a body and the
	//   axes are the body's inertial ones. Anything coasting next to the origin stays nearly
	//   still in it, whatever its orbital speed, and gravity in the frame is the small
	//   difference between the pull on an object and the pull on the origin.
	// - Body-fixed, near the ground. The origin is a point fixed to the body and the axes turn
	//   with it: east, north and up at the origin. The ground stands still, a landed vessel is
	//   at rest, and gravity comes with the centrifugal and Coriolis terms.
	//
	// Objects that move away from the origin are brought back by rebasing it (the floating
	// origin of the spec, 3.5). Changing between body-relative and local states is exact in
	// double precision: entering and leaving the bubble adds no error.
	class LocalFrame
	{
	public:
		enum class EKind
		{
			FreeFalling,
			BodyFixed,
		};

		LocalFrame() = default;
		// Free-falling frame with its origin on InOrigin (relative to InBodyIndex at InTime).
		PERIAPSISCORE_API LocalFrame(const CelestialSystem& InSystem, int InBodyIndex, const StateVector& InOrigin, double InTime);
		// Body-fixed frame with its origin on the ground point under (or at) an inertial position.
		PERIAPSISCORE_API static LocalFrame MakeBodyFixed(const CelestialSystem& InSystem, int InBodyIndex, const Vec3& InOrigin, double InTime);

		bool IsValid() const { return System != nullptr; }
		EKind Kind() const { return FrameKind; }
		bool IsBodyFixed() const { return FrameKind == EKind::BodyFixed; }
		int BodyIndex() const { return Body; }
		double Time() const { return CurrentTime; }
		// The origin's state relative to BodyIndex() at Time(), in the body's inertial axes.
		PERIAPSISCORE_API StateVector Origin() const;

		// Moves the frame to a new time (either direction): the origin along its orbit, or the
		// axes with the body's rotation.
		PERIAPSISCORE_API void AdvanceTo(double NewTime);

		// Re-expresses a free-falling origin relative to another body, for when the bubble
		// crosses a sphere of influence. The origin itself does not move, so local coordinates
		// keep their meaning; only the gravity that defines "free fall" changes. A body-fixed
		// frame becomes free-falling first.
		PERIAPSISCORE_API void ChangeBody(int NewBodyIndex);

		// Body-relative (BodyIndex(), at Time(), inertial axes) to local, and back.
		PERIAPSISCORE_API StateVector ToLocal(const StateVector& BodyRelative) const;
		PERIAPSISCORE_API StateVector ToBody(const StateVector& Local) const;
		// Directions only (no translation, no velocity of the frame).
		PERIAPSISCORE_API Vec3 DirectionToLocal(const Vec3& Inertial) const;
		PERIAPSISCORE_API Vec3 DirectionToBody(const Vec3& Local) const;

		// Acceleration of a free object, as seen from the frame. Free-falling: the body's pull
		// minus its pull on the origin. Body-fixed: the pull plus centrifugal and Coriolis.
		PERIAPSISCORE_API Vec3 Acceleration(const StateVector& Local) const;

		// The acceleration to hold constant over a physics step of Dt starting from Local:
		// evaluated at the middle of the step, which makes a held acceleration second-order
		// accurate instead of first.
		PERIAPSISCORE_API Vec3 StepGravity(const StateVector& Local, double Dt) const;

		// Puts the origin on NewOrigin (given in local coordinates) and returns the shift that
		// every object in the frame must have subtracted from its local state. A body-fixed
		// origin only moves in position (its velocity is the ground's) and keeps its axes.
		PERIAPSISCORE_API StateVector Rebase(const StateVector& NewOrigin);
		PERIAPSISCORE_API bool NeedsRebase(const StateVector& ActiveLocal, const BubbleSettings& Settings) const;

	private:
		const CelestialSystem* System = nullptr;
		EKind FrameKind = EKind::FreeFalling;
		int Body = kNoBody;
		double CurrentTime = 0.0;

		// Free-falling.
		StateVector EpochOrigin; // propagation always starts here, so steps do not accumulate error
		double Epoch = 0.0;
		StateVector CurrentOrigin;

		// Body-fixed: origin and axes in the body-fixed frame.
		Vec3 FixedOrigin;
		Vec3 East{1.0, 0.0, 0.0};
		Vec3 North{0.0, 1.0, 0.0};
		Vec3 Up{0.0, 0.0, 1.0};
		double Angle = 0.0; // body rotation at CurrentTime
		double Spin = 0.0;  // rad/s

		Vec3 FixedToLocal(const Vec3& V) const { return {Dot(V, East), Dot(V, North), Dot(V, Up)}; }
		Vec3 LocalToFixed(const Vec3& V) const { return East * V.X + North * V.Y + Up * V.Z; }
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
		// ...or, in a free-falling frame, this fast relative to it (a burn builds up speed).
		double RebaseSpeed = 100.0;
	};

	// Whether a vessel at Distance from the active one belongs in the bubble, given where it is now.
	inline bool BelongsInBubble(bool bInBubble, double Distance, const BubbleSettings& Settings)
	{
		return bInBubble ? Distance <= Settings.UnloadDistance : Distance < Settings.LoadDistance;
	}
}
