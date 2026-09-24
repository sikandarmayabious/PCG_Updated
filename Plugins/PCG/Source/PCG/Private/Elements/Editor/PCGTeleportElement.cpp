// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Editor/PCGTeleportElement.h"

#include "PCGContext.h"
#include "PCGPin.h"
#include "Metadata/Accessors/IPCGAttributeAccessor.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"
#include "Metadata/Accessors/PCGAttributeAccessorKeys.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Templates/UniquePtr.h"

#if WITH_EDITOR
#include "ScopedTransaction.h"
#endif

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGTeleportElement)

#define LOCTEXT_NAMESPACE "PCGTeleportElement"

UPCGTeleportSettings::UPCGTeleportSettings()
{
	TransformAttribute.SetPointProperty(EPCGPointProperties::Transform);
}

TArray<FPCGPinProperties> UPCGTeleportSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace_GetRef(PCGPinConstants::DefaultInputLabel, EPCGDataType::Point | EPCGDataType::Param).SetRequiredPin();
	return PinProperties;
}

TArray<FPCGPinProperties> UPCGTeleportSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	FPCGPinProperties& DepPin = PinProperties.Emplace_GetRef(PCGPinConstants::DefaultExecutionDependencyLabel, EPCGDataType::Any);
#if WITH_EDITOR
	DepPin.Tooltip = PCGPinConstants::Tooltips::ExecutionDependencyTooltip;
#endif // WITH_EDITOR
	DepPin.Usage = EPCGPinUsage::DependencyOnly;

	return PinProperties;
}

FPCGElementPtr UPCGTeleportSettings::CreateElement() const
{
	return MakeShared<FPCGTeleportElement>();
}

bool FPCGTeleportElement::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGTeleportElement::Execute);
	check(Context);

	const UPCGTeleportSettings* Settings = Context->GetInputSettings<UPCGTeleportSettings>();
	check(Settings);

	const TArray<FPCGTaggedData> InputTaggedData = Context->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel);

	// Build one transform accessor per input data element, so we can look up per-entry transforms cheaply.
	struct FObjectTransformAccessors
	{
		TUniquePtr<const IPCGAttributeAccessor> TransformAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> TransformKeys;
		TUniquePtr<const IPCGAttributeAccessor> ObjectAccessor;
		TUniquePtr<const IPCGAttributeAccessorKeys> ObjectKeys;
	};

	TArray<FObjectTransformAccessors> ObjectTransformAccessors;
	ObjectTransformAccessors.Reserve(InputTaggedData.Num());

	for (int32 DataIndex = 0; DataIndex < InputTaggedData.Num(); ++DataIndex)
	{
		FObjectTransformAccessors DataAccessors;
		
		const UPCGData* Data = InputTaggedData[DataIndex].Data;
		if (!Data)
		{
			continue;
		}

		// Get transform accessor
		{
			const FPCGAttributePropertyInputSelector FixedSelector = Settings->TransformAttribute.CopyAndFixLast(Data);
			DataAccessors.TransformAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, FixedSelector);
			DataAccessors.TransformKeys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, FixedSelector);

			if (!DataAccessors.TransformAccessor.IsValid() || !DataAccessors.TransformKeys.IsValid())
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("TransformAttributeNotFound", "Transform attribute '{0}' not found on input data at index {1}."), FText::FromName(FixedSelector.GetName()), DataIndex));
				continue;
			}
		}

		// Get object accessor
		{
			const FPCGAttributePropertyInputSelector FixedSelector = Settings->ObjectReferenceAttribute.CopyAndFixLast(Data);
			DataAccessors.ObjectAccessor = PCGAttributeAccessorHelpers::CreateConstAccessor(Data, FixedSelector);
			DataAccessors.ObjectKeys = PCGAttributeAccessorHelpers::CreateConstKeys(Data, FixedSelector);

			if (!DataAccessors.ObjectAccessor.IsValid() || !DataAccessors.ObjectKeys.IsValid())
			{
				PCGE_LOG(Error, GraphAndLog, FText::Format(LOCTEXT("ObjectReferenceAttributeNotFound", "Object reference attribute '{0}' not found on input data at index {1}."), FText::FromName(FixedSelector.GetName()), DataIndex));
				continue;
			}
		}

		ObjectTransformAccessors.Add(MoveTemp(DataAccessors));
	}

	TArray<FSoftObjectPath> ObjectPaths;
	TArray<FTransform> ObjectTransforms;

#if WITH_EDITOR
	FScopedTransaction Transaction(LOCTEXT("TeleportActorsTransaction", "Teleporting actors using PCG"), Context->ExecutionSource.Get() && Context->ExecutionSource->GetExecutionState().UseTransactions());
#endif

	for(FObjectTransformAccessors& ObjectTransformAccessor : ObjectTransformAccessors)
	{
		check(ObjectTransformAccessor.TransformAccessor.IsValid() && ObjectTransformAccessor.TransformKeys.IsValid() && ObjectTransformAccessor.ObjectAccessor.IsValid() && ObjectTransformAccessor.ObjectKeys.IsValid());

		// Gather data
		ObjectPaths.Reset();
		if (!PCGAttributeAccessorHelpers::ExtractAllValues(ObjectTransformAccessor.ObjectAccessor.Get(), ObjectTransformAccessor.ObjectKeys.Get(), ObjectPaths))
		{
			continue;
		}

		ObjectTransforms.Reset();
		if (!PCGAttributeAccessorHelpers::ExtractAllValues(ObjectTransformAccessor.TransformAccessor.Get(), ObjectTransformAccessor.TransformKeys.Get(), ObjectTransforms))
		{
			continue;
		}

		// While we "technically" deal with having 1:N or N:1 cases, really it doesn't make a lot of sense for this node to work this way.
		if (ObjectPaths.Num() != ObjectTransforms.Num())
		{
			PCGE_LOG(Error, GraphAndLog, LOCTEXT("InvalidPathToTransformCardinality", "Expected the same number of paths as transforms. This data will be skipped."));
			continue;
		}

		// Teleport objects
		int32 EmptyOrNotLoadedCount = 0;

		for (int32 ElementIndex = 0; ElementIndex < ObjectPaths.Num(); ++ElementIndex)
		{
			const FSoftObjectPath& ObjectPath = ObjectPaths[ElementIndex];
			const FTransform& ObjectTransform = ObjectTransforms[ElementIndex];

			UObject* ResolvedObject = ObjectPath.ResolveObject();
			if (!ResolvedObject)
			{
				++EmptyOrNotLoadedCount;
				continue;
			}

			if (AActor* Actor = Cast<AActor>(ResolvedObject))
			{
				Actor->Modify();
				Actor->SetActorTransform(ObjectTransform, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);
			}
			else if (USceneComponent* SceneComponent = Cast<USceneComponent>(ResolvedObject))
			{
				SceneComponent->Modify();
				SceneComponent->SetWorldTransform(ObjectTransform, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);
			}
			else
			{
				PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("ObjectNotActorOrComponent", "Object at path '{0}' is neither an Actor nor a SceneComponent and cannot be teleported."), FText::FromString(ObjectPath.ToString())));
			}
		}

		// Finally, log a warning (unless muted) for unloaded objects or invalid paths.
		if (!Settings->bSilenceWarningOnUnresolvedPath && EmptyOrNotLoadedCount > 0)
		{
			PCGE_LOG(Warning, GraphAndLog, FText::Format(LOCTEXT("UnresolvedObjectPaths", "Encountered '{0}' empty or unloaded object paths, that were skipped."), EmptyOrNotLoadedCount));
		}
	}

	return true;
}

#undef LOCTEXT_NAMESPACE
