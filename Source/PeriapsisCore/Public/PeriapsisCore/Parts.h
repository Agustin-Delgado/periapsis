// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/Mat3.h"

#include <optional>
#include <string>
#include <vector>

namespace Periapsis::Core
{
	// Parts (spec 5.1 and 7.1). Every part has its own frame: +Z is its axis ("up" when the
	// vessel stands on the pad), and its origin is the centre of its geometry. Parts connect at
	// attach nodes, or by their sides ("surface attachment") when they are round.

	// A point where another part can connect: a position in the part's frame, and the outward
	// direction a connected part sits in.
	struct AttachNode
	{
		std::string Id;
		Vec3 Position;
		Vec3 Direction;
		double Size = 1.0; // nominal diameter class, for matching in the builder
	};

	// The solid a part occupies, for mass distribution, collision and drawing. A frustum along
	// +Z (cylinders, cones, adapters, engine bells), or a box.
	struct PartGeometry
	{
		enum class EShape
		{
			Frustum,
			Box,
		};
		EShape Shape = EShape::Frustum;
		double Length = 1.0;       // along Z, m
		double BottomRadius = 0.5; // at -Length/2 (frustum), m
		double TopRadius = 0.5;    // at +Length/2 (frustum), m
		double Width = 1.0;        // edge across X and Y (box), m

		double MaxRadius() const { return Shape == EShape::Box ? 0.5 * Width : (BottomRadius > TopRadius ? BottomRadius : TopRadius); }
		// Radius of the side at a height along Z (frustum: interpolated; box: half the width).
		PERIAPSISCORE_API double RadiusAt(double Height) const;
		// Furthest extent of the solid along a unit direction, from the part's origin.
		PERIAPSISCORE_API double Support(const Vec3& Direction) const;
		// Height of the centre of mass along Z with uniform density (a cone's sits low).
		PERIAPSISCORE_API double CentroidHeight() const;
		// Inertia tensor of the solid with uniform density about its origin, in the part's frame.
		PERIAPSISCORE_API Mat3 UnitInertia(double Mass) const;
		// Corner points whose convex hull is the solid (rings of a frustum, corners of a box).
		PERIAPSISCORE_API std::vector<Vec3> HullPoints(int Segments) const;
	};

	// A propellant an engine burns, and the share of the engine's mass flow it takes.
	struct Propellant
	{
		int Resource = -1;         // index into the catalog's resources
		double MassFraction = 0.0; // the fractions of an engine add up to 1
	};

	struct EngineModule
	{
		double ThrustVacuum = 0.0;  // N
		double IspVacuum = 0.0;     // s
		double IspSeaLevel = 0.0;   // s
		std::vector<Propellant> Propellants;
		double GimbalDegrees = 0.0; // data only until torques arrive
		// The engine pushes along its part's +Z (its exhaust leaves through -Z).
	};

	// Breaks the joint to its parent when its stage fires: the decoupler leaves with what hangs
	// below it, pushed away with an impulse.
	struct DecouplerModule
	{
		double EjectionImpulse = 0.0; // N s, shared by both sides, opposite directions
		bool bCrossfeed = false;      // let propellant flow across it while attached
	};

	struct ResourceCapacity
	{
		int Resource = -1;
		double Mass = 0.0; // kg when full
	};

	struct PartDefinition
	{
		std::string Id;
		std::string Name;
		std::string Category; // command, tank, engine, decoupler, structure, ...
		double DryMass = 0.0; // kg
		double Cost = 0.0;
		PartGeometry Geometry;
		std::vector<AttachNode> Nodes;
		bool bSurfaceAttach = true; // round parts take parts on their sides
		// Strength of the joint to the part's parent (spec 5.2).
		double JointForce = 0.0;  // N
		double JointTorque = 0.0; // N m
		std::vector<ResourceCapacity> Resources;
		std::optional<EngineModule> Engine;
		std::optional<DecouplerModule> Decoupler;
		bool bCommand = false; // can be controlled: the root of a flyable vessel

		PERIAPSISCORE_API const AttachNode* FindNode(const std::string& NodeId) const;
		PERIAPSISCORE_API double WetMass() const;
		bool HasStageAction() const { return Engine.has_value() || Decoupler.has_value(); }
	};

	struct ResourceDefinition
	{
		std::string Id;
		std::string Name;
	};

	// Every part and resource the game knows, as loaded from data.
	struct PartCatalog
	{
		std::vector<ResourceDefinition> ResourceTypes;
		std::vector<PartDefinition> Parts;

		PERIAPSISCORE_API int FindPart(const std::string& Id) const;
		PERIAPSISCORE_API int FindResource(const std::string& Id) const;
		// One line per problem: dangling references, propellant fractions, bad numbers.
		PERIAPSISCORE_API std::vector<std::string> Validate() const;
	};

	// A triangle mesh of a part's solid in its own frame, counter-clockwise seen from outside.
	struct PartMesh
	{
		std::vector<Vec3> Positions;
		std::vector<Vec3> Normals;
		std::vector<int> Triangles;
	};
	PERIAPSISCORE_API PartMesh BuildPartMesh(const PartGeometry& Geometry, int Segments);

	// What a piece of a part's look is painted with; the view picks the colours.
	enum class PartPaint
	{
		Body,   // the part's own colour (by category)
		Trim,   // dark rings at the joints, a docking ring
		Bell,   // an engine's nozzle, outside
		Inside, // an engine's nozzle, inside
		Shield, // a pod's heat shield
		Window, // a pod's window band
		Accent, // the tip of a nose cone
	};

	struct PartLookPiece
	{
		PartPaint Paint = PartPaint::Body;
		PartMesh Mesh;
	};

	// How a part is drawn: its solid, detailed by category (rings on tanks, a bell on engines, a
	// heat shield on pods), never outside the solid that collides. One piece per paint.
	// Counter-clockwise seen from outside, like BuildPartMesh.
	PERIAPSISCORE_API std::vector<PartLookPiece> BuildPartLook(const PartDefinition& Part, int Segments);
}
