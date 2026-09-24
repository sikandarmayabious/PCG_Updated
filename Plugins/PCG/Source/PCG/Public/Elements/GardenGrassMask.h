#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PCGSettings.h"
#include "GardenGrassMask.generated.h"

class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UTexture;
class UPCGComponent;
class UInstancedStaticMeshComponent;

UENUM(BlueprintType)
enum class EGardenGrassWrapCoverage : uint8
{
    EntireSurface UMETA(DisplayName="Entire Surface"),
    AboveSpline UMETA(DisplayName="Above Spline")
};

/** One weighted grass variation, with its own textures and appearance. */

USTRUCT(BlueprintType)
struct PCG_API FGardenGrassMaterialSettings
{
    GENERATED_BODY()
    FGardenGrassMaterialSettings();

    /** Base color/albedo map. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    TSoftObjectPtr<UTexture> ColorMap;

    /** Amount of dry grass blending. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DryAmount = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    TSoftObjectPtr<UTexture> DryMap1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    TSoftObjectPtr<UTexture> DryMap2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    FLinearColor TintColor = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    FLinearColor TintDryColor = FLinearColor(0.85f, 0.72f, 0.38f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material")
    TSoftObjectPtr<UTexture> NormalMap;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Material", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float Roughness = 0.85f;
};

USTRUCT(BlueprintType)
struct PCG_API FGardenGrassMeshEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grass")
    TSoftObjectPtr<UStaticMesh> StaticMesh;

    /** Relative selection probability. Zero disables this entry. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Grass", meta=(ClampMin="0"))
    float Weight = 1.f;

    /** Material parameters applied to this grass mesh. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass")
    FGardenGrassMaterialSettings Material;
};

/** Per-area, explicit actor mesh silhouettes used to clip grass pixels at obstacle edges. */
UCLASS(ClassGroup=(Procedural), meta=(BlueprintSpawnableComponent))
class PCG_API UGardenGrassMaskComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGardenGrassMaskComponent();

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    virtual void OnRegister() override;
    virtual void OnUnregister() override;

    UPROPERTY(EditAnywhere, Category="Grass Mask")
    bool EnableExclusions = true;

    UPROPERTY(EditAnywhere, Category="Grass Mask")
    bool ExcludeFullFootprint = false;

    UPROPERTY(EditAnywhere, Category="Grass Mask", meta=(ClampMin="0"))
    float GroundContactTolerance = 3.f;

    UPROPERTY(EditAnywhere, Category="Grass Mask")
    bool EnableIncludedSurfaces = false;

    UPROPERTY()
    bool WrapEntireSurface = false;

    UPROPERTY(EditAnywhere, Category="Grass Mask", meta=(EditCondition="EnableIncludedSurfaces"))
    TArray<TSoftObjectPtr<AActor>> IncludedSurfaces;

    UPROPERTY(EditAnywhere, Category="Grass Mask", meta=(EditCondition="EnableExclusions"))
    TArray<TSoftObjectPtr<AActor>> ExcludedActors;

    bool ProjectToIncludedSurface(FVector& Position, FVector& Normal) const;

    UPROPERTY(EditAnywhere, Category="Grass Mask", meta=(ClampMin="256", ClampMax="4096"))
    int32 MaskResolution = 2048;

    UPROPERTY()
    FBox AreaBounds = FBox(ForceInit);

    UPROPERTY()
    TObjectPtr<UMaterialInterface> GrassMaterial;

    UPROPERTY()
    TArray<FGardenGrassMeshEntry> MeshEntries;

    void ConfigureMeshMaterials(const TArray<FGardenGrassMeshEntry>& Entries);
    UMaterialInstanceDynamic* GetMeshMaterial(int32 Index) const;

    UFUNCTION(CallInEditor, Category="Grass Mask")
    void RefreshMask();

    /** Saves current mask textures/materials and disables live PCG and mask updates. Save the level after baking. */
    UFUNCTION(CallInEditor, Category="Grass Baking")
    void BakeGrass();

    UFUNCTION(CallInEditor, Category="Grass Baking")
    void UnbakeGrass();

    UPROPERTY(VisibleAnywhere, Category="Grass Baking")
    bool bGrassBaked = false;

    UPROPERTY(VisibleAnywhere, Category="Grass Baking")
    FString BakeStatus;

    UPROPERTY()
    bool bWasPCGActivated = true;

    UPROPERTY()
    uint8 PreviousGenerationTrigger = 0;

    UPROPERTY()
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> BakedGrassComponents;

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category="Grass Mask|Debug")
    FVector ValidationProbeWorld = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="Grass Mask|Debug")
    bool ReadValidationProbe = false;

    UPROPERTY(VisibleAnywhere, Category="Grass Mask|Debug")
    FString ValidationReport;
#endif

private:
    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> TopCapture;

    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> BottomCapture;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> TopDepth;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> BottomDepth;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> MaskMaterial;

    UPROPERTY(Transient)
    TObjectPtr<USceneCaptureComponent2D> IncludeCapture;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> IncludeDepth;

    TArray<FLinearColor> IncludePixels;
    FVector4 IncludeFrame = FVector4(0, 0, 1, 0);
    double CaptureHeight = 0;

    uint32 LastSignature = 0;
    bool bForceRefresh = true;

    TWeakObjectPtr<UPCGComponent> BoundPCGComponent;
    FDelegateHandle GenerationCompleteHandle;

    void BindGenerationComplete();
    void OnGenerationComplete(UPCGComponent* Component);
    void UpdateMask();
    void ReadProbe();
    void SyncMeshMaterials();

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> EntryMaterials;
};


/** Passes points through; the shader cuts only the parts of blades over the exclusion. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCG_API UGardenGrassMaskSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    UGardenGrassMaskSettings();

#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override
    {
        return TEXT("GardenGrassSilhouetteMask");
    }

    virtual FText GetDefaultNodeTitle() const override
    {
        return FText::FromString(TEXT("Garden Grass Silhouette Mask"));
    }

    virtual EPCGSettingsType GetType() const override
    {
        return EPCGSettingsType::PointOps;
    }
#endif

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    bool EnableExclusions = true;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    bool ExcludeFullFootprint = false;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FVector ContactProbeHalfSize = FVector(0, 0, 3);

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    bool EnableIncludedSurfaces = false;

    /** Sample all mesh faces, including vertical sides and undersides, inside the spline footprint. */
    UPROPERTY(EditAnywhere, Category="Surface Selection", meta=(PCG_Overridable))
    bool WrapEntireSurface = false;

    /** Grass samples per square meter of actual mesh surface when wrapping. */
    UPROPERTY(EditAnywhere, Category="Surface Selection", meta=(PCG_Overridable, ClampMin="0"))
    double SurfaceDensity = 100.0;

    /** Restrict wrapped roots to the area above the spline surface, or keep every face. */
    UPROPERTY(EditAnywhere, Category="Surface Selection", meta=(PCG_Overridable))
    EGardenGrassWrapCoverage WrapCoverage = EGardenGrassWrapCoverage::EntireSurface;

    /** Height above the spline surface at which wrapped planting starts, in centimeters. */
    UPROPERTY(EditAnywhere, Category="Surface Selection", meta=(PCG_Overridable, Units="cm"))
    double WrapHeightOffset = 0.0;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable, EditCondition="EnableIncludedSurfaces"))
    TArray<TSoftObjectPtr<AActor>> IncludedSurfaces;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable, EditCondition="EnableExclusions"))
    TArray<TSoftObjectPtr<AActor>> ExcludedActors;

    /** Project spline samples onto the chosen mesh surfaces, or world ground when inclusion is disabled. */
    UPROPERTY(EditAnywhere, Category="Projection", meta=(PCG_Overridable, DisplayName="Snap to Surface"))
    bool ProjectPointsToSurface = true;

    /** Orient grass to the surface normal independently of position snapping. */
    UPROPERTY(EditAnywhere, Category="Projection", meta=(PCG_Overridable))
    bool AlignToSurface = true;

    /** Maximum vertical distance from the sampled point to a surface, in centimeters. */
    UPROPERTY(EditAnywhere, Category="Projection", meta=(PCG_Overridable, ClampMin="0", Units="cm"))
    double SnapToSurfaceMaxDistance = 10000.0;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(ClampMin="256", ClampMax="4096"))
    int32 MaskResolution = 2048;

    UPROPERTY(EditAnywhere, Category="Settings")
    TSoftObjectPtr<UMaterialInterface> GrassMaterial;

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override
    {
        TArray<FPCGPinProperties> Pins = DefaultPointInputPinProperties();
        Pins.Emplace(TEXT("Surface"), EPCGDataType::Spatial);
        return Pins;
    }

    virtual TArray<FPCGPinProperties> OutputPinProperties() const override
    {
        return DefaultPointOutputPinProperties();
    }

    virtual FPCGElementPtr CreateElement() const override;
};

/** Applies optional random point transform ranges with a real enable/disable switch. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCG_API UGardenGrassRandomTransformSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    UGardenGrassRandomTransformSettings();

#if WITH_EDITOR
    virtual FName GetDefaultNodeName() const override
    {
        return TEXT("GardenGrassRandomTransform");
    }

    virtual FText GetDefaultNodeTitle() const override
    {
        return FText::FromString(TEXT("Garden Grass Random Transform"));
    }

    virtual EPCGSettingsType GetType() const override
    {
        return EPCGSettingsType::PointOps;
    }
#endif

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    bool EnableRandomTransform = true;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FVector OffsetMin = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FVector OffsetMax = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FRotator RotationMin = FRotator(0, 0, 0);

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FRotator RotationMax = FRotator(0, 360, 0);

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FVector ScaleMin = FVector(0.8, 0.8, 0.4);

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    FVector ScaleMax = FVector(1.1, 1.1, 0.6);

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override
    {
        return DefaultPointInputPinProperties();
    }

    virtual TArray<FPCGPinProperties> OutputPinProperties() const override
    {
        return DefaultPointOutputPinProperties();
    }

    virtual FPCGElementPtr CreateElement() const override;
};

/** Adds a per-point static mesh path used by the grass spawner's By Attribute selector. */
UCLASS(BlueprintType, ClassGroup=(Procedural))
class PCG_API UGardenGrassMeshSettings : public UPCGSettings
{
    GENERATED_BODY()

public:
    UGardenGrassMeshSettings();

#if WITH_EDITOR
    virtual TArray<FPCGSettingsOverridableParam> GatherOverridableParams() const override;
    virtual FName GetDefaultNodeName() const override
    {
        return TEXT("GardenGrassMeshSelector");
    }

    virtual FText GetDefaultNodeTitle() const override
    {
        return FText::FromString(TEXT("Garden Grass Mesh Selector"));
    }

    virtual EPCGSettingsType GetType() const override
    {
        return EPCGSettingsType::PointOps;
    }
#endif

    UPROPERTY(EditAnywhere, Category="Settings")
    FName MeshAttributeName = TEXT("GardenGrassMesh");

    UPROPERTY(EditAnywhere, Category="Settings", meta=(PCG_Overridable))
    TArray<FGardenGrassMeshEntry> GrassMeshes;

protected:
    virtual TArray<FPCGPinProperties> InputPinProperties() const override
    {
        return DefaultPointInputPinProperties();
    }

    virtual TArray<FPCGPinProperties> OutputPinProperties() const override
    {
        return DefaultPointOutputPinProperties();
    }

    virtual FPCGElementPtr CreateElement() const override;
};
