// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Metadata/PCGCreateComplexConstantElement.h"

#include "PCGContext.h"
#include "PCGParamData.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/Accessors/PCGAttributeAccessorHelpers.h"

#define LOCTEXT_NAMESPACE "PCGCreateConstantElement"

#if WITH_EDITOR
FName UPCGCreateComplexConstantSettings::GetDefaultNodeName() const
{
	return "CreateComplexConstant";
}

FText UPCGCreateComplexConstantSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("CreateNodeTitle", "Create Complex Constant");
}

FText UPCGCreateComplexConstantSettings::GetNodeTooltipText() const
{
	return LOCTEXT("CreateNodeTooltip", "Create a new attribute set containing the content of the Properties structure.");
}

#endif // WITH_EDITOR

FPCGElementPtr UPCGCreateComplexConstantSettings::CreateElement() const
{
	return MakeShared<FPCGCreateComplexConstantElement>();
}

bool UPCGCreateComplexConstantSettings::HasDynamicPins() const
{
	return true;
}

FPCGDataTypeIdentifier UPCGCreateComplexConstantSettings::GetCurrentPinTypesID(const UPCGPin* InPin) const
{
	if (InPin && InPin->Properties.Label == PCGPinConstants::DefaultOutputLabel)
	{
		const UPCGNode* Node = Cast<const UPCGNode>(GetOuter());
		if (!Node || !Node->IsInputPinConnected(PCGPinConstants::DefaultInputLabel))
		{
			FPCGDataTypeIdentifier Id = FPCGDataTypeIdentifier::Construct<UPCGParamData>();
			
			// If we have just a single property in the bag, we can try to color the pin with the property type color.
			const UPropertyBag* PropertyBag = Properties.GetPropertyBagStruct();
			if (PropertyBag && PropertyBag->GetPropertyDescs().Num() == 1)
			{
				const FPropertyBagPropertyDesc& Desc = PropertyBag->GetPropertyDescs()[0];
				if (Desc.ContainerTypes.IsEmpty() || Desc.ContainerTypes[0] != EPropertyBagContainerType::Map)
				{
					// @todo_pcg: To be removed when we support Enums and Bytes natively with accessors
					if (Desc.ValueType == EPropertyBagPropertyType::Byte)
					{
						Id.CustomSubtype = static_cast<int32>(EPCGMetadataTypes::Integer32);
					}
					else if (Desc.ValueType == EPropertyBagPropertyType::Enum)
					{
						Id.CustomSubtype = static_cast<int32>(EPCGMetadataTypes::Integer64);
					}
					else
					{
						Id.CustomSubtype = static_cast<int32>(FPCGMetadataAttributeDesc::CreateFromProperty(Desc.CachedProperty).ValueType);
					}
				}
			}
			
			return Id;
		}
	}
	
	return Super::GetCurrentPinTypesID(InPin);
}

FString UPCGCreateComplexConstantSettings::GetAdditionalTitleInformation() const
{
	TStringBuilder<256> Title;
	if (TargetMetadataDomain != NAME_None)
	{
		Title += TEXT("@");
		Title += TargetMetadataDomain.ToString();
	}
	
	// If we have just a single property in the bag, we can add its name
	const UPropertyBag* PropertyBag = Properties.GetPropertyBagStruct();
	if (PropertyBag && PropertyBag->GetPropertyDescs().Num() == 1)
	{
		if (TargetMetadataDomain != NAME_None)
		{
			Title += TEXT(".");
		}
		
		Title += PropertyBag->GetPropertyDescs()[0].Name.ToString();
	}
	
	return Title.ToString();
}

TArray<FPCGPinProperties> UPCGCreateComplexConstantSettings::InputPinProperties() const
{
	return {};
}

TArray<FPCGPinProperties> UPCGCreateComplexConstantSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Result;
	Result.Emplace(
		PCGPinConstants::DefaultOutputLabel, 
		FPCGDataTypeIdentifier::Construct<UPCGParamData>(), 
		/*bAllowMultipleConnections=*/true, 
		/*bAllowMultipleData=*/false);
	
	return Result;
}

#if WITH_EDITOR
FName UPCGAddComplexConstantSettings::GetDefaultNodeName() const
{
	return "AddComplexConstant";
}

FText UPCGAddComplexConstantSettings::GetDefaultNodeTitle() const
{
	return LOCTEXT("AddNode", "Add Complex Constant");
}

FText UPCGAddComplexConstantSettings::GetNodeTooltipText() const
{
	return LOCTEXT("AddNodeTooltip", "Add new attributes from the content of the Properties structure.");
}
#endif // WITH_EDITOR

TArray<FPCGPinProperties> UPCGAddComplexConstantSettings::InputPinProperties() const
{
	TArray<FPCGPinProperties> Result;
	FPCGPinProperties& PinProperties = Result.Emplace_GetRef(
		PCGPinConstants::DefaultInputLabel, 
		FPCGDataTypeIdentifier::Construct<UPCGData>(), 
		/*bAllowMultipleConnections=*/true, 
		/*bAllowMultipleData=*/true);
	
	PinProperties.SetRequiredPin();
	
	return Result;
}

TArray<FPCGPinProperties> UPCGAddComplexConstantSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> Result;
	Result.Emplace(
		PCGPinConstants::DefaultOutputLabel, 
		FPCGDataTypeIdentifier::Construct<UPCGData>(), 
		/*bAllowMultipleConnections=*/true, 
		/*bAllowMultipleData=*/true);
	
	return Result;
}

bool FPCGCreateComplexConstantElement::ExecuteInternal(FPCGContext* InContext) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGCreateConstantElement::Execute);

	check(InContext);

	const UPCGCreateComplexConstantSettings* Settings = InContext->GetInputSettings<UPCGCreateComplexConstantSettings>();
	check(Settings);
	
	const UPropertyBag* PropertyBag = Settings->Properties.GetPropertyBagStruct();
	if (!PropertyBag || PropertyBag->GetPropertyDescs().IsEmpty())
	{
		return true;
	}
	
	bool bHasInput = true;
	
	TArray<FPCGTaggedData> Outputs;
	if (InContext->Node && InContext->Node->IsInputPinConnected(PCGPinConstants::DefaultInputLabel))
	{
		Algo::Transform(InContext->InputData.GetInputsByPin(PCGPinConstants::DefaultInputLabel), Outputs, [InContext](const FPCGTaggedData& TaggedData)
		{
			FPCGTaggedData OutTaggedData = TaggedData;
			OutTaggedData.Data = TaggedData.Data ? TaggedData.Data->DuplicateData(InContext) : nullptr;
			return OutTaggedData;
		});
	}
	else
	{
		UPCGParamData* ParamData = FPCGContext::NewObject_AnyThread<UPCGParamData>(InContext);
		check(ParamData);
		Outputs.Emplace_GetRef().Data = ParamData;
		bHasInput = false;
	}
	
	for (FPCGTaggedData& Output : Outputs)
	{
		if (!Output.Data)
		{
			continue;
		}
		
		UPCGData* OutputData = const_cast<UPCGData*>(Output.Data.Get());
		
		FPCGAttributePropertySelector Selector = FPCGAttributePropertySelector::CreateAttributeSelector(NAME_None, Settings->TargetMetadataDomain);
		const FPCGMetadataDomainID DomainID = OutputData->GetMetadataDomainIDFromSelector(Selector);
		FPCGMetadataDomain* Domain = OutputData->MutableMetadata()->GetMetadataDomain(DomainID);
		
		if (!Domain)
		{
			continue;
		}
		
		if (!bHasInput)
		{
			Domain->AddEntry();
		}
		
		const void* SrcPtr =Settings->Properties.GetValue().GetMemory();
		const FPCGAttributeAccessorKeysGenericPtrs PropertyKeys(MakeArrayView(&SrcPtr, 1));
		FPCGAttributeAccessorKeysEntries AttributeKeys(PCGInvalidEntryKey);
	
		for (const FPropertyBagPropertyDesc& Desc : PropertyBag->GetPropertyDescs())
		{
			const FProperty* Property = Desc.CachedProperty;
			check(Property);

			// @todo_pcg: To be removed when we support Enums and Bytes natively with accessors
			FPCGMetadataAttributeDesc AttributeDesc{};
			bool bUseGenericAccessors = true;
			const bool bSingleValue = Desc.ContainerTypes.IsEmpty();
			if (bSingleValue && Property->IsA<FByteProperty>())
			{
				AttributeDesc = PCG::Private::MakeAttributeDesc<int32>(Desc.Name);
				bUseGenericAccessors = false;
			}
			else if (bSingleValue && Property->IsA<FEnumProperty>())
			{
				AttributeDesc = PCG::Private::MakeAttributeDesc<int64>(Desc.Name);
				bUseGenericAccessors = false;
			}
			else
			{
				AttributeDesc = FPCGMetadataAttributeDesc::CreateFromProperty(Property);
			}

			if (!AttributeDesc.IsValid())
			{
				continue;
			}
			
			const TUniquePtr<IPCGAttributeAccessor> PropertyAccessor = PCGAttributeAccessorHelpers::CreatePropertyAccessor(Property, /*bUseGenericAccessor=*/bUseGenericAccessors);
			Selector.SetAttributeName(Desc.Name);			
			PCGAttributeAccessorHelpers::FPCGCreateAccessorWithAttributeCreationParams Params =
			{
				.InData = OutputData,
				.InSelector = &Selector,
				.InExpectedDesc = AttributeDesc,
			};

			TUniquePtr<IPCGAttributeAccessor> AttributeAccessor = PCGAttributeAccessorHelpers::CreateAccessorWithAttributeCreation(Params);
			if (!AttributeAccessor)
			{
				PCGLog::Metadata::LogFailToCreateAttributeError(AttributeDesc);
				continue;
			}
			
			PropertyAccessor->CopyTo(PropertyKeys, *AttributeAccessor, AttributeKeys, /*Index=*/0, /*Count=*/1, EPCGAttributeAccessorFlags::AllowBroadcastAndConstructible | EPCGAttributeAccessorFlags::AllowSetDefaultValue);
		}
		
		InContext->OutputData.TaggedData.Emplace(Output);
	}

	return true;
}

bool FPCGCreateComplexConstantElement::SupportsBasePointDataInputs(FPCGContext* InContext) const
{
	return true;
}

EPCGElementExecutionLoopMode FPCGCreateComplexConstantElement::ExecutionLoopMode(const UPCGSettings* Settings) const
{
	return EPCGElementExecutionLoopMode::SinglePrimaryPin;
}

#undef LOCTEXT_NAMESPACE
