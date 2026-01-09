#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceGateManager.generated.h"

class ARaceGate;

// Manages an ordered list of gates.
// When the drone passes the correct one, activates the next.
UCLASS()
class DRONERACERFP_API ARaceGateManager : public AActor
{
    GENERATED_BODY()

public:
    ARaceGateManager();

protected:
    virtual void BeginPlay() override;

public:

    // Ordered list of gates, assigned in BP_RaceGateManager
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<ARaceGate*> Gates;

    // Index of current active gate
    int32 CurrentIndex = 0;

    // Called by a gate when passed
    void GatePassed(ARaceGate* PassedGate);
};
