#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RaceGate.generated.h"

class UStaticMeshComponent;
class UBoxComponent;
class ARaceGateManager;

// A single race gate the drone must fly through in order.
UCLASS()
class DRONERACERFP_API ARaceGate : public AActor
{
    GENERATED_BODY()

public:
    ARaceGate();

protected:
    virtual void BeginPlay() override;

public:

    // Static mesh for the gate
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UStaticMeshComponent* GateMesh;

    // Trigger volume that detects the drone passing through the gate
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    UBoxComponent* GateTrigger;

    // Material instances assigned in a BP child
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    UMaterialInterface* GlowMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    UMaterialInterface* DarkMaterial;

    // Whether this gate is currently active
    bool bIsActiveGate = false;

    // Reference to the manager
    UPROPERTY()
    ARaceGateManager* RaceGateManager;

    // Activation / Deactivation
    void ActivateGate();
    void DeactivateGate();

private:
    UFUNCTION()
    void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex,
        bool bFromSweep,
        const FHitResult& SweepResult);
};
