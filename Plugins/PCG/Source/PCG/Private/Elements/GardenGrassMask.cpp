#include "Elements/GardenGrassMask.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PCGPoint.h"
#include "Elements/PCGProjectionParams.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/ScopeExit.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "PCGComponent.h"
#include "PCGContext.h"
#include "PCGElement.h"
#include "PCGPin.h"
#include "Data/PCGBasePointData.h"
#include "Data/PCGSpatialData.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

UGardenGrassMaskComponent::UGardenGrassMaskComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.5f;
    bTickInEditor = true;
}

void UGardenGrassMaskComponent::OnRegister()
{
    Super::OnRegister();
    SetComponentTickEnabled(!bGrassBaked);
    if (bGrassBaked) return;
    BindGenerationComplete();
}

void UGardenGrassMaskComponent::BindGenerationComplete()
{
    if (bGrassBaked) return;
    UPCGComponent* Component = GetOwner() ? GetOwner()->FindComponentByClass<UPCGComponent>() : nullptr;
    if (BoundPCGComponent.Get() == Component && GenerationCompleteHandle.IsValid()) return;
    if (BoundPCGComponent.IsValid())
        BoundPCGComponent->OnPCGGraphGeneratedDelegate.Remove(GenerationCompleteHandle);
    GenerationCompleteHandle.Reset();
    BoundPCGComponent = Component;
    if (Component)
        GenerationCompleteHandle = Component->OnPCGGraphGeneratedDelegate.AddUObject(this, &UGardenGrassMaskComponent::OnGenerationComplete);
}

void UGardenGrassMaskComponent::OnGenerationComplete(UPCGComponent* Component)
{
    // The spawner creates/replaces ISMs after the mask node executes. Bind their
    // material before generation completes instead of waiting for an editor tick.
    if (!bGrassBaked && Component == BoundPCGComponent.Get())
    {
        UpdateMask();
        SyncMeshMaterials();
    }
}

void UGardenGrassMaskComponent::RefreshMask()
{
    BindGenerationComplete();
    bForceRefresh = true;
    UpdateMask();
}

void UGardenGrassMaskComponent::ReadProbe()
{
#if WITH_EDITOR
    if (ReadValidationProbe && TopDepth && BottomDepth && TopCapture && BottomCapture)
    {
        ReadValidationProbe = false;
        const FVector Center = TopCapture->GetComponentLocation();
        const double U = (ValidationProbeWorld.Y - Center.Y) / TopCapture->OrthoWidth + .5;
        const double V = .5 - (ValidationProbeWorld.X - Center.X) / TopCapture->OrthoWidth;
        const int32 X = FMath::Clamp(FMath::FloorToInt(U * TopDepth->SizeX), 0, TopDepth->SizeX-1);
        const int32 Y = FMath::Clamp(FMath::FloorToInt(V * TopDepth->SizeY), 0, TopDepth->SizeY-1);
        TArray<FLinearColor> TopPixels, BottomPixels;
        FReadSurfaceDataFlags Flags(RCM_MinMax);
        TopDepth->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(TopPixels, Flags, FIntRect(X,Y,X+1,Y+1));
        const int32 BottomY = TopDepth->SizeY-1-Y;
        BottomDepth->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(BottomPixels, Flags, FIntRect(X,BottomY,X+1,BottomY+1));
        if (TopPixels.Num() && BottomPixels.Num())
            ValidationReport = FString::Printf(TEXT("TopDepth=%f BottomDepth=%f TopZ=%f BottomZ=%f ProbeZ=%f Full=%d"),
                TopPixels[0].R, BottomPixels[0].R, Center.Z-TopPixels[0].R,
                BottomCapture->GetComponentLocation().Z+BottomPixels[0].R, ValidationProbeWorld.Z, ExcludeFullFootprint);
    }
#endif
}

void UGardenGrassMaskComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (bGrassBaked) return;
    if (GetOwner() && !GetOwner()->FindComponentByClass<UPCGComponent>())
    {
        DestroyComponent();
        return;
    }
    UpdateMask();
    ReadProbe();
}

void UGardenGrassMaskComponent::OnUnregister()
{
    if (BoundPCGComponent.IsValid())
        BoundPCGComponent->OnPCGGraphGeneratedDelegate.Remove(GenerationCompleteHandle);
    GenerationCompleteHandle.Reset();
    BoundPCGComponent.Reset();
    if (TopCapture) TopCapture->DestroyComponent();
    if (BottomCapture) BottomCapture->DestroyComponent();
    if (IncludeCapture) IncludeCapture->DestroyComponent();
    IncludeCapture = nullptr;
    IncludeDepth = nullptr;
    IncludePixels.Reset();
    TopCapture = nullptr;
    BottomCapture = nullptr;
    TopDepth = nullptr;
    BottomDepth = nullptr;
    MaskMaterial = nullptr;
    EntryMaterials.Reset();
    bForceRefresh = true;
    Super::OnUnregister();
}

FGardenGrassMaterialSettings::FGardenGrassMaterialSettings()
{
    ColorMap = TSoftObjectPtr<UTexture>(
    FSoftObjectPath(TEXT("/PCG/EdMode/DrawSplineSurface/Grass/T_Grass_D.T_Grass_D"))
);

    DryMap1 = ColorMap;
    DryMap2 = ColorMap;

    NormalMap = TSoftObjectPtr<UTexture>(
        FSoftObjectPath(TEXT("/Engine/EngineMaterials/DefaultNormal.DefaultNormal"))
    );
}



void UGardenGrassMaskComponent::ConfigureMeshMaterials(const TArray<FGardenGrassMeshEntry>& Entries)
{
    if (bGrassBaked) return;
    MeshEntries = Entries;
    BindGenerationComplete();
    SyncMeshMaterials();
}

UMaterialInstanceDynamic* UGardenGrassMaskComponent::GetMeshMaterial(int32 Index) const
{
    return EntryMaterials.IsValidIndex(Index) ? EntryMaterials[Index].Get() : nullptr;
}

void UGardenGrassMaskComponent::SyncMeshMaterials()
{
    if (!GrassMaterial || !MaskMaterial) return;
    EntryMaterials.SetNum(MeshEntries.Num());
    for (int32 Index = 0; Index < MeshEntries.Num(); ++Index)
    {
        if (!EntryMaterials[Index])
            EntryMaterials[Index] = UMaterialInstanceDynamic::Create(GrassMaterial, this);
        UMaterialInstanceDynamic* Material = EntryMaterials[Index];
        Material->ClearParameterValues();
        Material->CopyInterpParameters(MaskMaterial);
        Material->SetScalarParameterValue(TEXT("GardenGrassEntryIndex"), float(Index));
        const FGardenGrassMeshEntry& Entry = MeshEntries[Index];
        auto Texture = [Material](FName Name, const TSoftObjectPtr<UTexture>& Value)
        {
            if (UTexture* Loaded = Value.LoadSynchronous()) Material->SetTextureParameterValue(Name, Loaded);
        };
        Texture(TEXT("ColorMap"), Entry.Material.ColorMap);
        Texture(TEXT("DryMap1"), Entry.Material.DryMap1);
        Texture(TEXT("DryMap2"), Entry.Material.DryMap2);
        Texture(TEXT("NormalMap"), Entry.Material.NormalMap);
        Material->SetScalarParameterValue(TEXT("DryAmount"), FMath::Clamp(Entry.Material.DryAmount, 0.f, 1.f));
        Material->SetScalarParameterValue(TEXT("Roughness"), FMath::Clamp(Entry.Material.Roughness, 0.f, 1.f));
        Material->SetVectorParameterValue(TEXT("TintColor"), Entry.Material.TintColor);
        Material->SetVectorParameterValue(TEXT("TintDryColor"), Entry.Material.TintDryColor);
    }

    TInlineComponentArray<UInstancedStaticMeshComponent*> Components(GetOwner());
    for (UInstancedStaticMeshComponent* ISM : Components)
    {
        // The material attribute separates even duplicate meshes into distinct ISMs.
        int32 EntryIndex = EntryMaterials.IndexOfByPredicate([ISM](const auto& Material) { return Material && ISM->GetMaterial(0) == Material; });
        if (EntryIndex == INDEX_NONE && ISM->GetMaterial(0))
        {
            // The PCG spawner can duplicate a dynamic override under its ISM.
            // Read the variation marker rather than relying on pointer identity.
            float Marker = -1.f;
            if (ISM->GetMaterial(0)->GetScalarParameterValue(FMaterialParameterInfo(TEXT("GardenGrassEntryIndex")), Marker) &&
                FMath::IsFinite(Marker) && Marker >= 0 && Marker < MeshEntries.Num())
                EntryIndex = FMath::RoundToInt(Marker);
        }
        if (EntryIndex == INDEX_NONE)
        {
            // Saved ISMs lose transient material references on editor reload. Preserve
            // the selected variation index independently of the mesh asset.
            for (FName Tag : ISM->ComponentTags)
            {
                const FString Text = Tag.ToString();
                if (Text.StartsWith(TEXT("GardenGrassEntry=")))
                {
                    LexTryParseString(EntryIndex, *Text.Mid(17));
                    break;
                }
            }
        }
        if (!EntryMaterials.IsValidIndex(EntryIndex)) continue;
        ISM->ComponentTags.RemoveAll([](FName Tag) { return Tag.ToString().StartsWith(TEXT("GardenGrassEntry=")); });
        ISM->ComponentTags.Add(FName(*FString::Printf(TEXT("GardenGrassEntry=%d"), EntryIndex)));
        for (int32 Slot = 0; Slot < FMath::Max(1, ISM->GetNumMaterials()); ++Slot)
            if (ISM->GetMaterial(Slot) != EntryMaterials[EntryIndex]) ISM->SetMaterial(Slot, EntryMaterials[EntryIndex]);
    }
}

void UGardenGrassMaskComponent::UpdateMask()
{
    if (bGrassBaked || !GetWorld() || !GetOwner() || !GrassMaterial) return;
    if (!MaskMaterial) MaskMaterial = UMaterialInstanceDynamic::Create(GrassMaterial, this);
#if WITH_EDITOR
    // These are internal mask cameras; their editor preview meshes must not appear on the lawn.
    for (USceneCaptureComponent2D* Capture : { TopCapture.Get(), BottomCapture.Get(), IncludeCapture.Get() })
    {
        if (!Capture) continue;
        TArray<USceneComponent*> Children;
        Capture->GetChildrenComponents(true, Children);
        for (USceneComponent* Child : Children) Child->SetVisibility(false);
    }
#endif

    TArray<UPrimitiveComponent*> Obstacles, Surfaces;
    FBox CaptureBounds(ForceInit);
    uint32 Signature = HashCombine(GetTypeHash(EnableExclusions), GetTypeHash(ExcludeFullFootprint));
    Signature = HashCombine(Signature, GetTypeHash(GroundContactTolerance));
    Signature = HashCombine(Signature, GetTypeHash(MaskResolution));
    Signature = HashCombine(Signature, GetTypeHash(EnableIncludedSurfaces));
    Signature = HashCombine(Signature, GetTypeHash(WrapEntireSurface));
    Signature = HashCombine(Signature, GetTypeHash(AreaBounds.Min));
    Signature = HashCombine(Signature, GetTypeHash(AreaBounds.Max));
    auto Gather = [&](const TArray<TSoftObjectPtr<AActor>>& Actors, TArray<UPrimitiveComponent*>& Result)
    {
      for (const TSoftObjectPtr<AActor>& Reference : Actors)
      {
        AActor* Actor = Reference.Get();
        if (!IsValid(Actor) || Actor->GetWorld() != GetWorld()) continue;
        TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
        for (UStaticMeshComponent* Mesh : Components)
        {
#if WITH_EDITOR
            if (Mesh->IsVisualizationComponent()) continue;
#endif
            if (!Mesh->GetStaticMesh() || !Mesh->IsVisible()) continue;
            const FBox Bounds = Mesh->Bounds.GetBox();
            if (AreaBounds.IsValid && (Bounds.Max.X < AreaBounds.Min.X || Bounds.Min.X > AreaBounds.Max.X ||
                Bounds.Max.Y < AreaBounds.Min.Y || Bounds.Min.Y > AreaBounds.Max.Y)) continue;
            // Never feed generated foliage back into its own surface mask.
            if (Mesh->IsA<UInstancedStaticMeshComponent>() && Actor == GetOwner()) continue;
            Result.AddUnique(Mesh);
            CaptureBounds += Bounds;
            Signature = HashCombine(Signature, GetTypeHash(Mesh));
            Signature = HashCombine(Signature, GetTypeHash(Mesh->GetStaticMesh()));
            Signature = HashCombine(Signature, GetTypeHash(Mesh->GetComponentLocation()));
            Signature = HashCombine(Signature, GetTypeHash(Mesh->GetComponentQuat()));
            Signature = HashCombine(Signature, GetTypeHash(Mesh->GetComponentScale()));
            Signature = HashCombine(Signature, GetTypeHash(Bounds.Min));
            Signature = HashCombine(Signature, GetTypeHash(Bounds.Max));
        }
      }
    };
    if (EnableExclusions) Gather(ExcludedActors, Obstacles);
    Signature = HashCombine(Signature, 0x51faceu);
    if (EnableIncludedSurfaces) Gather(IncludedSurfaces, Surfaces);
    const bool bActive = EnableExclusions && !Obstacles.IsEmpty();
    if (!bForceRefresh && Signature == LastSignature) return;
    LastSignature = Signature;
    bForceRefresh = false;
    ON_SCOPE_EXIT { SyncMeshMaterials(); };
    MaskMaterial->SetVectorParameterValue(TEXT("GrassMaskMode"), FLinearColor(ExcludeFullFootprint ? 1.f : 0.f,
        GroundContactTolerance, bActive ? 1.f : 0.f, EnableIncludedSurfaces ? (WrapEntireSurface ? 2.f : 1.f) : 0.f));
    MaskMaterial->SetScalarParameterValue(TEXT("GrassIncludeValid"), Surfaces.IsEmpty() ? 0.f : 1.f);
    if (!bActive && Surfaces.IsEmpty())
    {
        if (Surfaces.IsEmpty()) IncludePixels.Reset();
        return;
    }
    LastSignature = Signature;
    bForceRefresh = false;

    // Limit XY to the lawn so distant selected meshes do not reduce mask precision.
    if (AreaBounds.IsValid)
    {
        CaptureBounds.Min.X = FMath::Max(CaptureBounds.Min.X, AreaBounds.Min.X - 100.0);
        CaptureBounds.Min.Y = FMath::Max(CaptureBounds.Min.Y, AreaBounds.Min.Y - 100.0);
        CaptureBounds.Max.X = FMath::Min(CaptureBounds.Max.X, AreaBounds.Max.X + 100.0);
        CaptureBounds.Max.Y = FMath::Min(CaptureBounds.Max.Y, AreaBounds.Max.Y + 100.0);
    }
    const FVector Center = CaptureBounds.GetCenter();
    const double Width = FMath::Max(CaptureBounds.GetSize().X, CaptureBounds.GetSize().Y) + 4.0;
    const double TopZ = CaptureBounds.Max.Z + 100.0;
    const double BottomZ = CaptureBounds.Min.Z - 100.0;
    const int32 Resolution = FMath::Clamp(MaskResolution, 256, 4096);
    auto PrepareCapture = [&](TObjectPtr<USceneCaptureComponent2D>& Capture, TObjectPtr<UTextureRenderTarget2D>& Target,
        const FVector& Location, const FRotator& Rotation, const TArray<UPrimitiveComponent*>& ShowMeshes)
    {
        if (!Target)
        {
            Target = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
            Target->ClearColor = FLinearColor(100000000.f, 0, 0, 0);
            Target->Filter = TF_Nearest;
            Target->InitCustomFormat(Resolution, Resolution, PF_R32_FLOAT, true);
            Target->UpdateResourceImmediate(true);
        }
        else if (Target->SizeX != Resolution) Target->ResizeTarget(Resolution, Resolution);
        if (!Capture)
        {
            Capture = NewObject<USceneCaptureComponent2D>(GetOwner(), NAME_None, RF_Transient);
            Capture->bCaptureEveryFrame = false;
            Capture->bCaptureOnMovement = false;
            Capture->ProjectionType = ECameraProjectionMode::Orthographic;
            Capture->CaptureSource = ESceneCaptureSource::SCS_SceneDepth;
            Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
            Capture->bAutoCalculateOrthoPlanes = false;
            Capture->bUpdateOrthoPlanes = false;
            Capture->bAlwaysPersistRenderingState = false;
            Capture->PostProcessBlendWeight = 0;
            Capture->ShowFlags.SetAntiAliasing(false);
            Capture->ShowFlags.SetAtmosphere(false);
            Capture->ShowFlags.SetFog(false);
            Capture->RegisterComponent();
        }
        Capture->TextureTarget = Target;
        Capture->OrthoWidth = Width;


        Capture->SetWorldLocationAndRotation(Location, Rotation);
        Capture->ShowOnlyComponents.Reset();
        for (UPrimitiveComponent* Obstacle : ShowMeshes) Capture->ShowOnlyComponents.Add(Obstacle);
        Capture->CaptureScene();
    };
    PrepareCapture(TopCapture, TopDepth, FVector(Center.X, Center.Y, TopZ), FRotator(-90, 0, 0), Obstacles);
    PrepareCapture(BottomCapture, BottomDepth, FVector(Center.X, Center.Y, BottomZ), FRotator(90, 0, 0), Obstacles);
    MaskMaterial->SetTextureParameterValue(TEXT("GrassMaskTop"), TopDepth);
    MaskMaterial->SetTextureParameterValue(TEXT("GrassMaskBottom"), BottomDepth);
    MaskMaterial->SetVectorParameterValue(TEXT("GrassMaskFrame"), FLinearColor(Center.X, Center.Y, Width, TopZ));
    MaskMaterial->SetScalarParameterValue(TEXT("GrassMaskBottomZ"), BottomZ);
    IncludePixels.Reset();
    if (!Surfaces.IsEmpty())
    {
        PrepareCapture(IncludeCapture, IncludeDepth, FVector(Center.X, Center.Y, TopZ), FRotator(-90, 0, 0), Surfaces);
        MaskMaterial->SetTextureParameterValue(TEXT("GrassIncludeTop"), IncludeDepth);
        IncludeFrame = FVector4(Center.X, Center.Y, Width, TopZ);
        CaptureHeight = TopZ - BottomZ;
        FReadSurfaceDataFlags Flags(RCM_MinMax);
        if (!WrapEntireSurface)
            IncludeDepth->GameThread_GetRenderTargetResource()->ReadLinearColorPixels(IncludePixels, Flags);
    }
}

bool UGardenGrassMaskComponent::ProjectToIncludedSurface(FVector& Position, FVector& Normal) const
{
    if (!IncludeDepth || IncludePixels.Num() != IncludeDepth->SizeX * IncludeDepth->SizeY) return false;
    const int32 Size = IncludeDepth->SizeX;
    const double U = (Position.Y - IncludeFrame.Y) / IncludeFrame.Z + .5;
    const double V = .5 - (Position.X - IncludeFrame.X) / IncludeFrame.Z;
    if (U < 0 || U >= 1 || V < 0 || V >= 1) return false;
    const int32 X = FMath::Clamp(int32(U * Size), 0, Size - 1);
    const int32 Y = FMath::Clamp(int32(V * Size), 0, Size - 1);
    auto Depth = [&](int32 PX, int32 PY) { return IncludePixels[FMath::Clamp(PY, 0, Size-1) * Size + FMath::Clamp(PX, 0, Size-1)].R; };
    const double D = Depth(X, Y);
    if (!FMath::IsFinite(D) || D <= 0 || D >= CaptureHeight) return false;
    Position.Z = IncludeFrame.W - D;
    const double Step = IncludeFrame.Z / Size;
    const double DX = Depth(X, Y+1), DY = Depth(X+1, Y);
    Normal = FVector::UpVector;
    if (DX > 0 && DX < CaptureHeight && DY > 0 && DY < CaptureHeight &&
        FMath::Abs(DX-D) < Step * 4 && FMath::Abs(DY-D) < Step * 4)
        Normal = FVector((D-DX)/Step, (DY-D)/Step, 1).GetSafeNormal();
    return true;
}

UGardenGrassMaskSettings::UGardenGrassMaskSettings()
{
    GrassMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/PCG/EdMode/DrawSplineSurface/Grass/M_GardenGrass.M_GardenGrass")));

}

class FGardenGrassMaskElement final : public IPCGElement
{
public:
    virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }

    void SampleMeshSurfaces(FPCGContext* Context, const UGardenGrassMaskSettings* Settings, AActor* Owner) const
    {
        const TArray<FPCGTaggedData> Boundaries = Context->InputData.GetInputsByPin(TEXT("Surface"));
        Context->OutputData.TaggedData.Reset();
        if (Boundaries.IsEmpty())
        {
            PCGE_LOG(Error, GraphAndLog, NSLOCTEXT("GardenGrass", "MissingSurface", "Wrap Entire Surface requires the spline surface connected to the Surface pin."));
            return;
        }
        TArray<FTransform> Transforms;
        TArray<int32> Seeds;
        FRandomStream Random(Context->GetSeed());
        TSet<UStaticMeshComponent*> Visited;
        for (const TSoftObjectPtr<AActor>& Reference : Settings->IncludedSurfaces)
        {
            AActor* Actor = Reference.Get();
            if (!IsValid(Actor) || Actor->GetWorld() != Owner->GetWorld()) continue;
            TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
            for (UStaticMeshComponent* Component : Components)
            {
                if (!Component->IsVisible() || Visited.Contains(Component)) continue;
                if (Component->IsA<UInstancedStaticMeshComponent>() && Actor == Owner) continue;
#if WITH_EDITOR
                if (Component->IsVisualizationComponent()) continue;
#endif
                Visited.Add(Component);
                UStaticMesh* Mesh = Component->GetStaticMesh();
                if (!Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) continue;
#if !WITH_EDITOR
                if (!Mesh->bAllowCPUAccess)
                {
                    PCGE_LOG(Error, GraphAndLog, NSLOCTEXT("GardenGrass", "CPUAccess", "Wrapped included meshes require Allow CPU Access for runtime generation."));
                    continue;
                }
#endif
                const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
                const auto& Vertices = LOD.VertexBuffers.PositionVertexBuffer;
                const auto& Tangents = LOD.VertexBuffers.StaticMeshVertexBuffer;
                const FMatrix NormalMatrix = Component->GetComponentTransform().ToMatrixWithScale().InverseFast().GetTransposed();
                const double Density = FMath::Max(0.0, Settings->SurfaceDensity) / 10000.0;
                for (int32 Triangle = 0; Triangle + 2 < LOD.IndexBuffer.GetNumIndices(); Triangle += 3)
                {
                    const uint32 I0 = LOD.IndexBuffer.GetIndex(Triangle), I1 = LOD.IndexBuffer.GetIndex(Triangle+1), I2 = LOD.IndexBuffer.GetIndex(Triangle+2);
                    const FVector A = Component->GetComponentTransform().TransformPosition(FVector(Vertices.VertexPosition(I0)));
                    const FVector B = Component->GetComponentTransform().TransformPosition(FVector(Vertices.VertexPosition(I1)));
                    const FVector C = Component->GetComponentTransform().TransformPosition(FVector(Vertices.VertexPosition(I2)));
                    const double Expected = FVector::CrossProduct(B-A, C-A).Length() * 0.5 * Density;
                    if (!FMath::IsFinite(Expected) || Expected > 2000000 || Transforms.Num() + Expected > 2000000)
                    {
                        PCGE_LOG(Error, GraphAndLog, NSLOCTEXT("GardenGrass", "TooManySamples", "Wrapped surface exceeds two million samples. Reduce Grass Density or split the area."));
                        return;
                    }
                    const int32 Count = FMath::FloorToInt(Expected) + (Random.FRand() < FMath::Frac(Expected) ? 1 : 0);
                    for (int32 Sample = 0; Sample < Count; ++Sample)
                    {
                        const double S = FMath::Sqrt(Random.FRand()), T = Random.FRand();
                        const double U = 1-S, V = S*(1-T), W = S*T;
                        const FVector Position = U*A + V*B + W*C;
                        bool bInside = false;
                        for (const FPCGTaggedData& Boundary : Boundaries)
                        {
                            const UPCGSpatialData* Spatial = Cast<UPCGSpatialData>(Boundary.Data);
                            if (!Spatial) continue;
                            FPCGPoint Probe;
                            // Projection checks the spline footprint and evaluates its height at this XY.
                            FPCGProjectionParams Projection;
                            Projection.bProjectPositions = true;
                            Projection.bProjectRotations = false;
                            if (Spatial->ProjectPoint(FTransform(Position), FBox(FVector(-0.001,-0.001,-1.e9), FVector(0.001,0.001,1.e9)), Projection, Probe, nullptr) && Probe.Density > 0)
                            {
                                if (Settings->WrapCoverage == EGardenGrassWrapCoverage::AboveSpline &&
                                    Position.Z < Probe.Transform.GetLocation().Z + Settings->WrapHeightOffset) continue;
                                bInside = true;
                                break;
                            }
                        }
                        if (!bInside) continue;
                        const FVector LocalNormal = U*FVector(Tangents.VertexTangentZ(I0)) + V*FVector(Tangents.VertexTangentZ(I1)) + W*FVector(Tangents.VertexTangentZ(I2));
                        const FVector Normal = FVector(NormalMatrix.TransformVector(LocalNormal)).GetSafeNormal();
                        Transforms.Emplace(Settings->AlignToSurface ? FQuat::FindBetweenNormals(FVector::UpVector, Normal) : FQuat::Identity, Position);
                        Seeds.Add(Random.RandHelper(MAX_int32));
                    }
                }
            }
        }
        UPCGBasePointData* Output = FPCGContext::NewPointData_AnyThread(Context);
        Output->SetNumPoints(Transforms.Num());
        auto OutTransforms = Output->GetTransformValueRange();
        auto OutSeeds = Output->GetSeedValueRange();
        for (int32 Index = 0; Index < Transforms.Num(); ++Index)
        {
            OutTransforms[Index] = Transforms[Index];
            OutSeeds[Index] = Seeds[Index];
        }
        FPCGTaggedData& Result = Context->OutputData.TaggedData.Emplace_GetRef();
        Result.Data = Output;
        Result.Pin = PCGPinConstants::DefaultOutputLabel;
    }
    virtual bool IsCacheable(const UPCGSettings*) const override { return false; }
protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override
    {
        Context->OutputData.TaggedData = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
        for (FPCGTaggedData& Data : Context->OutputData.TaggedData) Data.Pin = PCGPinConstants::DefaultOutputLabel;
        const UGardenGrassMaskSettings* Settings = Context->GetInputSettings<UGardenGrassMaskSettings>();
        AActor* Owner = Context->GetTargetActor(nullptr);
        if (!Owner) return true;
        UGardenGrassMaskComponent* Mask = Owner->FindComponentByClass<UGardenGrassMaskComponent>();
        if (!Mask)
        {
            Mask = NewObject<UGardenGrassMaskComponent>(Owner, TEXT("GardenGrassMask"), RF_Transactional);
            Owner->AddInstanceComponent(Mask);
            Mask->RegisterComponent();
        }
        Mask->EnableExclusions = Settings->EnableExclusions;
        Mask->ExcludeFullFootprint = Settings->ExcludeFullFootprint;
        Mask->GroundContactTolerance = FMath::Max(0.0, Settings->ContactProbeHalfSize.Z);
        Mask->EnableIncludedSurfaces = Settings->EnableIncludedSurfaces;
        Mask->WrapEntireSurface = Settings->EnableIncludedSurfaces && Settings->WrapEntireSurface && Settings->ProjectPointsToSurface;
        Mask->IncludedSurfaces = Settings->IncludedSurfaces;
        Mask->ExcludedActors = Settings->ExcludedActors;
        Mask->MaskResolution = Settings->MaskResolution;
        Mask->GrassMaterial = Settings->GrassMaterial.LoadSynchronous();
        Mask->AreaBounds = FBox(ForceInit);
        for (const FPCGTaggedData& Data : Context->InputData.TaggedData)
            if (const UPCGSpatialData* Spatial = Cast<UPCGSpatialData>(Data.Data)) Mask->AreaBounds += Spatial->GetBounds();
        Mask->RefreshMask();
        if (Mask->WrapEntireSurface)
        {
            SampleMeshSurfaces(Context, Settings, Owner);
            return true;
        }
        // Inclusion still filters the spline when both projection controls are disabled.
        if (Settings->ProjectPointsToSurface || Settings->AlignToSurface || Settings->EnableIncludedSurfaces)
        {
            FCollisionQueryParams Query(SCENE_QUERY_STAT(GardenGrassGround), true);
            if (Settings->EnableExclusions)
                for (const TSoftObjectPtr<AActor>& Actor : Settings->ExcludedActors)
                    if (Actor.IsValid()) Query.AddIgnoredActor(Actor.Get());
            TInlineComponentArray<UInstancedStaticMeshComponent*> Generated(Owner);
            for (UInstancedStaticMeshComponent* Component : Generated) Query.AddIgnoredComponent(Component);
            for (FPCGTaggedData& Data : Context->OutputData.TaggedData)
            {
                const UPCGBasePointData* Input = Cast<UPCGBasePointData>(Data.Data);
                if (!Input) continue;
                UPCGBasePointData* Output = FPCGContext::NewPointData_AnyThread(Context);
                FPCGInitializeFromDataParams Params(Input);
                Params.bInheritSpatialData = false;
                Output->InitializeFromDataWithParams(Params);
                Output->SetNumPoints(Input->GetNumPoints(), false);
                Input->CopyPointsTo(Output, 0, 0, Input->GetNumPoints());
                Output->CopyUnallocatedPropertiesFrom(Input);
                auto Transforms = Output->GetTransformValueRange();
                TArray<int32> KeptIndices;
                for (int32 Index = 0; Index < Input->GetNumPoints(); ++Index)
                {
                    FTransform Transform = Transforms[Index];
                    FVector Position = Transform.GetLocation(), Normal = FVector::UpVector;
                    const double MaxDistance = FMath::Max(0.0, Settings->SnapToSurfaceMaxDistance);
                    bool bHit = false;
                    if (Settings->EnableIncludedSurfaces)
                        bHit = Mask->ProjectToIncludedSurface(Position, Normal);
                    else
                    {
                        FHitResult Hit;
                        bHit = Owner->GetWorld()->LineTraceSingleByChannel(Hit, Position + FVector(0,0,MaxDistance),
                            Position - FVector(0,0,MaxDistance), ECC_WorldStatic, Query);
                        if (bHit) { Position = Hit.ImpactPoint; Normal = Hit.ImpactNormal; }
                    }
                    if (!bHit && Settings->EnableIncludedSurfaces) continue;
                    KeptIndices.Add(Index);
                    if (bHit && FMath::Abs(Position.Z - Transform.GetLocation().Z) <= MaxDistance)
                    {
                        if (Settings->ProjectPointsToSurface) Transform.SetLocation(Position);
                        if (Settings->AlignToSurface)
                            Transform.SetRotation((FQuat::FindBetweenNormals(Transform.GetRotation().GetUpVector(), Normal)
                                * Transform.GetRotation()).GetNormalized());
                    }
                    Transforms[Index] = Transform;
                }
                if (KeptIndices.Num() != Input->GetNumPoints())
                {
                    UPCGBasePointData* Filtered = FPCGContext::NewPointData_AnyThread(Context);
                    FPCGInitializeFromDataParams FilterParams(Output);
                    FilterParams.bInheritSpatialData = false;
                    Filtered->InitializeFromDataWithParams(FilterParams);
                    Filtered->SetNumPoints(KeptIndices.Num(), false);
                    TArray<int32> WriteIndices;
                    for (int32 Index = 0; Index < KeptIndices.Num(); ++Index) WriteIndices.Add(Index);
                    Output->CopyPointsTo(Filtered, KeptIndices, WriteIndices);
                    Filtered->CopyUnallocatedPropertiesFrom(Output);
                    Data.Data = Filtered;
                }
                else Data.Data = Output;
            }
        }
        return true;
    }
};

FPCGElementPtr UGardenGrassMaskSettings::CreateElement() const
{
    return MakeShared<FGardenGrassMaskElement>();
}

#if WITH_EDITOR
TArray<FPCGSettingsOverridableParam> UGardenGrassMeshSettings::GatherOverridableParams() const
{
    // Keep the variation struct intact rather than exposing its members as
    // unrelated parallel arrays. The property bag supplies an array of structs.
    PCGSettingsHelpers::FPCGGetAllOverridableParamsConfig Config;
    Config.bExtractArrays = true;
    Config.MaxContainersNum = 1;
    Config.MaxStructDepth = 0;
    Config.bDiscardLeafStructProperty = false;
    Config.ShouldKeepPropertyFunc = [](const FProperty* Property, int32)
    {
        return Property->GetFName() == GET_MEMBER_NAME_CHECKED(UGardenGrassMeshSettings, GrassMeshes);
    };
    return PCGSettingsHelpers::GetAllOverridableParams(GetClass(), Config);
}
#endif

UGardenGrassMeshSettings::UGardenGrassMeshSettings()
{
}

// Generation-time root exclusion. Keep this independent of material vertex
// interpolation (in particular Nanite's instance transform path).
struct FGardenGrassRootBlocker
{
    FBox Bounds = FBox(ForceInit);
    TArray<FVector> Vertices;

    bool Contains(const FVector& P, bool bFullFootprint) const
    {
        if (P.X < Bounds.Min.X || P.X > Bounds.Max.X || P.Y < Bounds.Min.Y || P.Y > Bounds.Max.Y) return false;
        if (!bFullFootprint && (P.Z < Bounds.Min.Z - .1 || P.Z > Bounds.Max.Z + .1)) return false;
        TArray<double, TInlineAllocator<16>> Heights;
        for (int32 I = 0; I + 2 < Vertices.Num(); I += 3)
        {
            const FVector A = Vertices[I], B = Vertices[I+1], C = Vertices[I+2];
            const double Den = (B.Y-C.Y)*(A.X-C.X) + (C.X-B.X)*(A.Y-C.Y);
            if (FMath::Abs(Den) < 1.e-10) continue;
            const double U = ((B.Y-C.Y)*(P.X-C.X) + (C.X-B.X)*(P.Y-C.Y)) / Den;
            const double V = ((C.Y-A.Y)*(P.X-C.X) + (A.X-C.X)*(P.Y-C.Y)) / Den;
            if (U < -1.e-7 || V < -1.e-7 || U+V > 1.0000001) continue;
            if (bFullFootprint) return true;
            const double Z = U*A.Z + V*B.Z + (1-U-V)*C.Z;
            if (FMath::Abs(P.Z-Z) <= .1) return true;
            if (!Heights.ContainsByPredicate([Z](double H) { return FMath::Abs(H-Z) < .001; })) Heights.Add(Z);
        }
        // Odd/even crossings preserve gaps between disconnected closed shells.
        if (Heights.Num() < 2 || (Heights.Num() & 1)) return false;
        int32 Crossings = 0;
        for (double Z : Heights) if (Z > P.Z) ++Crossings;
        return (Crossings & 1) != 0;
    }
};

static TArray<FGardenGrassRootBlocker> BuildGardenGrassRootBlockers(const UGardenGrassMaskComponent* Mask)
{
    TArray<FGardenGrassRootBlocker> Result;
    if (!Mask->EnableExclusions) return Result;
    for (const TSoftObjectPtr<AActor>& Reference : Mask->ExcludedActors)
    {
        AActor* Actor = Reference.Get();
        if (!IsValid(Actor)) continue;
        TInlineComponentArray<UStaticMeshComponent*> Components(Actor);
        for (UStaticMeshComponent* Component : Components)
        {
            UStaticMesh* Mesh = Component->GetStaticMesh();
            if (!Component->IsVisible() || !Mesh || !Mesh->GetRenderData() || Mesh->GetRenderData()->LODResources.IsEmpty()) continue;
            if (Mask->GetWorld()->IsGameWorld() && !Mesh->bAllowCPUAccess) continue;
            const FStaticMeshLODResources& LOD = Mesh->GetRenderData()->LODResources[0];
            const auto Indices = LOD.IndexBuffer.GetArrayView();
            const auto& Positions = LOD.VertexBuffers.PositionVertexBuffer;
            TArray<FTransform> Transforms;
            if (UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component))
            {
                for (int32 I = 0; I < ISM->GetInstanceCount(); ++I)
                {
                    FTransform T;
                    if (ISM->GetInstanceTransform(I, T, true)) Transforms.Add(T);
                }
            }
            else Transforms.Add(Component->GetComponentTransform());
            for (const FTransform& Transform : Transforms)
            {
                FGardenGrassRootBlocker& Blocker = Result.Emplace_GetRef();
                Blocker.Vertices.Reserve(Indices.Num());
                for (int32 IndexOffset = 0; IndexOffset < Indices.Num(); ++IndexOffset)
                {
                    const uint32 Index = Indices[IndexOffset];
                    const FVector P = Transform.TransformPosition(FVector(Positions.VertexPosition(Index)));
                    Blocker.Vertices.Add(P);
                    Blocker.Bounds += P;
                }
            }
        }
    }
    return Result;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGardenGrassRootExclusionTest, "PCG.GardenGrass.RootExclusion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FGardenGrassRootExclusionTest::RunTest(const FString& Parameters)
{
    FGardenGrassRootBlocker B;
    auto AddPlane = [&B](double Z)
    {
        for (FVector P : {FVector(-1,-1,Z), FVector(1,-1,Z), FVector(1,1,Z),
            FVector(-1,-1,Z), FVector(1,1,Z), FVector(-1,1,Z)})
        { B.Vertices.Add(P); B.Bounds += P; }
    };
    AddPlane(-1); AddPlane(1);
    TestTrue(TEXT("Embedded root removed including shared triangle diagonal"), B.Contains(FVector::ZeroVector, false));
    TestFalse(TEXT("Root outside side preserved"), B.Contains(FVector(1.01,0,0), false));
    TestFalse(TEXT("Root above blocker preserved in contact mode"), B.Contains(FVector(0,0,2), false));
    TestTrue(TEXT("Full footprint includes above blocker"), B.Contains(FVector(0,0,2), true));
    TestFalse(TEXT("Full footprint preserves unrelated XY"), B.Contains(FVector(2,0,0), true));
    AddPlane(3); AddPlane(5);
    TestFalse(TEXT("Gap between disconnected shells preserved"), B.Contains(FVector(0,0,2), false));
    TestTrue(TEXT("Second shell excluded"), B.Contains(FVector(0,0,4), false));
    return true;
}
#endif

class FGardenGrassMeshElement final : public IPCGElement
{
public:
    virtual bool CanExecuteOnlyOnMainThread(FPCGContext*) const override { return true; }
    virtual bool IsCacheable(const UPCGSettings*) const override { return false; }

protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override
    {
        const UGardenGrassMeshSettings* Settings = Context->GetInputSettings<UGardenGrassMeshSettings>();
        if (!Settings)
        {
            return true;
        }

        AActor* Owner = Context->GetTargetActor(nullptr);
        UGardenGrassMaskComponent* Mask = Owner ? Owner->FindComponentByClass<UGardenGrassMaskComponent>() : nullptr;
        if (!Mask) return true;
        Mask->ConfigureMeshMaterials(Settings->GrassMeshes);
        const TArray<FGardenGrassRootBlocker> RootBlockers = BuildGardenGrassRootBlockers(Mask);
        TArray<int32> Choices;
        TArray<double> CumulativeWeights;
        double TotalWeight = 0;
        for (int32 Index = 0; Index < Settings->GrassMeshes.Num(); ++Index)
        {
            const FGardenGrassMeshEntry& Entry = Settings->GrassMeshes[Index];
            if (Entry.Weight > 0 && FMath::IsFinite(Entry.Weight) && Entry.StaticMesh.LoadSynchronous() && Mask->GetMeshMaterial(Index))
            {
                Choices.Add(Index);
                TotalWeight += Entry.Weight;
                CumulativeWeights.Add(TotalWeight);
            }
        }
        // No eligible entries is a valid empty result, not an attribute error
        // for the downstream spawner. PCG retires its previous unused ISMs.
        if (Choices.IsEmpty()) return true;

        auto PickEntry = [&](int32 Seed)
        {
            FRandomStream Random(Seed);
            const double Sample = double(Random.FRand()) * TotalWeight;
            for (int32 Choice = 0; Choice < Choices.Num(); ++Choice)
                if (Sample < CumulativeWeights[Choice]) return Choices[Choice];
            return Choices.Last();
        };

        for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
        {
            const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Input.Data);
            if (!PointData)
            {
                Context->OutputData.TaggedData.Add(Input);
                continue;
            }

            UPCGBasePointData* OutputPointData = FPCGContext::NewPointData_AnyThread(Context);
            FPCGInitializeFromDataParams InitializeFromDataParams(PointData);
            InitializeFromDataParams.bInheritSpatialData = false;
            OutputPointData->InitializeFromDataWithParams(InitializeFromDataParams);

            TArray<int32> KeptIndices;
            const auto RootTransforms = PointData->GetConstTransformValueRange();
            for (int32 I = 0; I < PointData->GetNumPoints(); ++I)
                if (!RootBlockers.ContainsByPredicate([&](const FGardenGrassRootBlocker& Blocker)
                    { return Blocker.Contains(RootTransforms[I].GetLocation(), Mask->ExcludeFullFootprint); })) KeptIndices.Add(I);
            const int32 NumPoints = KeptIndices.Num();
            OutputPointData->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
            TArray<int32> WriteIndices;
            WriteIndices.SetNumUninitialized(NumPoints);
            for (int32 I = 0; I < NumPoints; ++I) WriteIndices[I] = I;
            PointData->CopyPointsTo(OutputPointData, KeptIndices, WriteIndices);
            OutputPointData->CopyUnallocatedPropertiesFrom(PointData);

            FPCGMetadataAttribute<FSoftObjectPath>* MeshAttribute =
                OutputPointData->MutableMetadata()->FindOrCreateAttribute<FSoftObjectPath>(
                    Settings->MeshAttributeName,
                    Settings->GrassMeshes[Choices[0]].StaticMesh.ToSoftObjectPath(),
                    /*bAllowsInterpolation=*/false,
                    /*bOverrideParent=*/false,
                    /*bOverwriteIfTypeMismatch=*/true);

            FPCGMetadataAttribute<FSoftObjectPath>* MaterialAttribute =
                OutputPointData->MutableMetadata()->FindOrCreateAttribute<FSoftObjectPath>(
                    TEXT("GardenGrassMaterial"), FSoftObjectPath(Mask->GetMeshMaterial(Choices[0])), false, false, true);

            if (MeshAttribute && MaterialAttribute)
            {
                TPCGValueRange<int64> MetadataEntries = OutputPointData->GetMetadataEntryValueRange();
                const TConstPCGValueRange<int32> Seeds = OutputPointData->GetConstSeedValueRange();
                for (int32 Index = 0; Index < NumPoints; ++Index)
                {
                    // Spline samples can share the invalid/default metadata key.
                    // Give every point its own entry before assigning its variation.
                    MetadataEntries[Index] = OutputPointData->MutableMetadata()->AddEntry(MetadataEntries[Index]);
                    const int32 Choice = PickEntry(HashCombineFast(Seeds[Index], KeptIndices[Index]));
                    MeshAttribute->SetValue(MetadataEntries[Index], Settings->GrassMeshes[Choice].StaticMesh.ToSoftObjectPath());
                    MaterialAttribute->SetValue(MetadataEntries[Index], FSoftObjectPath(Mask->GetMeshMaterial(Choice)));
                }
            }

            FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
            Output.Data = OutputPointData;
            Output.Pin = PCGPinConstants::DefaultOutputLabel;
        }

        return true;
    }
};

FPCGElementPtr UGardenGrassMeshSettings::CreateElement() const
{
    return MakeShared<FGardenGrassMeshElement>();
}

UGardenGrassRandomTransformSettings::UGardenGrassRandomTransformSettings()
{
}

class FGardenGrassRandomTransformElement final : public IPCGElement
{
public:
    virtual bool IsCacheable(const UPCGSettings*) const override { return false; }

protected:
    virtual bool ExecuteInternal(FPCGContext* Context) const override
    {
        const UGardenGrassRandomTransformSettings* Settings = Context->GetInputSettings<UGardenGrassRandomTransformSettings>();
        if (!Settings)
        {
            return true;
        }

        if (!Settings->EnableRandomTransform)
        {
            Context->OutputData.TaggedData = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);
            for (FPCGTaggedData& Data : Context->OutputData.TaggedData)
            {
                Data.Pin = PCGPinConstants::DefaultOutputLabel;
            }
            return true;
        }

        auto RandomRange = [](FRandomStream& RandomStream, const double Min, const double Max)
        {
            return RandomStream.FRandRange(static_cast<float>(FMath::Min(Min, Max)), static_cast<float>(FMath::Max(Min, Max)));
        };

        auto RandomVector = [&RandomRange](FRandomStream& RandomStream, const FVector& Min, const FVector& Max)
        {
            return FVector(
                RandomRange(RandomStream, Min.X, Max.X),
                RandomRange(RandomStream, Min.Y, Max.Y),
                RandomRange(RandomStream, Min.Z, Max.Z));
        };

        auto RandomRotator = [&RandomRange](FRandomStream& RandomStream, const FRotator& Min, const FRotator& Max)
        {
            return FRotator(
                RandomRange(RandomStream, Min.Pitch, Max.Pitch),
                RandomRange(RandomStream, Min.Yaw, Max.Yaw),
                RandomRange(RandomStream, Min.Roll, Max.Roll));
        };

        for (const FPCGTaggedData& Input : Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel))
        {
            const UPCGBasePointData* PointData = Cast<UPCGBasePointData>(Input.Data);
            if (!PointData)
            {
                Context->OutputData.TaggedData.Add(Input);
                continue;
            }

            UPCGBasePointData* OutputPointData = FPCGContext::NewPointData_AnyThread(Context);
            FPCGInitializeFromDataParams InitializeFromDataParams(PointData);
            InitializeFromDataParams.bInheritSpatialData = false;
            OutputPointData->InitializeFromDataWithParams(InitializeFromDataParams);

            const int32 NumPoints = PointData->GetNumPoints();
            OutputPointData->SetNumPoints(NumPoints, /*bInitializeValues=*/false);
            PointData->CopyPointsTo(OutputPointData, 0, 0, NumPoints);
            OutputPointData->CopyUnallocatedPropertiesFrom(PointData);

            TPCGValueRange<FTransform> TransformRange = OutputPointData->GetTransformValueRange(/*bAllocate=*/false);
            const TConstPCGValueRange<int32> Seeds = OutputPointData->GetConstSeedValueRange();
            for (int32 Index = 0; Index < NumPoints; ++Index)
            {
                FRandomStream RandomStream(HashCombineFast(Seeds[Index], Index));
                FTransform Transform = TransformRange[Index];
                Transform.SetLocation(Transform.GetLocation() + RandomVector(RandomStream, Settings->OffsetMin, Settings->OffsetMax));
                Transform.SetRotation((Transform.GetRotation() * RandomRotator(RandomStream, Settings->RotationMin, Settings->RotationMax).Quaternion()).GetNormalized());
                Transform.SetScale3D(Transform.GetScale3D() * RandomVector(RandomStream, Settings->ScaleMin, Settings->ScaleMax));
                TransformRange[Index] = Transform;
            }

            FPCGTaggedData& Output = Context->OutputData.TaggedData.Add_GetRef(Input);
            Output.Data = OutputPointData;
            Output.Pin = PCGPinConstants::DefaultOutputLabel;
        }

        return true;
    }
};

FPCGElementPtr UGardenGrassRandomTransformSettings::CreateElement() const
{
    return MakeShared<FGardenGrassRandomTransformElement>();
}
