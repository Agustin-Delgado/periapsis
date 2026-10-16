#include "PeriapsisTerrain.h"

#include "Materials/MaterialInstanceDynamic.h"
#include "PeriapsisBubble.h"
#include "PeriapsisCore/Surface.h"
#include "PeriapsisUniverse.h"
#include "ProceduralMeshComponent.h"
#include "Tasks/Task.h"
#include "UObject/ConstructorHelpers.h"
#include "UnitBridge.h"

using namespace Periapsis::Core;

APeriapsisTerrain::APeriapsisTerrain()
{
	PrimaryActorTick.bCanEverTick = false; // the universe view drives it, once its frame is known
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	BaseMaterial = Material.Object;
	Finished = MakeShared<TQueue<FFinished, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();
}

void APeriapsisTerrain::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Tasks still running keep the queue alive through their own reference.
	Finished = MakeShared<TQueue<FFinished, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();
	Bodies.Reset();
	Super::EndPlay(EndPlayReason);
}

bool APeriapsisTerrain::HasTerrain(int32 Body) const
{
	return Bodies.ContainsByPredicate([Body](const FBodyTerrain& B) { return B.BodyIndex == Body; });
}

int32 APeriapsisTerrain::NumPendingChunks() const
{
	int32 Count = 0;
	for (const FBodyTerrain& Body : Bodies)
	{
		Count += Body.InFlight.Num();
	}
	return Count;
}

void APeriapsisTerrain::Rebuild(const FPeriapsisUniverse& Universe)
{
	for (UProceduralMeshComponent* Component : AllComponents)
	{
		if (Component)
		{
			Component->DestroyComponent();
		}
	}
	AllComponents.Reset();
	VisibleLastFrame.Reset();
	Bodies.Reset();
	Finished = MakeShared<TQueue<FFinished, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();

	const CelestialSystem& System = Universe.GetSystem();
	for (int32 Index = 0; Index < System.BodyCount(); ++Index)
	{
		const CelestialBody& Body = System.Body(Index);
		if (Body.Terrain.IsFlat())
		{
			continue;
		}
		FBodyTerrain& Terrain = Bodies.AddDefaulted_GetRef();
		Terrain.BodyIndex = Index;
		Terrain.Shape = MakeShared<const TerrainShape>(Body.Terrain); // the tasks keep their own copy alive
		Terrain.Radius = Body.Radius;
		if (BaseMaterial)
		{
			Terrain.Material = UMaterialInstanceDynamic::Create(BaseMaterial, this);
			Terrain.Material->SetVectorParameterValue(TEXT("Color"), Universe.GetBodyInfo(Index).Color);
		}
	}
	BuiltFor = &Universe;
}

void APeriapsisTerrain::ShowChunk(FBodyTerrain& Body, FChunk& Chunk)
{
	if (Chunk.Component)
	{
		return;
	}
	UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(this);
	Component->SetupAttachment(RootComponent);
	Component->SetUsingAbsoluteLocation(true);
	Component->SetUsingAbsoluteRotation(true);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCastShadow(true);

	// Body-fixed metres to Unreal centimetres, mirrored like every vector (FUnitBridge). The
	// mirror also turns the Core's counter-clockwise triangles into Unreal's clockwise front faces.
	const TerrainChunkMesh& Mesh = Chunk.Mesh;
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	Vertices.Reserve(static_cast<int32>(Mesh.Positions.size()));
	Normals.Reserve(static_cast<int32>(Mesh.Normals.size()));
	for (size_t Index = 0; Index < Mesh.Positions.size(); ++Index)
	{
		Vertices.Add(FUnitBridge::ToUnreal(Mesh.Positions[Index]));
		Normals.Add(FUnitBridge::DirectionToUnreal(Mesh.Normals[Index]));
	}
	const TArray<int32> Triangles(Mesh.Triangles.data(), static_cast<int32>(Mesh.Triangles.size()));
	Component->CreateMeshSection(0, Vertices, Triangles, Normals, {}, {}, {}, false);
	if (Body.Material)
	{
		Component->SetMaterial(0, Body.Material);
	}
	Component->RegisterComponent();
	AllComponents.Add(Component);
	Chunk.Component = Component;
}

void APeriapsisTerrain::Evict(FBodyTerrain& Body)
{
	if (Body.Chunks.Num() <= MaxCachedChunks)
	{
		return;
	}
	TArray<TPair<uint64, uint64>> ByAge; // last used frame, key
	for (const TPair<uint64, TSharedPtr<FChunk>>& Pair : Body.Chunks)
	{
		if (Pair.Value->LastUsedFrame < FrameCounter && Pair.Value->Node.Level > 0)
		{
			ByAge.Emplace(Pair.Value->LastUsedFrame, Pair.Key);
		}
	}
	ByAge.Sort([](const TPair<uint64, uint64>& A, const TPair<uint64, uint64>& B) { return A.Key < B.Key; });
	const int32 ToRemove = FMath::Min(ByAge.Num(), Body.Chunks.Num() - MaxCachedChunks);
	for (int32 Index = 0; Index < ToRemove; ++Index)
	{
		const TSharedPtr<FChunk> Chunk = Body.Chunks.FindAndRemoveChecked(ByAge[Index].Value);
		if (Chunk->Component)
		{
			AllComponents.Remove(Chunk->Component);
			VisibleLastFrame.Remove(Chunk->Component);
			Chunk->Component->DestroyComponent();
		}
	}
}

void APeriapsisTerrain::Update(const FPeriapsisUniverse& Universe, const FPeriapsisViewTransform& View, const FVector& CameraLocation,
	double PixelsPerRadian)
{
	++FrameCounter;
	if (BuiltFor != &Universe)
	{
		Rebuild(Universe);
	}

	FFinished Done;
	while (Finished->Dequeue(Done))
	{
		for (FBodyTerrain& Body : Bodies)
		{
			if (Body.BodyIndex == Done.Body)
			{
				Body.InFlight.Remove(Done.Node.Key());
				const TSharedPtr<FChunk> Chunk = MakeShared<FChunk>();
				Chunk->Node = Done.Node;
				Chunk->Mesh = MoveTemp(*Done.Mesh);
				Body.Chunks.Add(Done.Node.Key(), Chunk);
			}
		}
	}

	TerrainLodSettings Settings;
	Settings.Resolution = Resolution;
	Settings.PixelsPerRadian = PixelsPerRadian;
	const CelestialSystem& System = Universe.GetSystem();
	const Vec3 CameraRoot = View.ToRoot(CameraLocation);
	int32 InFlight = NumPendingChunks();
	TSet<UProceduralMeshComponent*> Visible;
	DrawnChunks = 0;
	DeepestLevel = 0;

	for (FBodyTerrain& Body : Bodies)
	{
		const CelestialBody& Celestial = System.Body(Body.BodyIndex);
		const Vec3 BodyRoot = Universe.BodyPositionAt(Body.BodyIndex, View.Time);
		const Vec3 Camera = ToBodyFixed(Celestial, CameraRoot - BodyRoot, View.Time);

		TArray<TerrainNode> Wanted;
		const std::vector<TerrainNode> Selected = SelectTerrainNodes(Body.Radius, Camera, Settings,
			[&](const TerrainNode& Node, TerrainNodeBounds& Out)
			{
				const TSharedPtr<FChunk>* Found = Body.Chunks.Find(Node.Key());
				if (!Found)
				{
					return false;
				}
				(*Found)->LastUsedFrame = FrameCounter;
				Out = {(*Found)->Mesh.Center, (*Found)->Mesh.BoundingRadius, (*Found)->Mesh.GeometricError};
				return true;
			},
			[&](const TerrainNode& Node) { Wanted.Add(Node); });

		// Coarse chunks first: they fill the most screen and unlock the finer ones.
		Wanted.Sort([](const TerrainNode& A, const TerrainNode& B) { return A.Level < B.Level; });
		for (const TerrainNode& Node : Wanted)
		{
			if (InFlight >= MaxChunksInFlight)
			{
				break;
			}
			if (Body.InFlight.Contains(Node.Key()))
			{
				continue;
			}
			Body.InFlight.Add(Node.Key());
			++InFlight;
			UE::Tasks::Launch(UE_SOURCE_LOCATION,
				[Queue = Finished, Shape = Body.Shape, Radius = Body.Radius, Node, BodyIndex = Body.BodyIndex]()
				{
					TSharedPtr<TerrainChunkMesh> Mesh = MakeShared<TerrainChunkMesh>(BuildTerrainChunk(*Shape, Radius, Node, Resolution));
					Queue->Enqueue({BodyIndex, Node, Mesh});
				});
		}

		const FQuat Rotation = View.BodyFixedToWorld(Celestial);
		for (const TerrainNode& Node : Selected)
		{
			FChunk& Chunk = *Body.Chunks.FindChecked(Node.Key());
			ShowChunk(Body, Chunk);
			const Vec3 Center = BodyRoot + FromBodyFixed(Celestial, Chunk.Mesh.Center, View.Time);
			Chunk.Component->SetWorldLocationAndRotation(View.ToWorld(Center), Rotation);
			Chunk.Component->SetVisibility(true);
			Visible.Add(Chunk.Component);
			++DrawnChunks;
			DeepestLevel = FMath::Max(DeepestLevel, Node.Level);
		}
		Evict(Body);
	}

	for (UProceduralMeshComponent* Component : VisibleLastFrame)
	{
		if (!Visible.Contains(Component))
		{
			Component->SetVisibility(false);
		}
	}
	VisibleLastFrame = MoveTemp(Visible);
}
