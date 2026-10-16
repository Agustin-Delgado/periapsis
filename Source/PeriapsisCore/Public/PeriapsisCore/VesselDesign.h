// Pure C++: no Unreal headers allowed in this module (see CLAUDE.md).
#pragma once

#include "PeriapsisCore/Api.h"

#include "PeriapsisCore/Parts.h"

#include <string>
#include <vector>

namespace Periapsis::Core
{
	// How a part hangs from its parent: a node (or the side) of the parent, and a node (or the
	// side) of the part. The part is placed so the two points meet, facing each other.
	struct Attachment
	{
		static constexpr const char* Surface = ""; // node id meaning "the side of the part"

		std::string ParentNode;     // node of the parent; empty: its side
		double SurfaceAngle = 0.0;  // rad around the parent's Z, on its side
		double SurfaceHeight = 0.0; // m along the parent's Z, on its side
		std::string Node;           // node of the part; empty: its side (its -X face)
		double Roll = 0.0;          // rad around the attachment axis
	};

	// A part in a vessel design, placed in the frame of the root part (the vessel frame).
	struct PartInstance
	{
		int Part = -1;   // index into the catalog
		int Parent = -1; // index in the design; -1 for the root
		Attachment Attach;
		int Stage = -1;  // firing order of its engine or decoupler (0 fires first); -1 none
		int FlowPriority = 0; // tanks with a higher priority are drained first
		int SymmetryGroup = -1; // parts placed together by symmetry share a group

		// Set by the design from the attachments.
		Vec3 Position;
		Mat3 Rotation;
		Vec3 JointPosition; // where it meets its parent, in the vessel frame
		Vec3 JointAxis;     // from the parent towards the part, in the vessel frame
	};

	// A node of a part in the design that nothing is attached to.
	struct FreeNode
	{
		int Instance = -1;
		std::string Node;
		Vec3 Position;  // vessel frame
		Vec3 Direction; // vessel frame
	};

	// A vessel as the builder makes it and as data describes it (spec 5.1): a tree of parts,
	// root first and parents always before their children, with symmetry groups and stages.
	class VesselDesign
	{
	public:
		std::string Name;

		const std::vector<PartInstance>& Parts() const { return Instances; }
		bool IsEmpty() const { return Instances.empty(); }

		// Starts the design over with a single root part.
		PERIAPSISCORE_API void SetRoot(const PartCatalog& Catalog, int Part);

		// Attaches a part to Parent and returns the new instances. When the parent belongs to a
		// symmetry group, a copy goes on every member of the group. Otherwise, a side attachment
		// with Symmetry > 1 places that many copies evenly around the parent's axis. Returns
		// nothing when the attachment is not possible (unknown or taken nodes).
		PERIAPSISCORE_API std::vector<int> Attach(const PartCatalog& Catalog, int Parent, const Attachment& How, int Part,
			int Symmetry = 1);

		// Removes a part with everything attached below it, and its symmetry siblings.
		PERIAPSISCORE_API void Remove(int Instance);

		// Moves an instance (and its symmetry group) to another stage.
		PERIAPSISCORE_API void SetStage(int Instance, int Stage);
		PERIAPSISCORE_API void SetFlowPriority(int Instance, int Priority);
		// Puts an instance in the symmetry group of another (a new group if it has none), for
		// designs saved with their copies listed one by one.
		PERIAPSISCORE_API void JoinSymmetryGroup(int Instance, int Member);

		// Default staging: each decoupler level fires in turn from the bottom up (spec 5.5). An
		// engine ignites with the decouplers that uncover it, the first engines at launch.
		PERIAPSISCORE_API void AutoStage(const PartCatalog& Catalog);
		// Number of stages: one past the highest stage in use.
		PERIAPSISCORE_API int StageCount() const;
		// Renumbers the stages in use so there are no gaps, keeping their order.
		PERIAPSISCORE_API void CompactStages();

		PERIAPSISCORE_API std::vector<FreeNode> FreeNodes(const PartCatalog& Catalog) const;
		// Problems that make the design unusable, one line each.
		PERIAPSISCORE_API std::vector<std::string> Validate(const PartCatalog& Catalog) const;

		// Where a part goes for an attachment, relative to its parent: position and rotation
		// in the parent's frame. False when a node does not exist.
		PERIAPSISCORE_API static bool Place(const PartDefinition& ParentPart, const PartDefinition& ChildPart, const Attachment& How,
			Vec3& OutPosition, Mat3& OutRotation, Vec3& OutJoint, Vec3& OutAxis);

	private:
		bool PlaceInstance(const PartCatalog& Catalog, PartInstance& Instance) const;
		bool NodeTaken(int Instance, const std::string& Node) const;
		int NextSymmetryGroup() const;

		std::vector<PartInstance> Instances;
	};
}
