#include "PeriapsisCore/Vessel.h"

#include "PeriapsisCore/Constants.h"
#include "PeriapsisCore/Rocket.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Periapsis::Core
{
	namespace
	{
		// Joints of the one-part vessels never break.
		constexpr double kUnbreakable = 1e30;

		Vessel OnePartVessel(PartDefinition Part, std::vector<ResourceDefinition> Resources)
		{
			auto Catalog = std::make_shared<PartCatalog>();
			Catalog->ResourceTypes = std::move(Resources);
			Catalog->Parts.push_back(std::move(Part));
			VesselDesign Design;
			Design.SetRoot(*Catalog, 0);
			return Vessel(Catalog, Design);
		}
	}

	Vessel::Vessel(std::shared_ptr<const PartCatalog> InCatalog, const VesselDesign& Design)
		: CatalogRef(std::move(InCatalog))
		, VesselName(Design.Name)
	{
		for (const PartInstance& Instance : Design.Parts())
		{
			VesselPart Part;
			Part.Instance = Instance;
			for (const ResourceCapacity& Capacity : CatalogRef->Parts[Instance.Part].Resources)
			{
				Part.Amounts.push_back(Capacity.Mass);
			}
			VesselParts.push_back(std::move(Part));
		}
		UpdateDomains();
	}

	Vessel Vessel::FromShipConfig(const ShipConfig& Config, double Length, double Diameter)
	{
		PartDefinition Hull;
		Hull.Id = "ship_hull";
		Hull.Name = "Ship";
		Hull.Category = "command";
		Hull.DryMass = Config.DryMass;
		Hull.Geometry.Length = Length;
		Hull.Geometry.BottomRadius = Hull.Geometry.TopRadius = 0.5 * Diameter;
		Hull.JointForce = Hull.JointTorque = kUnbreakable;
		Hull.bCommand = true;
		if (Config.FuelMass > 0.0)
		{
			Hull.Resources.push_back({0, Config.FuelMass});
		}
		EngineModule Engine;
		Engine.ThrustVacuum = Config.Thrust;
		Engine.IspVacuum = Engine.IspSeaLevel = Config.IspSeconds;
		Engine.Propellants.push_back({0, 1.0});
		Hull.Engine = Engine;
		Vessel Result = OnePartVessel(std::move(Hull), {{"fuel", "Fuel"}});
		// Lit from the start, without a stage: the M2 ship had no staging.
		Result.VesselParts[0].bEngineActive = Config.Thrust > 0.0;
		return Result;
	}

	Vessel Vessel::Inert(double Mass, double Size)
	{
		PartDefinition Box;
		Box.Id = "inert_box";
		Box.Name = "Inert";
		Box.Category = "structure";
		Box.DryMass = Mass;
		Box.Geometry.Shape = PartGeometry::EShape::Box;
		Box.Geometry.Length = Size;
		Box.Geometry.Width = Size;
		Box.JointForce = Box.JointTorque = kUnbreakable;
		return OnePartVessel(std::move(Box), {});
	}

	double Vessel::PartMass(int Part) const
	{
		double Mass = Definition(Part).DryMass;
		for (const double Amount : VesselParts[Part].Amounts)
		{
			Mass += Amount;
		}
		return Mass;
	}

	Vec3 Vessel::PartCenterOfMass(int Part) const
	{
		const PartInstance& Instance = VesselParts[Part].Instance;
		return Instance.Position + Instance.Rotation * Vec3{0.0, 0.0, Definition(Part).Geometry.CentroidHeight()};
	}

	double Vessel::Mass() const
	{
		double Mass = 0.0;
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			Mass += PartMass(Part);
		}
		return Mass;
	}

	double Vessel::DryMass() const
	{
		double Mass = 0.0;
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			Mass += Definition(Part).DryMass;
		}
		return Mass;
	}

	double Vessel::PropellantMass() const
	{
		return Mass() - DryMass();
	}

	double Vessel::Cost() const
	{
		double Total = 0.0;
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			Total += Definition(Part).Cost;
		}
		return Total;
	}

	Vec3 Vessel::CenterOfMass() const
	{
		Vec3 Moment;
		double Total = 0.0;
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			const double M = PartMass(Part);
			Moment += PartCenterOfMass(Part) * M;
			Total += M;
		}
		return Total > 0.0 ? Moment / Total : Vec3{};
	}

	Mat3 Vessel::Inertia() const
	{
		const Vec3 Center = CenterOfMass();
		Mat3 Total = Mat3::Zero();
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			const PartGeometry& Geometry = Definition(Part).Geometry;
			const PartInstance& Instance = VesselParts[Part].Instance;
			const double M = PartMass(Part);
			// About the part's own centre of mass (parallel axis back from its origin), turned into
			// the vessel frame, then carried to the vessel's centre of mass.
			const Vec3 Local{0.0, 0.0, Geometry.CentroidHeight()};
			const Mat3 AboutOrigin = Geometry.UnitInertia(M);
			const Mat3 AboutOwn = AboutOrigin + (Mat3::Identity() * NormSquared(Local) + Outer(Local, Local) * -1.0) * -M;
			const Mat3 Turned = Instance.Rotation * AboutOwn * Instance.Rotation.Transposed();
			const Vec3 D = PartCenterOfMass(Part) - Center;
			Total = Total + Turned + (Mat3::Identity() * NormSquared(D) + Outer(D, D) * -1.0) * M;
		}
		return Total;
	}

	double Vessel::Extent(const Vec3& Direction, const Vec3& From) const
	{
		double Reach = -std::numeric_limits<double>::infinity();
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			const PartInstance& Instance = VesselParts[Part].Instance;
			const double Here = Dot(Instance.Position - From, Direction) + Definition(Part).Geometry.Support(Instance.Rotation.Transposed() * Direction);
			Reach = std::max(Reach, Here);
		}
		return Reach;
	}

	double Vessel::BoundingRadius() const
	{
		const Vec3 Center = CenterOfMass();
		double Radius = 0.0;
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			const PartGeometry& G = Definition(Part).Geometry;
			const double Corner = std::sqrt(2.0 * G.MaxRadius() * G.MaxRadius() + 0.25 * G.Length * G.Length);
			Radius = std::max(Radius, Norm(VesselParts[Part].Instance.Position - Center) + Corner);
		}
		return Radius;
	}

	void Vessel::UpdateDomains()
	{
		// Union of each part with its parent, except across a decoupler without crossfeed.
		std::vector<int> Root(VesselParts.size());
		for (size_t Index = 0; Index < VesselParts.size(); ++Index)
		{
			Root[Index] = static_cast<int>(Index);
		}
		auto Find = [&](int Index)
		{
			while (Root[Index] != Index)
			{
				Index = Root[Index] = Root[Root[Index]];
			}
			return Index;
		};
		for (size_t Index = 0; Index < VesselParts.size(); ++Index)
		{
			const int Parent = VesselParts[Index].Instance.Parent;
			const PartDefinition& Part = Definition(static_cast<int>(Index));
			if (Parent >= 0 && !(Part.Decoupler && !Part.Decoupler->bCrossfeed))
			{
				Root[Find(static_cast<int>(Index))] = Find(Parent);
			}
		}
		for (size_t Index = 0; Index < VesselParts.size(); ++Index)
		{
			VesselParts[Index].Domain = Find(static_cast<int>(Index));
		}
	}

	std::vector<double> Vessel::Holdings() const
	{
		const size_t Resources = CatalogRef->ResourceTypes.size();
		std::vector<double> Held(VesselParts.size() * Resources, 0.0);
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			const VesselPart& Holder = VesselParts[Part];
			const std::vector<ResourceCapacity>& Capacities = Definition(Part).Resources;
			for (size_t Slot = 0; Slot < Capacities.size(); ++Slot)
			{
				Held[Holder.Domain * Resources + Capacities[Slot].Resource] += Holder.Amounts[Slot];
			}
		}
		return Held;
	}

	bool Vessel::Burns(int Part, const std::vector<double>& Held) const
	{
		const VesselPart& Engine = VesselParts[Part];
		const PartDefinition& Spec = Definition(Part);
		if (!Engine.bEngineActive || !Spec.Engine)
		{
			return false;
		}
		const size_t Resources = CatalogRef->ResourceTypes.size();
		for (const Propellant& P : Spec.Engine->Propellants)
		{
			if (!(Held[Engine.Domain * Resources + P.Resource] > 0.0))
			{
				return false;
			}
		}
		return true;
	}

	bool Vessel::IsEngineBurning(int Part) const
	{
		return Burns(Part, Holdings());
	}

	Propulsion Vessel::Engines(double Throttle, double PressurePa) const
	{
		Propulsion Result;
		if (!(Throttle > 0.0))
		{
			return Result;
		}
		const std::vector<double> Held = Holdings();
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			if (!Burns(Part, Held))
			{
				continue;
			}
			const EngineModule& Engine = *Definition(Part).Engine;
			const Vec3 Axis = VesselParts[Part].Instance.Rotation * Vec3{0.0, 0.0, 1.0};
			Result.Thrust += Throttle * ThrustAtPressure(Engine.ThrustVacuum, Engine.IspVacuum, Engine.IspSeaLevel, PressurePa) * Axis.Z;
			Result.MassFlow += Throttle * MassFlowRate(Engine.ThrustVacuum, Engine.IspVacuum);
			++Result.BurningEngines;
		}
		return Result;
	}

	std::vector<double> Vessel::Demand(double Throttle, const std::vector<double>& Held) const
	{
		const size_t Resources = CatalogRef->ResourceTypes.size();
		std::vector<double> Flows(Held.size(), 0.0);
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			if (!Burns(Part, Held))
			{
				continue;
			}
			const EngineModule& Engine = *Definition(Part).Engine;
			const double Flow = Throttle * MassFlowRate(Engine.ThrustVacuum, Engine.IspVacuum);
			for (const Propellant& P : Engine.Propellants)
			{
				Flows[VesselParts[Part].Domain * Resources + P.Resource] += Flow * P.MassFraction;
			}
		}
		return Flows;
	}

	double Vessel::TimeToFlameout(double Throttle) const
	{
		double Earliest = std::numeric_limits<double>::infinity();
		if (!(Throttle > 0.0))
		{
			return Earliest;
		}
		const std::vector<double> Held = Holdings();
		const std::vector<double> Flows = Demand(Throttle, Held);
		for (size_t Index = 0; Index < Flows.size(); ++Index)
		{
			if (Flows[Index] > 0.0)
			{
				Earliest = std::min(Earliest, Held[Index] / Flows[Index]);
			}
		}
		return Earliest;
	}

	void Vessel::Consume(double Dt, double Throttle)
	{
		if (!(Dt > 0.0) || !(Throttle > 0.0))
		{
			return;
		}
		const size_t Resources = CatalogRef->ResourceTypes.size();
		const std::vector<double> Flows = Demand(Throttle, Holdings());
		for (size_t Domain = 0; Domain < VesselParts.size(); ++Domain)
		{
			for (size_t Resource = 0; Resource < Resources; ++Resource)
			{
				double Needed = Flows[Domain * Resources + Resource] * Dt;
				if (!(Needed > 0.0))
				{
					continue;
				}
				// The tanks that hold it, from the highest priority down.
				struct FTank
				{
					double* Amount;
					int Priority;
				};
				std::vector<FTank> Tanks;
				for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
				{
					VesselPart& Holder = VesselParts[Part];
					if (Holder.Domain != static_cast<int>(Domain))
					{
						continue;
					}
					const std::vector<ResourceCapacity>& Capacities = Definition(Part).Resources;
					for (size_t Slot = 0; Slot < Capacities.size(); ++Slot)
					{
						if (Capacities[Slot].Resource == static_cast<int>(Resource) && Holder.Amounts[Slot] > 0.0)
						{
							Tanks.push_back({&Holder.Amounts[Slot], Holder.Instance.FlowPriority});
						}
					}
				}
				std::stable_sort(Tanks.begin(), Tanks.end(), [](const FTank& A, const FTank& B) { return A.Priority > B.Priority; });
				for (size_t First = 0; First < Tanks.size() && Needed > 0.0;)
				{
					size_t Last = First;
					double Held = 0.0;
					while (Last < Tanks.size() && Tanks[Last].Priority == Tanks[First].Priority)
					{
						Held += *Tanks[Last].Amount;
						++Last;
					}
					// What is left within rounding of the need is the end of it: empty exactly.
					const bool bEmpties = Needed >= Held * (1.0 - 1e-12);
					for (size_t Index = First; Index < Last; ++Index)
					{
						double& Amount = *Tanks[Index].Amount;
						Amount = bEmpties ? 0.0 : std::max(0.0, Amount - Needed * Amount / Held);
					}
					Needed = bEmpties ? Needed - Held : 0.0;
					First = Last;
				}
			}
		}
	}

	int Vessel::StageCount() const
	{
		int Count = 0;
		for (const VesselPart& Part : VesselParts)
		{
			Count = std::max(Count, Part.Instance.Stage + 1);
		}
		return Count;
	}

	std::vector<Separation> Vessel::ActivateNextStage()
	{
		std::vector<Separation> Result;
		const int Stage = NextStageIndex++;
		std::vector<int> Decouplers;
		for (int Part = 0; Part < static_cast<int>(VesselParts.size()); ++Part)
		{
			if (VesselParts[Part].Instance.Stage != Stage)
			{
				continue;
			}
			const PartDefinition& Spec = Definition(Part);
			if (Spec.Engine)
			{
				VesselParts[Part].bEngineActive = true;
			}
			if (Spec.Decoupler && VesselParts[Part].Instance.Parent >= 0)
			{
				Decouplers.push_back(Part);
			}
		}
		// Deepest first: a decoupler inside another one's piece comes off this vessel, not that piece.
		for (auto It = Decouplers.rbegin(); It != Decouplers.rend(); ++It)
		{
			Result.push_back(Detach(*It, Definition(*It).Decoupler->EjectionImpulse));
		}
		return Result;
	}

	Separation Vessel::BreakJoint(int Part)
	{
		return Detach(Part, 0.0);
	}

	Separation Vessel::Detach(int Part, double Impulse)
	{
		Separation Result;
		if (Part <= 0 || Part >= static_cast<int>(VesselParts.size()))
		{
			return Result;
		}
		const Vec3 OldCenter = CenterOfMass();
		const Vec3 Axis = VesselParts[Part].Instance.JointAxis;

		// The part and everything below it (parents always come before their children).
		std::vector<bool> InPiece(VesselParts.size(), false);
		InPiece[Part] = true;
		for (size_t Index = Part + 1; Index < VesselParts.size(); ++Index)
		{
			const int Parent = VesselParts[Index].Instance.Parent;
			InPiece[Index] = Parent >= 0 && InPiece[Parent];
		}
		std::vector<int> NewIndex(VesselParts.size(), -1);
		std::vector<VesselPart> Kept;
		std::vector<VesselPart> Taken;
		for (size_t Index = 0; Index < VesselParts.size(); ++Index)
		{
			std::vector<VesselPart>& Into = InPiece[Index] ? Taken : Kept;
			NewIndex[Index] = static_cast<int>(Into.size());
			VesselPart Copy = VesselParts[Index];
			const int Parent = Copy.Instance.Parent;
			Copy.Instance.Parent = (Parent >= 0 && InPiece[Parent] == InPiece[Index]) ? NewIndex[Parent] : -1;
			Into.push_back(std::move(Copy));
		}

		Vessel& Piece = Result.Piece;
		Piece.CatalogRef = CatalogRef;
		Piece.VesselName = VesselName;
		Piece.VesselParts = std::move(Taken);
		Piece.NextStageIndex = NextStageIndex;
		Piece.UpdateDomains();
		VesselParts = std::move(Kept);
		UpdateDomains();

		const double PieceMass = Piece.Mass();
		const double RestMass = Mass();
		Result.Offset = Piece.CenterOfMass() - OldCenter;
		Result.RestOffset = CenterOfMass() - OldCenter;
		Result.DeltaV = Axis * (Impulse / PieceMass);
		Result.RestDeltaV = Axis * (-Impulse / RestMass);
		return Result;
	}

	std::vector<JointLoad> Vessel::JointLoads(double Throttle, double PressurePa) const
	{
		const size_t Count = VesselParts.size();
		// Per subtree: mass, first moment of mass, thrust, and moment of the thrust about the origin.
		std::vector<double> SubMass(Count, 0.0);
		std::vector<Vec3> SubMoment(Count), SubThrust(Count), SubThrustMoment(Count);
		Vec3 TotalThrust;
		double TotalMass = 0.0;
		const std::vector<double> Held = Holdings();
		for (int Part = 0; Part < static_cast<int>(Count); ++Part)
		{
			const double M = PartMass(Part);
			SubMass[Part] = M;
			SubMoment[Part] = PartCenterOfMass(Part) * M;
			if (Throttle > 0.0 && Burns(Part, Held))
			{
				const EngineModule& Engine = *Definition(Part).Engine;
				const PartInstance& Instance = VesselParts[Part].Instance;
				const Vec3 Push = Instance.Rotation * Vec3{0.0, 0.0, 1.0}
					* (Throttle * ThrustAtPressure(Engine.ThrustVacuum, Engine.IspVacuum, Engine.IspSeaLevel, PressurePa));
				SubThrust[Part] = Push;
				SubThrustMoment[Part] = Cross(Instance.Position, Push);
				TotalThrust += Push;
			}
			TotalMass += M;
		}
		for (int Part = static_cast<int>(Count) - 1; Part > 0; --Part)
		{
			const int Parent = VesselParts[Part].Instance.Parent;
			SubMass[Parent] += SubMass[Part];
			SubMoment[Parent] += SubMoment[Part];
			SubThrust[Parent] += SubThrust[Part];
			SubThrustMoment[Parent] += SubThrustMoment[Part];
		}
		const Vec3 Acceleration = TotalMass > 0.0 ? TotalThrust / TotalMass : Vec3{};

		std::vector<JointLoad> Loads;
		for (int Part = 1; Part < static_cast<int>(Count); ++Part)
		{
			const Vec3 Joint = VesselParts[Part].Instance.JointPosition;
			// What the parent has to push on the subtree so it keeps up with the vessel.
			const Vec3 Force = Acceleration * SubMass[Part] - SubThrust[Part];
			const Vec3 Torque = Cross(SubMoment[Part] - Joint * SubMass[Part], Acceleration)
				- (SubThrustMoment[Part] - Cross(Joint, SubThrust[Part]));
			const PartDefinition& Spec = Definition(Part);
			JointLoad Load;
			Load.Part = Part;
			Load.Force = Norm(Force);
			Load.Torque = Norm(Torque);
			Load.Ratio = std::max(Load.Force / Spec.JointForce, Load.Torque / Spec.JointTorque);
			Loads.push_back(Load);
		}
		return Loads;
	}
}
