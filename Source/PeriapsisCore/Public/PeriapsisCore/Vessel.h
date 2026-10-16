// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/VesselDesign.h"

#include <memory>
#include <string>
#include <vector>

namespace Periapsis::Core
{
	// A point ship with a single engine, as M2 described it. Scenarios can still give a ship
	// this way; it becomes a one-part vessel.
	struct ShipConfig
	{
		double DryMass = 0.0;    // kg
		double FuelMass = 0.0;   // kg, at the start
		double Thrust = 0.0;     // N, vacuum
		double IspSeconds = 0.0; // s, vacuum
	};

	// A part of a flying vessel: where it is in the vessel frame and what is left in it.
	struct VesselPart
	{
		PartInstance Instance; // placement in the vessel frame of the design it came from
		std::vector<double> Amounts; // kg, one per entry of the definition's Resources
		bool bEngineActive = false;  // ignited by its stage (it may have run dry since)
		int Domain = -1;             // parts that share propellant have the same domain
	};

	// What the engines do at a throttle and an ambient pressure.
	struct Propulsion
	{
		double Thrust = 0.0;   // N, along the vessel's +Z
		double MassFlow = 0.0; // kg/s
		int BurningEngines = 0;
	};

	// A piece that came off (defined below the vessel, which it holds).
	struct Separation;

	// Load on the joint of a part to its parent, as a fraction of the joint's strength.
	struct JointLoad
	{
		int Part = -1;
		double Force = 0.0;  // N
		double Torque = 0.0; // N m
		double Ratio = 0.0;  // the larger of force and torque over their limits
	};

	// A vessel in flight (spec 5.1 to 5.5): its parts, the propellant in them, the engines
	// that are lit and the next stage to fire. The vessel frame is the root part's frame of the
	// design; the vessel is one rigid body (spec 5.2), its joints carry loads but do not bend.
	//
	// Propellant flows within a domain: the parts connected without crossing a decoupler
	// (unless it allows crossfeed). An engine burns from every tank of its domain; tanks with a
	// higher flow priority empty first, and tanks of the same priority drain together, in
	// proportion to what they hold. An engine flames out when any of its propellants is gone
	// from its domain.
	class Vessel
	{
	public:
		Vessel() = default;
		// Full tanks, nothing lit.
		PERIAPSISCORE_API Vessel(std::shared_ptr<const PartCatalog> InCatalog, const VesselDesign& Design);

		// The prototype ship of M2 to M4 as a one-part vessel with its engine already lit, and a
		// cube of a given mass with nothing in it, for scenarios that describe vessels that way.
		PERIAPSISCORE_API static Vessel FromShipConfig(const ShipConfig& Config, double Length, double Diameter);
		PERIAPSISCORE_API static Vessel Inert(double Mass, double Size);

		const std::string& Name() const { return VesselName; }
		void SetName(const std::string& InName) { VesselName = InName; }
		const PartCatalog& Catalog() const { return *CatalogRef; }
		const std::shared_ptr<const PartCatalog>& CatalogPtr() const { return CatalogRef; }
		const std::vector<VesselPart>& Parts() const { return VesselParts; }
		const PartDefinition& Definition(int Part) const { return CatalogRef->Parts[VesselParts[Part].Instance.Part]; }
		bool IsValid() const { return CatalogRef != nullptr && !VesselParts.empty(); }
		bool IsControllable() const { return IsValid() && Definition(0).bCommand; }

		// Mass properties (spec 5.3), with the propellant left.
		PERIAPSISCORE_API double Mass() const;
		PERIAPSISCORE_API double DryMass() const;
		PERIAPSISCORE_API double PropellantMass() const;
		PERIAPSISCORE_API Vec3 CenterOfMass() const;
		// Inertia tensor about the centre of mass, in the vessel frame.
		PERIAPSISCORE_API Mat3 Inertia() const;
		PERIAPSISCORE_API double Cost() const;
		// How far the vessel reaches along a unit direction (vessel frame) from a point.
		PERIAPSISCORE_API double Extent(const Vec3& Direction, const Vec3& From) const;
		// Radius of a sphere around the centre of mass that holds every part.
		PERIAPSISCORE_API double BoundingRadius() const;

		// Engines and propellant.
		PERIAPSISCORE_API Propulsion Engines(double Throttle, double PressurePa = 0.0) const;
		bool HasThrust() const { return Engines(1.0).BurningEngines > 0; }
		// Time at a throttle until the next engine flames out (infinite when none burns).
		PERIAPSISCORE_API double TimeToFlameout(double Throttle) const;
		// Burns for Dt at a throttle. Dt should not go past TimeToFlameout(): the flows are
		// taken as constant over it.
		PERIAPSISCORE_API void Consume(double Dt, double Throttle);
		PERIAPSISCORE_API bool IsEngineBurning(int Part) const;

		// Stages (spec 5.5). NextStage() fires next; StageCount() is one past the last.
		int NextStage() const { return NextStageIndex; }
		PERIAPSISCORE_API int StageCount() const;
		// Lights the stage's engines and fires its decouplers. The pieces that come off are
		// returned; this vessel keeps the root part.
		PERIAPSISCORE_API std::vector<Separation> ActivateNextStage();

		// Joint loads under the engines at a throttle (spec 5.2): forces and moments that hold
		// every subtree to the rest while the thrust accelerates the vessel. Gravity pulls on
		// every part alike and adds no load in free flight.
		PERIAPSISCORE_API std::vector<JointLoad> JointLoads(double Throttle, double PressurePa = 0.0) const;
		// Breaks the joint of a part to its parent: the part and what hangs below it come off
		// with no impulse.
		PERIAPSISCORE_API Separation BreakJoint(int Part);

	private:
		Separation Detach(int Part, double Impulse);
		// Propellant per domain and resource, [Domain * resources + Resource], in one pass.
		std::vector<double> Holdings() const;
		bool Burns(int Part, const std::vector<double>& Held) const;
		// Mass flow out of every domain and resource, laid out like Holdings().
		std::vector<double> Demand(double Throttle, const std::vector<double>& Held) const;
		void UpdateDomains();
		Vec3 PartCenterOfMass(int Part) const;
		double PartMass(int Part) const;

		std::shared_ptr<const PartCatalog> CatalogRef;
		std::string VesselName;
		std::vector<VesselPart> VesselParts;
		int NextStageIndex = 0;
	};

	// A piece that came off: the vessel it is now, and where the two centres of mass are and
	// what velocity each gained, relative to the vessel just before the split, in its frame.
	// When a stage separates several pieces, each is relative to the vessel left by the
	// previous one: apply them in order.
	struct Separation
	{
		Vessel Piece;
		Vec3 Offset;     // the piece's centre of mass minus the old vessel's
		Vec3 DeltaV;     // velocity the piece gained
		Vec3 RestOffset; // the remaining vessel's centre of mass minus the old one
		Vec3 RestDeltaV; // velocity the remaining vessel gained
	};
}
