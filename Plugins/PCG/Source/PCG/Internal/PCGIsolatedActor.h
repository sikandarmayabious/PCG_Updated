// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGData.h"

#include "GameFramework/Actor.h"

#include "PCGIsolatedActor.generated.h"

/** 
* Simnple actor wrapper class to store PCG generated data.
*/
UCLASS(MinimalAPI, NotPlaceable)
class APCGIsolatedActor : public AActor
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleInstanceOnly, Category = PCG)
	FPCGDataCollection Data;

	UPROPERTY(VisibleInstanceOnly, Category = PCG)
	TSoftObjectPtr<AActor> Origin = nullptr;
};