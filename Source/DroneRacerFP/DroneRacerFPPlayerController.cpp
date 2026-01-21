// Copyright Epic Games, Inc. All Rights Reserved.


#include "DroneRacerFPPlayerController.h"
#include "DroneControllerCalibrationWidget.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"

void ADroneRacerFPPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// get the enhanced input subsystem
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		// add the mapping context so we get controls
		Subsystem->AddMappingContext(InputMappingContext, 0);
	}
}


void ADroneRacerFPPlayerController::ShowControllerCalibration()
{
    if (!CalibrationWidgetClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("CalibrationWidgetClass not set"));
        return;
    }

    if (CalibrationWidget)
    {
        CalibrationWidget->RemoveFromParent();
        CalibrationWidget = nullptr;
    }

    CalibrationWidget = CreateWidget<UDroneControllerCalibrationWidget>(this, CalibrationWidgetClass);
    if (!CalibrationWidget)
    {
        return;
    }

    // 1) Decide which controller/device you're calibrating
    // For now: pick a stable string for the active device.
    // If you don't have per-device IDs yet, start with "Player0" and upgrade later.
    const FString DeviceId = TEXT("Player0");  // TODO: replace with real per-device ID
    CalibrationWidget->DeviceId = DeviceId;

    // 2) Bind: provide raw axis state each tick
    CalibrationWidget->OnGetRawState.BindLambda([this, DeviceId](FControllerRawState& OutState) -> bool
        {
            OutState.DeviceId = DeviceId;

            // ---- YOU MUST FILL THIS ----
            // Provide ALL raw axis values in a consistent order each frame.
            // Example placeholder: reading 8 axes from your own input aggregator.
            OutState.Axes.Reset();

            // TODO: Replace these with your real raw axes readout:
            // OutState.Axes.Add(GetRawAxis(0));
            // ...
            // For now, return false so calibration doesn't run until implemented.
            return false;
        });

    // 3) Bind: receive completed calibration
    CalibrationWidget->OnCalibrationFinished.BindLambda([this](const FControllerCalibration& Result)
        {
            UE_LOG(LogTemp, Log, TEXT("Calibration finished for %s. Mappings=%d"),
                *Result.DeviceId, Result.Mappings.Num());

            // TODO: save Result to disk (next section)
            // SaveCalibration(Result);

            if (CalibrationWidget)
            {
                CalibrationWidget->RemoveFromParent();
                CalibrationWidget = nullptr;
            }

            // Return input to game
            SetInputMode(FInputModeGameOnly());
            bShowMouseCursor = false;
        });

    CalibrationWidget->AddToViewport(1000);

    // UI input mode
    SetInputMode(FInputModeUIOnly());
    bShowMouseCursor = true;

    CalibrationWidget->StartCalibration();
}
