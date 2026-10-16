#include "PeriapsisBuilder.h"

#include "PeriapsisCore/Constants.h"

using namespace Periapsis::Core;

namespace
{
	constexpr int32 MaxHistory = 64;
}

FPeriapsisBuilder::FPeriapsisBuilder(std::shared_ptr<const PartCatalog> InCatalog, VesselDesign Start)
	: Catalog(MoveTemp(InCatalog))
	, Design(MoveTemp(Start))
{
	if (Design.Name.empty())
	{
		Design.Name = "Untitled";
	}
	// A design that comes with stages keeps them; new parts then need staging by hand.
	bManualStages = Design.StageCount() > 0;
	Changed();
}

void FPeriapsisBuilder::SelectPart(int32 Delta)
{
	const int32 Count = static_cast<int32>(Catalog->Parts.size());
	if (Count > 0)
	{
		SelectedPart = ((SelectedPart + Delta) % Count + Count) % Count;
	}
}

void FPeriapsisBuilder::SelectPartById(const FString& Id)
{
	const int Found = Catalog->FindPart(TCHAR_TO_UTF8(*Id));
	if (Found >= 0)
	{
		SelectedPart = Found;
	}
}

TArray<FPeriapsisAttachTarget> FPeriapsisBuilder::GetTargets() const
{
	TArray<FPeriapsisAttachTarget> Targets;
	for (const FreeNode& Node : Design.FreeNodes(*Catalog))
	{
		Targets.Add({Node.Instance, UTF8_TO_TCHAR(Node.Node.c_str()), Node.Position, Node.Direction});
	}
	for (int32 Index = 0; Index < static_cast<int32>(Design.Parts().size()); ++Index)
	{
		const PartInstance& Instance = Design.Parts()[Index];
		const PartDefinition& Part = Catalog->Parts[Instance.Part];
		if (!Part.bSurfaceAttach)
		{
			continue;
		}
		const double Angle = DegToRad(SideAngleDegrees);
		const double Height = FMath::Clamp(SideHeight, -0.5 * Part.Geometry.Length, 0.5 * Part.Geometry.Length);
		const Vec3 Out{FMath::Cos(Angle), FMath::Sin(Angle), 0.0};
		const Vec3 Point = Out * Part.Geometry.RadiusAt(Height) + Vec3{0.0, 0.0, Height};
		Targets.Add({Index, FString(), Instance.Position + Instance.Rotation * Point, Instance.Rotation * Out});
	}
	return Targets;
}

FPeriapsisAttachTarget FPeriapsisBuilder::GetTarget() const
{
	const TArray<FPeriapsisAttachTarget> Targets = GetTargets();
	return Targets.IsValidIndex(TargetIndex) ? Targets[TargetIndex] : FPeriapsisAttachTarget();
}

void FPeriapsisBuilder::CycleTarget(int32 Delta)
{
	const int32 Count = GetTargets().Num();
	TargetIndex = Count > 0 ? ((TargetIndex + Delta) % Count + Count) % Count : 0;
}

void FPeriapsisBuilder::TurnSide(double DeltaDegrees)
{
	SideAngleDegrees = FMath::Fmod(SideAngleDegrees + DeltaDegrees + 360.0, 360.0);
}

void FPeriapsisBuilder::MoveSide(double DeltaMetres)
{
	SideHeight += DeltaMetres;
	const FPeriapsisAttachTarget Target = GetTarget();
	if (Target.Instance != INDEX_NONE)
	{
		const double Half = 0.5 * Catalog->Parts[Design.Parts()[Target.Instance].Part].Geometry.Length;
		SideHeight = FMath::Clamp(SideHeight, -Half, Half);
	}
}

void FPeriapsisBuilder::CycleSymmetry()
{
	SymmetryIndex = (SymmetryIndex + 1) % UE_ARRAY_COUNT(SymmetryChoices);
}

bool FPeriapsisBuilder::AttachmentFor(const FPeriapsisAttachTarget& Target, Attachment& Out) const
{
	if (Target.Instance == INDEX_NONE)
	{
		return false;
	}
	const PartDefinition& Child = Catalog->Parts[SelectedPart];
	Out = Attachment();
	if (Target.Node.IsEmpty())
	{
		Out.SurfaceAngle = DegToRad(SideAngleDegrees);
		const double Half = 0.5 * Catalog->Parts[Design.Parts()[Target.Instance].Part].Geometry.Length;
		Out.SurfaceHeight = FMath::Clamp(SideHeight, -Half, Half);
	}
	else
	{
		Out.ParentNode = TCHAR_TO_UTF8(*Target.Node);
	}

	// The new part keeps the vessel's up as far as it can: on a sideways target it touches with
	// its side (or a sideways node), on an end it uses the node that faces back.
	const bool bSideways = FMath::Abs(Target.Direction.Z) < 0.5;
	if (bSideways && Child.bSurfaceAttach)
	{
		return true; // its own side
	}
	const AttachNode* Best = nullptr;
	double BestScore = -2.0;
	for (const AttachNode& Node : Child.Nodes)
	{
		const double Score = bSideways ? (FMath::Abs(Node.Direction.Z) < 0.5 ? 1.0 - Node.Direction.X : -1.0)
									   : Dot(Node.Direction, -Target.Direction);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = &Node;
		}
	}
	if (!Best)
	{
		return false;
	}
	Out.Node = Best->Id;
	return true;
}

std::vector<PartInstance> FPeriapsisBuilder::Preview() const
{
	if (Design.IsEmpty())
	{
		VesselDesign Alone;
		Alone.SetRoot(*Catalog, SelectedPart);
		return Alone.Parts();
	}
	Attachment How;
	if (!AttachmentFor(GetTarget(), How))
	{
		return {};
	}
	VesselDesign Trial = Design;
	std::vector<PartInstance> Added;
	for (const int Index : Trial.Attach(*Catalog, GetTarget().Instance, How, SelectedPart, GetSymmetry()))
	{
		Added.push_back(Trial.Parts()[Index]);
	}
	return Added;
}

bool FPeriapsisBuilder::Attach()
{
	const FString PartName = UTF8_TO_TCHAR(Catalog->Parts[SelectedPart].Name.c_str());
	if (Design.IsEmpty())
	{
		Remember();
		const std::string Name = Design.Name;
		Design.SetRoot(*Catalog, SelectedPart);
		Design.Name = Name;
		Message = FString::Printf(TEXT("Started with %s"), *PartName);
		Changed();
		return true;
	}
	Attachment How;
	const FPeriapsisAttachTarget Target = GetTarget();
	if (!AttachmentFor(Target, How))
	{
		Message = FString::Printf(TEXT("%s has no node that fits here"), *PartName);
		return false;
	}
	VesselDesign Before = Design;
	const std::vector<int> Added = Design.Attach(*Catalog, Target.Instance, How, SelectedPart, GetSymmetry());
	if (Added.empty())
	{
		Message = FString::Printf(TEXT("%s cannot go there"), *PartName);
		return false;
	}
	History.Add(MoveTemp(Before));
	if (History.Num() > MaxHistory)
	{
		History.RemoveAt(0);
	}
	if (!bManualStages)
	{
		Design.AutoStage(*Catalog);
	}
	Message = FString::Printf(TEXT("Attached %d x %s"), static_cast<int32>(Added.size()), *PartName);
	Changed();
	return true;
}

bool FPeriapsisBuilder::RemoveTarget()
{
	const FPeriapsisAttachTarget Target = GetTarget();
	if (Target.Instance == INDEX_NONE || Target.Instance == 0)
	{
		Message = TEXT("The root part stays (N starts a new design)");
		return false;
	}
	Remember();
	const FString PartName = UTF8_TO_TCHAR(Catalog->Parts[Design.Parts()[Target.Instance].Part].Name.c_str());
	Design.Remove(Target.Instance);
	Design.CompactStages();
	Message = FString::Printf(TEXT("Removed %s and what hung from it"), *PartName);
	Changed();
	return true;
}

bool FPeriapsisBuilder::ShiftTargetStage(int32 Delta)
{
	const FPeriapsisAttachTarget Target = GetTarget();
	if (Target.Instance == INDEX_NONE || !Catalog->Parts[Design.Parts()[Target.Instance].Part].HasStageAction())
	{
		Message = TEXT("Only engines and decouplers have a stage");
		return false;
	}
	Remember();
	const int32 Stage = FMath::Max(Design.Parts()[Target.Instance].Stage + Delta, 0);
	Design.SetStage(Target.Instance, Stage);
	Design.CompactStages();
	bManualStages = true;
	Message = FString::Printf(TEXT("Moved to stage %d"), Design.Parts()[Target.Instance].Stage);
	Changed();
	return true;
}

void FPeriapsisBuilder::AutoStage()
{
	Remember();
	Design.AutoStage(*Catalog);
	bManualStages = false;
	Message = TEXT("Staged automatically, bottom first");
}

void FPeriapsisBuilder::SetName(const FString& Name)
{
	Design.Name = TCHAR_TO_UTF8(*Name);
}

void FPeriapsisBuilder::Load(VesselDesign NewDesign)
{
	Remember();
	Design = MoveTemp(NewDesign);
	bManualStages = Design.StageCount() > 0;
	Message = FString::Printf(TEXT("Loaded %s"), UTF8_TO_TCHAR(Design.Name.c_str()));
	Changed();
}

bool FPeriapsisBuilder::Undo()
{
	if (History.IsEmpty())
	{
		Message = TEXT("Nothing to undo");
		return false;
	}
	Design = History.Pop();
	Message = TEXT("Undone");
	Changed();
	return true;
}

void FPeriapsisBuilder::Remember()
{
	History.Add(Design);
	if (History.Num() > MaxHistory)
	{
		History.RemoveAt(0);
	}
}

void FPeriapsisBuilder::Changed()
{
	const int32 Count = GetTargets().Num();
	TargetIndex = Count > 0 ? FMath::Clamp(TargetIndex, 0, Count - 1) : 0;
}

Vessel FPeriapsisBuilder::MakeVessel() const
{
	return Design.IsEmpty() ? Vessel() : Vessel(Catalog, Design);
}

std::vector<StageStats> FPeriapsisBuilder::Stats(double PressurePa, double Gravity) const
{
	return AnalyzeStages(MakeVessel(), PressurePa, Gravity);
}

TArray<FString> FPeriapsisBuilder::Problems() const
{
	TArray<FString> Result;
	for (const std::string& Error : Design.Validate(*Catalog))
	{
		Result.Add(UTF8_TO_TCHAR(Error.c_str()));
	}
	if (!Design.IsEmpty() && !Catalog->Parts[Design.Parts()[0].Part].bCommand)
	{
		Result.Add(TEXT("the root part cannot be controlled: start from a command pod"));
	}
	bool bEngine = false;
	for (const PartInstance& Instance : Design.Parts())
	{
		const PartDefinition& Part = Catalog->Parts[Instance.Part];
		bEngine |= Part.Engine.has_value();
		if (Part.Engine && Instance.Stage < 0)
		{
			Result.Add(FString::Printf(TEXT("%s is in no stage: it will never light"), UTF8_TO_TCHAR(Part.Name.c_str())));
			break;
		}
	}
	if (!Design.IsEmpty() && !bEngine)
	{
		Result.Add(TEXT("no engine"));
	}
	return Result;
}
