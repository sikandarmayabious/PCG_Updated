#include "Elements/GardenGrassMask.h"
#include "PCGComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#if WITH_EDITOR
#include "Materials/MaterialInstanceConstant.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#endif

void UGardenGrassMaskComponent::BakeGrass()
{
#if WITH_EDITOR
    if (bGrassBaked || !GetOwner() || !GetWorld() || GetWorld()->IsGameWorld()) return;
    UPCGComponent* PCG = GetOwner()->FindComponentByClass<UPCGComponent>();
    if (!PCG || PCG->IsGenerating() || PCG->IsPartitioned())
    {
        BakeStatus = TEXT("Finish generation first. Bake currently supports non-partitioned grass areas only.");
        return;
    }
    RefreshMask();
    TInlineComponentArray<UInstancedStaticMeshComponent*> Components(GetOwner());
    Components.RemoveAll([](UInstancedStaticMeshComponent* ISM)
    {
        return ISM->GetInstanceCount() == 0 || !ISM->ComponentTags.ContainsByPredicate(
            [](FName Tag) { return Tag.ToString().StartsWith(TEXT("GardenGrassEntry=")); });
    });
    if (Components.IsEmpty())
    {
        BakeStatus = TEXT("No generated garden grass to bake. Generate the area first.");
        return;
    }
    const FString Folder = TEXT("/Game/GardenGrassBakes/Bake_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    auto Save = [](UObject* Asset)
    {
        FAssetRegistryModule::AssetCreated(Asset);
        Asset->MarkPackageDirty();
        FSavePackageArgs Args;
        Args.TopLevelFlags = RF_Public | RF_Standalone;
        return UPackage::SavePackage(Asset->GetOutermost(), Asset,
            *FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension()), Args);
    };
    TMap<UTexture*, UTexture*> Textures;
    TMap<UMaterialInterface*, UMaterialInterface*> Materials;
    for (UInstancedStaticMeshComponent* ISM : Components)
    {
        for (int32 Slot = 0; Slot < ISM->GetNumMaterials(); ++Slot)
        {
            UMaterialInterface* Source = ISM->GetMaterial(Slot);
            if (!Source || Materials.Contains(Source)) continue;
            const FString Name = FString::Printf(TEXT("MI_Grass_%d"), Materials.Num());
            UMaterialInstanceConstant* Material = NewObject<UMaterialInstanceConstant>(
                CreatePackage(*(Folder / Name)), *Name, RF_Public | RF_Standalone);
            Material->SetParentEditorOnly(GrassMaterial);
            TArray<FMaterialParameterInfo> Infos;
            TArray<FGuid> Ids;
            Source->GetAllScalarParameterInfo(Infos, Ids);
            for (const auto& Info : Infos)
            {
                float Value;
                if (Source->GetScalarParameterValue(Info, Value)) Material->SetScalarParameterValueEditorOnly(Info, Value);
            }
            Infos.Reset(); Ids.Reset();
            Source->GetAllVectorParameterInfo(Infos, Ids);
            for (const auto& Info : Infos)
            {
                FLinearColor Value;
                if (Source->GetVectorParameterValue(Info, Value)) Material->SetVectorParameterValueEditorOnly(Info, Value);
            }
            Infos.Reset(); Ids.Reset();
            Source->GetAllTextureParameterInfo(Infos, Ids);
            for (const auto& Info : Infos)
            {
                UTexture* Value = nullptr;
                if (!Source->GetTextureParameterValue(Info, Value) || !Value) continue;
                if (UTextureRenderTarget2D* RT = Cast<UTextureRenderTarget2D>(Value))
                {
                    if (!Textures.Contains(RT))
                    {
                        const FString TextureName = FString::Printf(TEXT("T_Mask_%d"), Textures.Num());
                        UTexture2D* Texture = RT->ConstructTexture2D(CreatePackage(*(Folder / TextureName)), TextureName,
                            RF_Public | RF_Standalone, 0);
                        if (!Texture) { BakeStatus = TEXT("Mask conversion failed; live grass was retained."); return; }
                        Texture->SRGB = false;
                        Texture->CompressionSettings = TC_SingleFloat;
                        Texture->MipGenSettings = TMGS_NoMipmaps;
                        Texture->Filter = TF_Nearest;
                        Texture->AddressX = Texture->AddressY = TA_Clamp;
                        Texture->NeverStream = true;
                        Texture->PostEditChange();
                        if (!Save(Texture)) { BakeStatus = TEXT("Mask save failed; live grass was retained."); return; }
                        Textures.Add(RT, Texture);
                    }
                    Value = Textures[RT];
                }
                Material->SetTextureParameterValueEditorOnly(Info, Value);
            }
            Material->PostEditChange();
            if (!Save(Material)) { BakeStatus = TEXT("Material save failed; live grass was retained."); return; }
            Materials.Add(Source, Material);
        }
    }
    Modify();
    PCG->Modify();
    bWasPCGActivated = PCG->bActivated;
    PreviousGenerationTrigger = static_cast<uint8>(PCG->GenerationTrigger);
    PCG->bActivated = false;
    PCG->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;
    int64 Instances = 0;
    for (UInstancedStaticMeshComponent* ISM : Components)
    {
        UInstancedStaticMeshComponent* Baked = DuplicateObject<UInstancedStaticMeshComponent>(ISM, GetOwner(),
            MakeUniqueObjectName(GetOwner(), ISM->GetClass(), TEXT("BakedGardenGrass")));
        Baked->ClearFlags(RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
        Baked->SetFlags(RF_Transactional);
        Baked->ComponentTags.Reset();
        Baked->ComponentTags.Add(TEXT("BakedGardenGrass"));
        GetOwner()->AddInstanceComponent(Baked);
        Instances += ISM->GetInstanceCount();
        for (int32 Slot = 0; Slot < ISM->GetNumMaterials(); ++Slot)
            if (UMaterialInterface** Material = Materials.Find(ISM->GetMaterial(Slot))) Baked->SetMaterial(Slot, *Material);
        Baked->RegisterComponent();
        BakedGrassComponents.Add(Baked);
    }
    bGrassBaked = true;
    // Copies are ordinary actor instance components, outside PCG managed resources.
    // PCG cleanup or invalidation must never remove the baked instances.
    PCG->CleanupLocalImmediate(true);
    SetComponentTickEnabled(false);
    if (BoundPCGComponent.IsValid()) BoundPCGComponent->OnPCGGraphGeneratedDelegate.Remove(GenerationCompleteHandle);
    GenerationCompleteHandle.Reset();
    for (USceneCaptureComponent2D* Capture : {TopCapture.Get(), BottomCapture.Get(), IncludeCapture.Get()})
        if (Capture) Capture->DestroyComponent();
    TopCapture = BottomCapture = IncludeCapture = nullptr;
    TopDepth = BottomDepth = IncludeDepth = nullptr;
    IncludePixels.Empty();
    EntryMaterials.Empty();
    MaskMaterial = nullptr;
    BakeStatus = FString::Printf(TEXT("Baked %lld instances. Save the level. Assets: %s. Unbake before editing."), Instances, *Folder);
    GetOwner()->MarkPackageDirty();
#endif
}

void UGardenGrassMaskComponent::UnbakeGrass()
{
#if WITH_EDITOR
    if (!bGrassBaked || !GetOwner() || !GetWorld() || GetWorld()->IsGameWorld()) return;
    Modify();
    for (UInstancedStaticMeshComponent* Baked : BakedGrassComponents)
        if (IsValid(Baked)) { GetOwner()->RemoveInstanceComponent(Baked); Baked->DestroyComponent(); }
    BakedGrassComponents.Reset();
    if (UPCGComponent* PCG = GetOwner()->FindComponentByClass<UPCGComponent>())
    {
        PCG->Modify();
        PCG->GenerationTrigger = static_cast<EPCGComponentGenerationTrigger>(PreviousGenerationTrigger);
        PCG->bActivated = bWasPCGActivated;
    }
    bGrassBaked = false;
    SetComponentTickEnabled(true);
    RefreshMask();
    if (UPCGComponent* PCG = GetOwner()->FindComponentByClass<UPCGComponent>())
        if (PCG->bActivated) PCG->GenerateLocal(true);
    BakeStatus = TEXT("Live grass restored. Previously baked assets are retained for safe cleanup.");
    GetOwner()->MarkPackageDirty();
#endif
}
