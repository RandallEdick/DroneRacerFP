#include "RaceGate.h"
#include "RaceGateManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "EngineUtils.h"
#include "DroneFPCharacter.h"   // Change to your actual drone class!

ARaceGate::ARaceGate()
{
    PrimaryActorTick.bCanEverTick = false;

    GateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GateMesh"));
    RootComponent = GateMesh;

    GateTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("GateTrigger"));
    GateTrigger->SetupAttachment(RootComponent);
    GateTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GateTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
    GateTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

    GateTrigger->OnComponentBeginOverlap.AddDynamic(this, &ARaceGate::OnTriggerBeginOverlap);
}

void ARaceGate::BeginPlay()
{
    Super::BeginPlay();

    // Find RaceGateManager in the level
    if (!RaceGateManager)
    {
        for (TActorIterator<ARaceGateManager> It(GetWorld()); It; ++It)
        {
            RaceGateManager = *It;
            break;
        }
    }
}

void ARaceGate::ActivateGate()
{
    bIsActiveGate = true;

    if (GlowMaterial)
        GateMesh->SetMaterial(0, GlowMaterial);
}

void ARaceGate::DeactivateGate()
{
    bIsActiveGate = false;

    if (DarkMaterial)
        GateMesh->SetMaterial(0, DarkMaterial);
}

void ARaceGate::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex,
    bool bFromSweep,
    const FHitResult& SweepResult)
{
    if (!bIsActiveGate || !RaceGateManager)
        return;

    ADroneFPCharacter* Drone = Cast<ADroneFPCharacter>(OtherActor);
    if (Drone)
    {
        RaceGateManager->GatePassed(this);
    }
}
