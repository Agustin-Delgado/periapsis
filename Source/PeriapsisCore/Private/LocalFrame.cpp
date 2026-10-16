#include "PeriapsisCore/LocalFrame.h"

#include "PeriapsisCore/Flight.h"
#include "PeriapsisCore/Kepler.h"

namespace Periapsis::Core
{
	LocalFrame::LocalFrame(const CelestialSystem& InSystem, int InBodyIndex, const StateVector& InOrigin, double InTime)
		: System(&InSystem)
		, Body(InBodyIndex)
		, EpochOrigin(InOrigin)
		, Epoch(InTime)
		, CurrentOrigin(InOrigin)
		, CurrentTime(InTime)
	{
	}

	void LocalFrame::AdvanceTo(double NewTime)
	{
		CurrentTime = NewTime;
		CurrentOrigin = PropagateKepler(EpochOrigin, System->Body(Body).Mu, NewTime - Epoch);
	}

	void LocalFrame::ChangeBody(int NewBodyIndex)
	{
		if (NewBodyIndex == Body)
		{
			return;
		}
		CurrentOrigin = ChangeFrame(*System, Body, NewBodyIndex, CurrentOrigin, CurrentTime);
		Body = NewBodyIndex;
		EpochOrigin = CurrentOrigin;
		Epoch = CurrentTime;
	}

	Vec3 LocalFrame::RelativeGravity(const Vec3& LocalPosition) const
	{
		const double Mu = System->Body(Body).Mu;
		return GravityAcceleration(CurrentOrigin.Position + LocalPosition, Mu) - GravityAcceleration(CurrentOrigin.Position, Mu);
	}

	Vec3 LocalFrame::StepGravity(const StateVector& Local, double Dt) const
	{
		const double Mu = System->Body(Body).Mu;
		const double Half = 0.5 * Dt;
		const Vec3 OriginMid = PropagateKepler(EpochOrigin, Mu, CurrentTime + Half - Epoch).Position;
		const Vec3 LocalMid = Local.Position + Local.Velocity * Half;
		return GravityAcceleration(OriginMid + LocalMid, Mu) - GravityAcceleration(OriginMid, Mu);
	}

	StateVector LocalFrame::Rebase(const StateVector& NewOrigin)
	{
		CurrentOrigin = ToBody(NewOrigin);
		EpochOrigin = CurrentOrigin;
		Epoch = CurrentTime;
		return NewOrigin;
	}
}
