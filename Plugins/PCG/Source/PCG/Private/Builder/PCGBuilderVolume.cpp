// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builder/PCGBuilderVolume.h"
#include "Engine/CollisionProfile.h"
#include "Components/BrushComponent.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PCGBuilderVolume)

APCGBuilderVolume::APCGBuilderVolume()
	: Super(FObjectInitializer::Get().DoNotCreateDefaultSubobject(TEXT("Sprite")))
{
#if WITH_EDITORONLY_DATA
	bIsSpatiallyLoaded = false;
#endif

	GetBrushComponent()->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
}
