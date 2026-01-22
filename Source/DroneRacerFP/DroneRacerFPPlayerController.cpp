// Copyright Epic Games, Inc. All Rights Reserved.


#include "DroneRacerFPPlayerController.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputActionValue.h"

#include "DroneControllerCalibrationWidget.h"
#include "Kismet/GameplayStatics.h"

void ADroneRacerFPPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // Add mapping context
    if (IMC_Default)
    {
        if (ULocalPlayer* LP = GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
                LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                Subsystem->AddMappingContext(IMC_Default, 0);
            }
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("IMC_Default not set on PlayerController"));
    }
}
void ADroneRacerFPPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
    if (!EIC)
    {
        UE_LOG(LogTemp, Warning, TEXT("InputComponent is not EnhancedInputComponent"));
        return;
    }

    if (!IA_StartCalibration)
    {
        UE_LOG(LogTemp, Warning, TEXT("IA_StartCalibration not set on PlayerController"));
        return;
    }

    EIC->BindAction(IA_StartCalibration, ETriggerEvent::Started, this,
        &ADroneRacerFPPlayerController::OnStartCalibration);
}


void ADroneRacerFPPlayerController::OnStartCalibration(const FInputActionValue& Value)
{
    UE_LOG(LogTemp, Log, TEXT("EnhancedInput: Start Calibration"));
    ShowControllerCalibration();
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
