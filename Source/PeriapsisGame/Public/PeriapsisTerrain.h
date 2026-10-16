#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "GameFramework/Actor.h"
#include "PeriapsisCore/TerrainMesh.h"
#include "PeriapsisTerrain.generated.h"

class FPeriapsisUniverse;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;
struct FPeriapsisViewTransform;

// The terrain of every body with relief (spec 6.3): a cube-sphere quadtree per body, with the
// level of detail picked each frame so that no change of detail moves the ground on screen by
// more than a pixel or two (Periapsis::Core::SelectTerrainNodes). Chunks are generated on
// worker threads and kept in a cache; until a finer chunk is ready its parent is drawn. Only
// for drawing: collision comes from the physics bubble's ground patch.
UCLASS()
class PERIAPSISGAME_API APeriapsisTerrain : public AActor
{
	GENERATED_BODY()

public:
	APeriapsisTerrain();

	// Selects, generates and places the chunks for this frame. PixelsPerRadian is the camera's
	// resolution along the screen width.
	void Update(const FPeriapsisUniverse& Universe, const FPeriapsisViewTransform& View, const FVector& CameraLocation, double PixelsPerRadian);

	bool HasTerrain(int32 Body) const;
	// Statistics for the HUD and the tests.
	int32 NumDrawnChunks() const { return DrawnChunks; }
	int32 NumPendingChunks() const;
	int32 DeepestDrawnLevel() const { return DeepestLevel; }

	static constexpr int32 Resolution = 33;
	static constexpr int32 MaxChunksInFlight = 24;
	static constexpr int32 MaxCachedChunks = 4000;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FChunk
	{
		Periapsis::Core::TerrainNode Node;
		Periapsis::Core::TerrainChunkMesh Mesh;
		TObjectPtr<UProceduralMeshComponent> Component = nullptr;
		uint64 LastUsedFrame = 0;
	};

	struct FBodyTerrain
	{
		int32 BodyIndex = 0;
		TSharedPtr<const Periapsis::Core::TerrainShape> Shape;
		double Radius = 0.0;
		TMap<uint64, TSharedPtr<FChunk>> Chunks; // generated
		TSet<uint64> InFlight;
		TObjectPtr<UMaterialInstanceDynamic> Material = nullptr;
	};

	struct FFinished
	{
		int32 Body = 0;
		Periapsis::Core::TerrainNode Node;
		TSharedPtr<Periapsis::Core::TerrainChunkMesh> Mesh;
	};

	void Rebuild(const FPeriapsisUniverse& Universe);
	void ShowChunk(FBodyTerrain& Body, FChunk& Chunk);
	void Evict(FBodyTerrain& Body);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	// Keeps the components alive for the garbage collector.
	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> AllComponents;

	TArray<FBodyTerrain> Bodies;
	const FPeriapsisUniverse* BuiltFor = nullptr;
	// Chunks generated on worker threads; each task holds the queue, so it outlives this actor.
	TSharedPtr<TQueue<FFinished, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Finished;
	uint64 FrameCounter = 0;
	int32 DrawnChunks = 0;
	int32 DeepestLevel = 0;
	TSet<UProceduralMeshComponent*> VisibleLastFrame;
};
