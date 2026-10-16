#include "PeriapsisPhysicsScene.h"

using namespace Periapsis::Core;

void FPeriapsisPointMassScene::AddVessel(int32 Vessel, double MassKg)
{
	FBody& Body = Bodies.Add(Vessel);
	Body.Mass = MassKg;
}

void FPeriapsisPointMassScene::RemoveVessel(int32 Vessel)
{
	Bodies.Remove(Vessel);
}

bool FPeriapsisPointMassScene::IsReady(int32 Vessel) const
{
	const FBody* Body = Bodies.Find(Vessel);
	return Body && Body->bReady;
}

StateVector FPeriapsisPointMassScene::GetState(int32 Vessel) const
{
	return Bodies.FindChecked(Vessel).State;
}

void FPeriapsisPointMassScene::SetState(int32 Vessel, const StateVector& Local)
{
	Bodies.FindChecked(Vessel).State = Local;
}

void FPeriapsisPointMassScene::SetMass(int32 Vessel, double MassKg)
{
	Bodies.FindChecked(Vessel).Mass = MassKg;
}

void FPeriapsisPointMassScene::SetAcceleration(int32 Vessel, const Vec3& Acceleration)
{
	Bodies.FindChecked(Vessel).Acceleration = Acceleration;
}

void FPeriapsisPointMassScene::SetAttitude(int32, const Vec3&)
{
	// Point masses have no orientation.
}

void FPeriapsisPointMassScene::Simulate(double Dt)
{
	for (TPair<int32, FBody>& Pair : Bodies)
	{
		FBody& Body = Pair.Value;
		if (Body.bReady)
		{
			const Vec3& A = Body.Acceleration;
			Body.State = {Body.State.Position + Body.State.Velocity * Dt + A * (0.5 * Dt * Dt), Body.State.Velocity + A * Dt};
		}
		Body.Acceleration = {};
		Body.bReady = true;
	}
}
