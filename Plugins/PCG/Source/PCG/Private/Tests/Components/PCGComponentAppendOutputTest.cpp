// Copyright Epic Games, Inc. All Rights Reserved.

#include "PCGComponent.h"
#include "Data/Tool/PCGToolSplineData.h"
#include "Helpers/PCGHelpers.h"
#include "Tests/PCGTestsCommon.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SplineComponent.h"

#if WITH_EDITOR

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(
	FPCGComponentBakeGeneratedComponentsInPlaceTest,
	FPCGTestBaseClass,
	"Plugins.PCG.Component.AppendOutput.BakeGeneratedComponentsInPlace",
	PCGTestsCommon::TestFlags)

bool FPCGComponentBakeGeneratedComponentsInPlaceTest::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGComponent* PCGComponent = TestData.TestPCGComponent;
	AActor* Owner = TestData.TestActor;
	UTEST_NOT_NULL("PCG component exists", PCGComponent);
	UTEST_NOT_NULL("Owner exists", Owner);

	auto CreateManagedISM = [Owner, PCGComponent](FName ComponentName)
	{
		UInstancedStaticMeshComponent* ISMComponent = NewObject<UInstancedStaticMeshComponent>(Owner, ComponentName, RF_Transient | RF_Transactional);
		ISMComponent->SetupAttachment(Owner->GetRootComponent());
		Owner->AddInstanceComponent(ISMComponent);
		ISMComponent->RegisterComponent();
		ISMComponent->ComponentTags.Add(PCGHelpers::MarkedForCleanupPCGTag);
		PCGComponent->AddComponentsToManagedResources({ ISMComponent });
		return ISMComponent;
	};
	auto IsManaged = [PCGComponent](const UObject* Object)
	{
		const UObject* Objects[] = { Object };
		return PCGComponent->IsAnyObjectManagedByResource(MakeArrayView(Objects));
	};

	UInstancedStaticMeshComponent* FirstISM = CreateManagedISM(TEXT("ISM_Grass_A"));
	UTEST_TRUE("First ISM starts managed", IsManaged(FirstISM));
	UTEST_TRUE("First ISM starts transient", FirstISM->HasAnyFlags(RF_Transient));

	UTEST_TRUE("Existing generated ISM was baked", PCGComponent->BakeGeneratedComponentsInPlace());
	UTEST_TRUE("Baked ISM remains valid", IsValid(FirstISM));
	UTEST_EQUAL("Baked ISM remains on the same actor", FirstISM->GetOwner(), Owner);
	UTEST_FALSE("Baked ISM is persistent", FirstISM->HasAnyFlags(RF_Transient));
	UTEST_FALSE("Baked ISM no longer has the PCG ownership tag", FirstISM->ComponentHasTag(PCGHelpers::DefaultPCGTag));
	UTEST_FALSE("Baked ISM no longer has the cleanup tag", FirstISM->ComponentHasTag(PCGHelpers::MarkedForCleanupPCGTag));
	UTEST_FALSE("Baked ISM is no longer managed", IsManaged(FirstISM));

	UInstancedStaticMeshComponent* SecondISM = CreateManagedISM(TEXT("ISM_Grass_B"));
	UTEST_TRUE("New ISM is independently managed", IsManaged(SecondISM));
	UTEST_FALSE("Previous baked ISM stays unmanaged", IsManaged(FirstISM));

	PCGComponent->CleanupLocalImmediate(/*bRemoveComponents=*/true);
	UTEST_TRUE("Cleaning the current generation does not delete the baked ISM", IsValid(FirstISM));

	return true;
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(
	FPCGSplineSurfaceAppendSessionTest,
	FPCGTestBaseClass,
	"Plugins.PCG.Component.AppendOutput.SplineSurfaceSession",
	PCGTestsCommon::TestFlags)

bool FPCGSplineSurfaceAppendSessionTest::RunTest(const FString& Parameters)
{
	PCGTestsCommon::FTestData TestData;
	UPCGComponent* PCGComponent = TestData.TestPCGComponent;
	AActor* Owner = TestData.TestActor;
	auto IsManaged = [PCGComponent](const UObject* Object)
	{
		const UObject* Objects[] = { Object };
		return PCGComponent->IsAnyObjectManagedByResource(MakeArrayView(Objects));
	};

	FPCGInteractiveToolWorkingDataContext Context;
	Context.OwningPCGComponent = PCGComponent;
	Context.OwningActor = Owner;
	Context.WorkingDataIdentifier = TEXT("SplineSurfaceTool.Default");

	FPCGInteractiveToolWorkingData_SplineSurface WorkingData;
	WorkingData.Initialize(Context);
	USplineComponent* FirstSpline = WorkingData.GetSplineComponent();
	UTEST_NOT_NULL("First spline was created", FirstSpline);
	UTEST_TRUE("First spline is a closed surface", FirstSpline->IsClosedLoop());

	WorkingData.OnToolStart(Context);
	UInstancedStaticMeshComponent* FirstISM = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("ISM_FirstAcceptedGrass"), RF_Transient | RF_Transactional);
	FirstISM->SetupAttachment(Owner->GetRootComponent());
	Owner->AddInstanceComponent(FirstISM);
	FirstISM->RegisterComponent();
	PCGComponent->AddComponentsToManagedResources({ FirstISM });
	WorkingData.OnToolApply(Context);
	UTEST_FALSE("Accepted spline is persistent", FirstSpline->HasAnyFlags(RF_Transient));
	UTEST_FALSE("Accept immediately persists the first ISM", FirstISM->HasAnyFlags(RF_Transient));
	UTEST_FALSE("Accepted first ISM is detached from PCG ownership", IsManaged(FirstISM));

	WorkingData.OnToolStart(Context);
	USplineComponent* AppendedSpline = WorkingData.GetSplineComponent();
	UTEST_NOT_NULL("Append session created a spline", AppendedSpline);
	UTEST_NOT_EQUAL("Append session does not reuse the accepted spline", AppendedSpline, FirstSpline);
	UTEST_TRUE("Appended spline is a closed surface", AppendedSpline->IsClosedLoop());
	UTEST_FALSE("Starting append preserves the first ISM", FirstISM->HasAnyFlags(RF_Transient));
	UTEST_FALSE("Starting append does not reclaim the first ISM", IsManaged(FirstISM));

	TArray<USplineComponent*> ActorSplines;
	Owner->GetComponents<USplineComponent>(ActorSplines, false);
	UTEST_EQUAL("Both surface splines coexist during append", ActorSplines.Num(), 2);

	WorkingData.OnToolCancel(Context);
	UTEST_EQUAL("Cancel restores the prior accepted spline", WorkingData.GetSplineComponent(), FirstSpline);
	UTEST_TRUE("Cancel keeps the accepted spline valid", IsValid(FirstSpline));
	UTEST_TRUE("Cancel keeps the previously baked ISM valid", IsValid(FirstISM));

	WorkingData.OnToolStart(Context);
	USplineComponent* SecondAcceptedSpline = WorkingData.GetSplineComponent();
	UInstancedStaticMeshComponent* SecondISM = NewObject<UInstancedStaticMeshComponent>(Owner, TEXT("ISM_SecondAcceptedGrass"), RF_Transient | RF_Transactional);
	SecondISM->SetupAttachment(Owner->GetRootComponent());
	Owner->AddInstanceComponent(SecondISM);
	SecondISM->RegisterComponent();
	PCGComponent->AddComponentsToManagedResources({ SecondISM });
	WorkingData.OnToolApply(Context);
	UTEST_NOT_NULL("Second accepted spline exists", SecondAcceptedSpline);
	UTEST_NOT_EQUAL("Second accepted spline is independent", SecondAcceptedSpline, FirstSpline);
	UTEST_FALSE("Second accepted spline is persistent", SecondAcceptedSpline->HasAnyFlags(RF_Transient));
	UTEST_FALSE("Second accepted ISM is persistent", SecondISM->HasAnyFlags(RF_Transient));
	UTEST_FALSE("Second accepted ISM is detached from PCG ownership", IsManaged(SecondISM));
	UTEST_TRUE("Accepting the second stroke preserves the first ISM", IsValid(FirstISM));
	UTEST_TRUE("Both accepted ISMs remain registered", FirstISM->IsRegistered() && SecondISM->IsRegistered());

	Owner->GetComponents<USplineComponent>(ActorSplines, false);
	UTEST_EQUAL("Two accepted surface splines coexist", ActorSplines.Num(), 2);

	return true;
}

#endif // WITH_EDITOR
