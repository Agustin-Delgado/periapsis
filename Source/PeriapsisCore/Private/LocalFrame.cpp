#include "PeriapsisCore/LocalFrame.h"

#include "PeriapsisCore/Flight.h"
#include "PeriapsisCore/Kepler.h"
#include "PeriapsisCore/Surface.h"

#include <cmath>

namespace Periapsis::Core
{
	LocalFrame::LocalFrame(const CelestialSystem& InSystem, int InBodyIndex, const StateVector& InOrigin, double InTime)
		: System(&InSystem)
		, Body(InBodyIndex)
		, CurrentTime(InTime)
		, EpochOrigin(InOrigin)
		, Epoch(InTime)
		, CurrentOrigin(InOrigin)
	{
	}

	LocalFrame LocalFrame::MakeBodyFixed(const CelestialSystem& InSystem, int InBodyIndex, const Vec3& InOrigin, double InTime)
	{
		LocalFrame Frame;
		Frame.System = &InSystem;
		Frame.FrameKind = EKind::BodyFixed;
		Frame.Body = InBodyIndex;
		const CelestialBody& B = InSystem.Body(InBodyIndex);
		Frame.Spin = AngularSpeed(B);
		Frame.CurrentTime = InTime;
		Frame.Angle = RotationAngle(B, InTime);
		Frame.FixedOrigin = ToBodyFixed(B, InOrigin, InTime);

		// East, north and up at the origin. On a pole east is undefined: take body-fixed +X.
		Frame.Up = Normalized(Frame.FixedOrigin);
		Frame.East = Normalized(Cross(Vec3{0.0, 0.0, 1.0}, Frame.Up));
		if (Norm(Frame.East) < 0.5)
		{
			Frame.East = {1.0, 0.0, 0.0};
		}
		Frame.North = Cross(Frame.Up, Frame.East);
		return Frame;
	}

	StateVector LocalFrame::Origin() const
	{
		if (FrameKind == EKind::FreeFalling)
		{
			return CurrentOrigin;
		}
		const Vec3 Position = RotateAboutZ(FixedOrigin, Angle);
		return {Position, Cross(Vec3{0.0, 0.0, Spin}, Position)};
	}

	void LocalFrame::AdvanceTo(double NewTime)
	{
		CurrentTime = NewTime;
		if (FrameKind == EKind::FreeFalling)
		{
			CurrentOrigin = PropagateKepler(EpochOrigin, System->Body(Body).Mu, NewTime - Epoch);
		}
		else
		{
			Angle = RotationAngle(System->Body(Body), NewTime);
		}
	}

	void LocalFrame::ChangeBody(int NewBodyIndex)
	{
		if (NewBodyIndex == Body)
		{
			return;
		}
		const StateVector Here = Origin();
		*this = LocalFrame(*System, Body, Here, CurrentTime);
		CurrentOrigin = Periapsis::Core::ChangeFrame(*System, Body, NewBodyIndex, Here, CurrentTime);
		Body = NewBodyIndex;
		EpochOrigin = CurrentOrigin;
		Epoch = CurrentTime;
	}

	StateVector LocalFrame::ToLocal(const StateVector& BodyRelative) const
	{
		if (FrameKind == EKind::FreeFalling)
		{
			return {BodyRelative.Position - CurrentOrigin.Position, BodyRelative.Velocity - CurrentOrigin.Velocity};
		}
		const Vec3 Position = RotateAboutZ(BodyRelative.Position, -Angle);
		const Vec3 Velocity = RotateAboutZ(BodyRelative.Velocity - Cross(Vec3{0.0, 0.0, Spin}, BodyRelative.Position), -Angle);
		return {FixedToLocal(Position - FixedOrigin), FixedToLocal(Velocity)};
	}

	StateVector LocalFrame::ToBody(const StateVector& Local) const
	{
		if (FrameKind == EKind::FreeFalling)
		{
			return {Local.Position + CurrentOrigin.Position, Local.Velocity + CurrentOrigin.Velocity};
		}
		const Vec3 Position = RotateAboutZ(FixedOrigin + LocalToFixed(Local.Position), Angle);
		const Vec3 Velocity = RotateAboutZ(LocalToFixed(Local.Velocity), Angle) + Cross(Vec3{0.0, 0.0, Spin}, Position);
		return {Position, Velocity};
	}

	Vec3 LocalFrame::DirectionToLocal(const Vec3& Inertial) const
	{
		return FrameKind == EKind::FreeFalling ? Inertial : FixedToLocal(RotateAboutZ(Inertial, -Angle));
	}

	Vec3 LocalFrame::DirectionToBody(const Vec3& Local) const
	{
		return FrameKind == EKind::FreeFalling ? Local : RotateAboutZ(LocalToFixed(Local), Angle);
	}

	Vec3 LocalFrame::Acceleration(const StateVector& Local) const
	{
		const double Mu = System->Body(Body).Mu;
		if (FrameKind == EKind::FreeFalling)
		{
			return GravityAcceleration(CurrentOrigin.Position + Local.Position, Mu) - GravityAcceleration(CurrentOrigin.Position, Mu);
		}
		const Vec3 Omega{0.0, 0.0, Spin};
		const Vec3 Position = FixedOrigin + LocalToFixed(Local.Position);
		const Vec3 Velocity = LocalToFixed(Local.Velocity);
		const Vec3 Fixed = GravityAcceleration(Position, Mu) - Cross(Omega, Cross(Omega, Position)) - Cross(Omega, Velocity) * 2.0;
		return FixedToLocal(Fixed);
	}

	Vec3 LocalFrame::StepGravity(const StateVector& Local, double Dt) const
	{
		const double Half = 0.5 * Dt;
		if (FrameKind == EKind::FreeFalling)
		{
			const double Mu = System->Body(Body).Mu;
			const Vec3 OriginMid = PropagateKepler(EpochOrigin, Mu, CurrentTime + Half - Epoch).Position;
			const Vec3 LocalMid = Local.Position + Local.Velocity * Half;
			return GravityAcceleration(OriginMid + LocalMid, Mu) - GravityAcceleration(OriginMid, Mu);
		}
		// The body-fixed axes are fixed in this frame: only the object moves during the step.
		const Vec3 Start = Acceleration(Local);
		return Acceleration({Local.Position + Local.Velocity * Half, Local.Velocity + Start * Half});
	}

	StateVector LocalFrame::Rebase(const StateVector& NewOrigin)
	{
		if (FrameKind == EKind::FreeFalling)
		{
			CurrentOrigin = ToBody(NewOrigin);
			EpochOrigin = CurrentOrigin;
			Epoch = CurrentTime;
			return NewOrigin;
		}
		FixedOrigin = FixedOrigin + LocalToFixed(NewOrigin.Position);
		return {NewOrigin.Position, Vec3{}};
	}

	bool LocalFrame::NeedsRebase(const StateVector& ActiveLocal, const BubbleSettings& Settings) const
	{
		if (Norm(ActiveLocal.Position) > Settings.RebaseDistance)
		{
			return true;
		}
		return FrameKind == EKind::FreeFalling && Norm(ActiveLocal.Velocity) > Settings.RebaseSpeed;
	}
}
