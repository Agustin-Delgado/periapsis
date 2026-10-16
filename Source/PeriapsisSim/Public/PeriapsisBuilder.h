#pragma once

#include "CoreMinimal.h"
#include "PeriapsisCore/Staging.h"
#include "PeriapsisCore/VesselDesign.h"

#include <memory>
#include <vector>

// Somewhere a part can go: a free node of a part in the design, or the side of a round part.
struct FPeriapsisAttachTarget
{
	int32 Instance = INDEX_NONE;
	FString Node; // empty: the side of the part
	Periapsis::Core::Vec3 Position;  // vessel frame
	Periapsis::Core::Vec3 Direction; // outwards, vessel frame
};

// The vessel builder (spec 8.2), without any view: what the builder screen shows and edits.
// Parts are picked from the catalog and attached to a target (a free node, or the side of a
// round part at an angle and height), with radial symmetry; parts can be removed with what
// hangs below them, and moved between stages. Delta-v, thrust-to-weight, mass and cost per
// stage are recomputed on every change, in vacuum and at sea level.
class PERIAPSISSIM_API FPeriapsisBuilder
{
public:
	static constexpr int32 SymmetryChoices[] = {1, 2, 3, 4, 6, 8};
	static constexpr double SideTurnDegrees = 15.0;
	static constexpr double SideStepMetres = 0.25;

	FPeriapsisBuilder(std::shared_ptr<const Periapsis::Core::PartCatalog> InCatalog, Periapsis::Core::VesselDesign Start);

	const Periapsis::Core::PartCatalog& GetCatalog() const { return *Catalog; }
	const std::shared_ptr<const Periapsis::Core::PartCatalog>& GetCatalogPtr() const { return Catalog; }
	const Periapsis::Core::VesselDesign& GetDesign() const { return Design; }

	// The part to attach, from the catalog.
	int32 GetSelectedPart() const { return SelectedPart; }
	void SelectPart(int32 Delta);
	void SelectPartById(const FString& Id);

	TArray<FPeriapsisAttachTarget> GetTargets() const;
	int32 GetTargetIndex() const { return TargetIndex; }
	// The current target, or one with no instance when the design is empty.
	FPeriapsisAttachTarget GetTarget() const;
	void CycleTarget(int32 Delta);
	// Where on a side the next part goes.
	void TurnSide(double DeltaDegrees);
	void MoveSide(double DeltaMetres);
	double GetSideAngle() const { return SideAngleDegrees; }
	double GetSideHeight() const { return SideHeight; }
	int32 GetSymmetry() const { return SymmetryChoices[SymmetryIndex]; }
	void CycleSymmetry();

	// The parts that Attach() would add now, placed (empty when it is not possible).
	std::vector<Periapsis::Core::PartInstance> Preview() const;
	// Attaches the selected part to the target (or starts the design with it). Stages follow
	// AutoStage until a stage is set by hand (or the design came with stages). False, with the
	// message saying why, when it cannot go.
	bool Attach();
	// Removes the target's part, its symmetry copies and what hangs below them.
	bool RemoveTarget();
	// Moves the target's part (engine or decoupler) to another stage.
	bool ShiftTargetStage(int32 Delta);
	// Restages everything bottom first, and keeps doing it as parts are added.
	void AutoStage();
	bool HasManualStages() const { return bManualStages; }
	void SetName(const FString& Name);
	void Load(Periapsis::Core::VesselDesign NewDesign);
	bool Undo();

	// Every stage at full throttle (spec 4.5), in vacuum or at sea level, against Gravity.
	std::vector<Periapsis::Core::StageStats> Stats(double PressurePa, double Gravity) const;
	// The vessel as it would stand on the pad, full.
	Periapsis::Core::Vessel MakeVessel() const;
	// Problems that make the design unusable (no command part, invalid attachments).
	TArray<FString> Problems() const;

	const FString& GetMessage() const { return Message; }

private:
	bool AttachmentFor(const FPeriapsisAttachTarget& Target, Periapsis::Core::Attachment& Out) const;
	void Changed();
	void Remember();

	std::shared_ptr<const Periapsis::Core::PartCatalog> Catalog;
	Periapsis::Core::VesselDesign Design;
	TArray<Periapsis::Core::VesselDesign> History;
	int32 SelectedPart = 0;
	int32 TargetIndex = 0;
	double SideAngleDegrees = 0.0;
	double SideHeight = 0.0;
	int32 SymmetryIndex = 0;
	bool bManualStages = false;
	FString Message;
};
