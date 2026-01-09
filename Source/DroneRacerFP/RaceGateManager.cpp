#include "RaceGateManager.h"
#include "RaceGate.h"

ARaceGateManager::ARaceGateManager()
{
    PrimaryActorTick.bCanEverTick = false;
}

void ARaceGateManager::BeginPlay()
{
    Super::BeginPlay();

    // Deactivate all gates
    for (ARaceGate* Gate : Gates)
    {
        if (Gate)
            Gate->DeactivateGate();
    }

    // Start with the first gate
    CurrentIndex = 0;

    if (Gates.IsValidIndex(CurrentIndex))
        Gates[CurrentIndex]->ActivateGate();
}

void ARaceGateManager::GatePassed(ARaceGate* PassedGate)
{
    if (!PassedGate)
        return;

    int32 PassedIndex = Gates.IndexOfByKey(PassedGate);

    // Wrong gate
    if (PassedIndex != CurrentIndex)
        return;

    // Correct gate → deactivate it
    PassedGate->DeactivateGate();

    // Move to next gate
    CurrentIndex++;

    if (Gates.IsValidIndex(CurrentIndex))
    {
        Gates[CurrentIndex]->ActivateGate();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("RACE COMPLETE!"));
    }
}
