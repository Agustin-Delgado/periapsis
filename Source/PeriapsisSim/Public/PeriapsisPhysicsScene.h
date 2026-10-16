#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/Mat3.h"
#include "PeriapsisCore/StateVector.h"
#include "PeriapsisCore/Vessel.h"

// What the universe needs from a physics engine to run the physics bubble (M3). The game
// implements it with Chaos; FPeriapsisPointMassScene stands in for it in tests.
//
// Everything is SI and in the bubble's local frame (Periapsis::Core::LocalFrame). The
// universe calls it from FPeriapsisUniverse::PhysicsStep, which runs right before the engine
// integrates one step, so states read here are the ones at the start of that step, and when a
// vessel stages. A vessel is one rigid body with a shape per part (spec 5.2); its state is the
// state of its centre of mass, and its attitude turns the vessel frame (the design's) into the
// local frame.
class IPeriapsisPhysicsScene
{
public:
	virtual ~IPeriapsisPhysicsScene() = default;

	// Gives a vessel a body shaped by its parts, or rebuilds it when the vessel lost parts. Engines
	// may take a step to add it to the simulation: until IsReady() it is not moved and its state
	// cannot be read or written.
	virtual void AddVessel(int32 Vessel, const Periapsis::Core::Vessel& Shape) = 0;
	virtual void RemoveVessel(int32 Vessel) = 0;
	virtual bool IsReady(int32 Vessel) const = 0;

	virtual Periapsis::Core::StateVector GetState(int32 Vessel) const = 0;
	virtual void SetState(int32 Vessel, const Periapsis::Core::StateVector& Local) = 0;
	// Mass and centre of mass (vessel frame), which move as the tanks drain.
	virtual void SetMass(int32 Vessel, double MassKg, const Periapsis::Core::Vec3& CenterOfMass) = 0;

	// Acceleration to hold through the next step (gravity and thrust, the engine applies no
	// gravity of its own). In contact with the ground the engine's contacts decide the motion:
	// the step is not corrected for free flight.
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration, bool bInContact) = 0;
	// The vessel frame's axes in the local frame; the vessel turns around its centre of mass.
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Mat3& Attitude) = 0;

	// A frozen body is at rest and not simulated: a vessel landed and asleep (spec 5.10).
	virtual void SetFrozen(int32 Vessel, bool bFrozen) = 0;

	// Whether the engine itself keeps vessels on the ground right now (terrain collision
	// around them). When it does not, the universe holds them up with the analytic ground.
	virtual bool HasGround() const = 0;
};

// Point masses with held accelerations, integrated exactly over each step. Bodies join the
// simulation one step after being added, as they do in Chaos.
class PERIAPSISSIM_API FPeriapsisPointMassScene : public IPeriapsisPhysicsScene
{
public:
	virtual void AddVessel(int32 Vessel, const Periapsis::Core::Vessel& Shape) override;
	virtual void RemoveVessel(int32 Vessel) override;
	virtual bool IsReady(int32 Vessel) const override;
	virtual Periapsis::Core::StateVector GetState(int32 Vessel) const override;
	virtual void SetState(int32 Vessel, const Periapsis::Core::StateVector& Local) override;
	virtual void SetMass(int32 Vessel, double MassKg, const Periapsis::Core::Vec3& CenterOfMass) override;
	virtual void SetAcceleration(int32 Vessel, const Periapsis::Core::Vec3& Acceleration, bool bInContact) override;
	virtual void SetAttitude(int32 Vessel, const Periapsis::Core::Mat3& Attitude) override;
	virtual void SetFrozen(int32 Vessel, bool bFrozen) override;
	virtual bool HasGround() const override { return false; }

	// Integrates one step; the accelerations are used once and cleared, as forces are.
	void Simulate(double Dt);

	int32 NumBodies() const { return Bodies.Num(); }
	// How many times a vessel was given a body (rebuilds after staging included).
	int32 NumAdds() const { return Adds; }

private:
	struct FBody
	{
		Periapsis::Core::StateVector State;
		Periapsis::Core::Vec3 Acceleration;
		double Mass = 0.0;
		bool bReady = false;
		bool bFrozen = false;
	};
	TMap<int32, FBody> Bodies;
	int32 Adds = 0;
};
