#include "PeriapsisCore/VesselDesign.h"

#include "PeriapsisCore/Constants.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace Periapsis::Core
{
	namespace
	{
		const Vec3 AxisZ{0.0, 0.0, 1.0};
		const Vec3 AxisX{1.0, 0.0, 0.0};

		// A direction that fixes the roll around an attachment axis: the part's own axis when
		// the attachment is on its side, its X axis when the attachment is along its axis.
		Vec3 RollReference(const Vec3& Direction)
		{
			return NormSquared(Cross(Direction, AxisZ)) > 1e-6 ? AxisZ : AxisX;
		}

		// The attachment point and outward direction of a node, or of the side of a part.
		bool AttachPoint(const PartDefinition& Part, const std::string& Node, double Angle, double Height, Vec3& OutPoint, Vec3& OutDirection)
		{
			if (Node.empty())
			{
				OutDirection = {std::cos(Angle), std::sin(Angle), 0.0};
				OutPoint = OutDirection * Part.Geometry.RadiusAt(Height) + AxisZ * Height;
				return Part.bSurfaceAttach;
			}
			const AttachNode* Found = Part.FindNode(Node);
			if (!Found)
			{
				return false;
			}
			OutPoint = Found->Position;
			OutDirection = Found->Direction;
			return true;
		}
	}

	bool VesselDesign::Place(const PartDefinition& ParentPart, const PartDefinition& ChildPart, const Attachment& How, Vec3& OutPosition,
		Mat3& OutRotation, Vec3& OutJoint, Vec3& OutAxis)
	{
		Vec3 ParentPoint, ParentDirection, ChildPoint, ChildDirection;
		if (!AttachPoint(ParentPart, How.ParentNode, How.SurfaceAngle, How.SurfaceHeight, ParentPoint, ParentDirection))
		{
			return false;
		}
		// A part on something's side touches it with its own side facing back (-X).
		if (!AttachPoint(ChildPart, How.Node, kPi, 0.0, ChildPoint, ChildDirection))
		{
			return false;
		}
		// The child's node faces the parent's: its direction turns into the opposite of the
		// parent's, and the roll references line up, then turn by Roll around the axis.
		const Vec3 Target = -ParentDirection;
		const Mat3 RollTurn = Mat3::Rotation(Normalized(ParentDirection), How.Roll);
		const Mat3 ChildBasis = Mat3::FromZX(ChildDirection, RollReference(ChildDirection));
		const Mat3 ParentBasis = Mat3::FromZX(Target, RollTurn * RollReference(Target));
		OutRotation = ParentBasis * ChildBasis.Transposed();
		OutPosition = ParentPoint - OutRotation * ChildPoint;
		OutJoint = ParentPoint;
		OutAxis = Normalized(ParentDirection);
		return true;
	}

	bool VesselDesign::PlaceInstance(const PartCatalog& Catalog, PartInstance& Instance) const
	{
		if (Instance.Parent < 0)
		{
			Instance.Position = {};
			Instance.Rotation = Mat3::Identity();
			Instance.JointPosition = {};
			Instance.JointAxis = AxisZ;
			return true;
		}
		const PartInstance& Parent = Instances[Instance.Parent];
		Vec3 Position, Joint, Axis;
		Mat3 Rotation;
		if (!Place(Catalog.Parts[Parent.Part], Catalog.Parts[Instance.Part], Instance.Attach, Position, Rotation, Joint, Axis))
		{
			return false;
		}
		Instance.Position = Parent.Position + Parent.Rotation * Position;
		Instance.Rotation = Parent.Rotation * Rotation;
		Instance.JointPosition = Parent.Position + Parent.Rotation * Joint;
		Instance.JointAxis = Parent.Rotation * Axis;
		return true;
	}

	void VesselDesign::SetRoot(const PartCatalog& Catalog, int Part)
	{
		Instances.clear();
		PartInstance Root;
		Root.Part = Part;
		PlaceInstance(Catalog, Root);
		Instances.push_back(Root);
	}

	bool VesselDesign::NodeTaken(int Instance, const std::string& Node) const
	{
		if (Node.empty())
		{
			return false; // a side takes any number of parts
		}
		const PartInstance& Self = Instances[Instance];
		if (Self.Parent >= 0 && Self.Attach.Node == Node)
		{
			return true;
		}
		return std::any_of(Instances.begin(), Instances.end(),
			[&](const PartInstance& Other) { return Other.Parent == Instance && Other.Attach.ParentNode == Node; });
	}

	int VesselDesign::NextSymmetryGroup() const
	{
		int Next = 0;
		for (const PartInstance& Instance : Instances)
		{
			Next = std::max(Next, Instance.SymmetryGroup + 1);
		}
		return Next;
	}

	std::vector<int> VesselDesign::Attach(const PartCatalog& Catalog, int Parent, const Attachment& How, int Part, int Symmetry)
	{
		if (Parent < 0 || Parent >= static_cast<int>(Instances.size()) || Part < 0 || Part >= static_cast<int>(Catalog.Parts.size()))
		{
			return {};
		}
		// One attachment per parent: the members of the parent's group, or copies around it.
		std::vector<std::pair<int, Attachment>> Placements;
		const int ParentGroup = Instances[Parent].SymmetryGroup;
		if (ParentGroup >= 0)
		{
			for (int Index = 0; Index < static_cast<int>(Instances.size()); ++Index)
			{
				if (Instances[Index].SymmetryGroup == ParentGroup)
				{
					Placements.push_back({Index, How});
				}
			}
		}
		else if (Symmetry > 1 && How.ParentNode.empty())
		{
			for (int Copy = 0; Copy < Symmetry; ++Copy)
			{
				Attachment Turned = How;
				Turned.SurfaceAngle = How.SurfaceAngle + kTwoPi * Copy / Symmetry;
				Placements.push_back({Parent, Turned});
			}
		}
		else
		{
			Placements.push_back({Parent, How});
		}

		std::vector<PartInstance> Added;
		for (const auto& [ParentIndex, Attach] : Placements)
		{
			if (NodeTaken(ParentIndex, Attach.ParentNode))
			{
				return {};
			}
			PartInstance Instance;
			Instance.Part = Part;
			Instance.Parent = ParentIndex;
			Instance.Attach = Attach;
			if (!PlaceInstance(Catalog, Instance))
			{
				return {};
			}
			Added.push_back(Instance);
		}
		const int Group = Added.size() > 1 ? NextSymmetryGroup() : -1;
		std::vector<int> Result;
		for (PartInstance& Instance : Added)
		{
			Instance.SymmetryGroup = Group;
			Result.push_back(static_cast<int>(Instances.size()));
			Instances.push_back(Instance);
		}
		return Result;
	}

	void VesselDesign::Remove(int Instance)
	{
		if (Instance <= 0 || Instance >= static_cast<int>(Instances.size()))
		{
			return; // the root stays: SetRoot starts over
		}
		std::vector<bool> Removed(Instances.size(), false);
		const int Group = Instances[Instance].SymmetryGroup;
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			const PartInstance& Candidate = Instances[Index];
			const bool bSelf = static_cast<int>(Index) == Instance || (Group >= 0 && Candidate.SymmetryGroup == Group);
			// Parents come first, so a removed parent is already marked.
			Removed[Index] = bSelf || (Candidate.Parent >= 0 && Removed[Candidate.Parent]);
		}
		std::vector<int> NewIndex(Instances.size(), -1);
		std::vector<PartInstance> Kept;
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			if (!Removed[Index])
			{
				NewIndex[Index] = static_cast<int>(Kept.size());
				PartInstance Copy = Instances[Index];
				Copy.Parent = Copy.Parent >= 0 ? NewIndex[Copy.Parent] : -1;
				Kept.push_back(Copy);
			}
		}
		Instances = std::move(Kept);
	}

	void VesselDesign::SetStage(int Instance, int Stage)
	{
		if (Instance < 0 || Instance >= static_cast<int>(Instances.size()))
		{
			return;
		}
		const int Group = Instances[Instance].SymmetryGroup;
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			if (static_cast<int>(Index) == Instance || (Group >= 0 && Instances[Index].SymmetryGroup == Group))
			{
				Instances[Index].Stage = std::max(Stage, -1);
			}
		}
	}

	void VesselDesign::SetFlowPriority(int Instance, int Priority)
	{
		if (Instance < 0 || Instance >= static_cast<int>(Instances.size()))
		{
			return;
		}
		const int Group = Instances[Instance].SymmetryGroup;
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			if (static_cast<int>(Index) == Instance || (Group >= 0 && Instances[Index].SymmetryGroup == Group))
			{
				Instances[Index].FlowPriority = Priority;
			}
		}
	}

	void VesselDesign::JoinSymmetryGroup(int Instance, int Member)
	{
		const int Count = static_cast<int>(Instances.size());
		if (Instance < 0 || Instance >= Count || Member < 0 || Member >= Count || Instance == Member)
		{
			return;
		}
		if (Instances[Member].SymmetryGroup < 0)
		{
			Instances[Member].SymmetryGroup = NextSymmetryGroup();
		}
		Instances[Instance].SymmetryGroup = Instances[Member].SymmetryGroup;
	}

	void VesselDesign::AutoStage(const PartCatalog& Catalog)
	{
		// Level: how many decouplers are on the way from the root to the part, itself included.
		std::vector<int> Level(Instances.size(), 0);
		int Deepest = 0;
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			const PartInstance& Instance = Instances[Index];
			Level[Index] = (Instance.Parent >= 0 ? Level[Instance.Parent] : 0) + (Catalog.Parts[Instance.Part].Decoupler ? 1 : 0);
			Deepest = std::max(Deepest, Level[Index]);
		}
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			const PartDefinition& Part = Catalog.Parts[Instances[Index].Part];
			if (Part.Decoupler)
			{
				Instances[Index].Stage = Deepest - Level[Index] + 1;
			}
			else if (Part.Engine)
			{
				Instances[Index].Stage = Deepest - Level[Index];
			}
			else
			{
				Instances[Index].Stage = -1;
			}
		}
		CompactStages();
	}

	int VesselDesign::StageCount() const
	{
		int Count = 0;
		for (const PartInstance& Instance : Instances)
		{
			Count = std::max(Count, Instance.Stage + 1);
		}
		return Count;
	}

	void VesselDesign::CompactStages()
	{
		std::set<int> Used;
		for (const PartInstance& Instance : Instances)
		{
			if (Instance.Stage >= 0)
			{
				Used.insert(Instance.Stage);
			}
		}
		std::map<int, int> Renumber;
		for (const int Stage : Used)
		{
			const int Next = static_cast<int>(Renumber.size());
			Renumber[Stage] = Next;
		}
		for (PartInstance& Instance : Instances)
		{
			if (Instance.Stage >= 0)
			{
				Instance.Stage = Renumber[Instance.Stage];
			}
		}
	}

	std::vector<FreeNode> VesselDesign::FreeNodes(const PartCatalog& Catalog) const
	{
		std::vector<FreeNode> Result;
		for (int Index = 0; Index < static_cast<int>(Instances.size()); ++Index)
		{
			const PartInstance& Instance = Instances[Index];
			for (const AttachNode& Node : Catalog.Parts[Instance.Part].Nodes)
			{
				if (!NodeTaken(Index, Node.Id))
				{
					Result.push_back({Index, Node.Id, Instance.Position + Instance.Rotation * Node.Position, Instance.Rotation * Node.Direction});
				}
			}
		}
		return Result;
	}

	std::vector<std::string> VesselDesign::Validate(const PartCatalog& Catalog) const
	{
		std::vector<std::string> Errors;
		if (Instances.empty())
		{
			Errors.push_back("the design has no parts");
			return Errors;
		}
		std::set<std::pair<int, std::string>> UsedNodes;
		for (size_t Index = 0; Index < Instances.size(); ++Index)
		{
			const PartInstance& Instance = Instances[Index];
			const std::string Where = "part " + std::to_string(Index);
			if (Instance.Part < 0 || Instance.Part >= static_cast<int>(Catalog.Parts.size()))
			{
				Errors.push_back(Where + ": unknown part");
				continue;
			}
			const PartDefinition& Part = Catalog.Parts[Instance.Part];
			if ((Index == 0) != (Instance.Parent < 0) || Instance.Parent >= static_cast<int>(Index))
			{
				Errors.push_back(Where + " (" + Part.Id + "): only the first part is the root, and parents come before children");
				continue;
			}
			if (Instance.Stage >= 0 && !Part.HasStageAction())
			{
				Errors.push_back(Where + " (" + Part.Id + "): only engines and decouplers have a stage");
			}
			if (Instance.Parent < 0)
			{
				continue;
			}
			const PartDefinition& ParentPart = Catalog.Parts[Instances[Instance.Parent].Part];
			Vec3 Position, Joint, Axis;
			Mat3 Rotation;
			if (!Place(ParentPart, Part, Instance.Attach, Position, Rotation, Joint, Axis))
			{
				Errors.push_back(Where + " (" + Part.Id + "): cannot attach to '" + ParentPart.Id + "' that way");
			}
			const std::pair<int, std::string> ParentNode{Instance.Parent, Instance.Attach.ParentNode};
			const std::pair<int, std::string> OwnNode{static_cast<int>(Index), Instance.Attach.Node};
			if (!ParentNode.second.empty() && !UsedNodes.insert(ParentNode).second)
			{
				Errors.push_back(Where + " (" + Part.Id + "): node '" + ParentNode.second + "' of its parent is already taken");
			}
			if (!OwnNode.second.empty() && !UsedNodes.insert(OwnNode).second)
			{
				Errors.push_back(Where + " (" + Part.Id + "): its node '" + OwnNode.second + "' is already taken");
			}
		}
		return Errors;
	}
}
