#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/StateVector.h"

// What the universe needs from a physics engine to run the physics bubble (M3). The game
// implements it with Chaos; FPeriapsisPointMassScene stands in for it in tests.
//
// Everything is SI and in the bubble's local frame (Periapsis::Core::LocalFrame). The
// universe only calls it from FPeriapsisUniverse::PhysicsStep, which runs right before the
// engine integrates one step, so states read here are the ones at the start of that step.
class IPeriapsisPhysicsScene
{
public:
	virtual ~IPeriapsisPhysicsScene() = default;

	// Gives a vessel a body. Engines may take a step to add it to the simulation: until
	// IsReady() it is not moved and its state cannot be read or written.
	virtual void AddVessel(int32 Vessel, double MassKg) = 0;
	virtual void RemoveVessel(int32 Vessel) = 0;
	virtual bool IsReady(int32 Vessel) const = 0;

	virtual Periapsis::Core::StateVector GetState(int32 Vessel) const = 0;
	virtual void SetState(int32 Vessel, const Periapsis::Core::StateVector& Local) = 0;
	virtual void SetMass(int32 Vessel, double MassKg) = 0;

	// Acceleration to hold through the next step (gravity and thrust, the engine applies no
	// gravity of its own), and the direction the vessel's nose points.
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration) = 0;
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Vec3& Forward) = 0;
};

// Point masses with held accelerations, integrated exactly over each step. Bodies join the
// simulation one step after being added, as they do in Chaos.
class PERIAPSISSIM_API FPeriapsisPointMassScene : public IPeriapsisPhysicsScene
{
public:
	virtual void AddVessel(int32 Vessel, double MassKg) override;
	virtual void RemoveVessel(int32 Vessel) override;
	virtual bool IsReady(int32 Vessel) const override;
	virtual Periapsis::Core::StateVector GetState(int32 Vessel) const override;
	virtual void SetState(int32 Vessel, const Periapsis::Core::StateVector& Local) override;
	virtual void SetMass(int32 Vessel, double MassKg) override;
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration) override;
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Vec3& Forward) override;

	// Integrates one step; the accelerations are used once and cleared, as forces are.
	void Simulate(double Dt);

	int32 NumBodies() const { return Bodies.Num(); }

private:
	struct FBody
	{
		Periapsis::Core::StateVector State;
		Periapsis::Core::Vec3 Acceleration;
		double Mass = 0.0;
		bool bReady = false;
	};
	TMap<int32, FBody> Bodies;
};
