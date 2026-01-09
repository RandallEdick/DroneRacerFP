// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneRacerFPGameMode.h"
#include "DroneRacerFPCharacter.h"
#include "UObject/ConstructorHelpers.h"

ADroneRacerFPGameMode::ADroneRacerFPGameMode()
	: Super()
{
    // set default pawn class to our Blueprinted character
    static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(
        TEXT("/Game/BP_DroneFPCharacter")
    );


    if (PlayerPawnClassFinder.Succeeded())
    {
        DefaultPawnClass = PlayerPawnClassFinder.Class;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("DroneRacerFPGameMode: Could not find BP_DroneFPCharacter!"));
    }

}
