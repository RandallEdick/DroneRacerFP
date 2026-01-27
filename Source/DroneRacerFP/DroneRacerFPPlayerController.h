// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "DroneRacerFPPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class UDroneControllerCalibrationWidget;

/**
 *
 */
UCLASS()
class DRONERACERFP_API ADroneRacerFPPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_StartCalibration;
protected:
	/** Input Mapping Context to be used for player input */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Input)
	UInputMappingContext* InputMappingContext;

	// YourPlayerController.h

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<class UDroneControllerCalibrationWidget> CalibrationWidgetClass;



	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputMappingContext* IMC_Default;

	UPROPERTY()
	UDroneControllerCalibrationWidget* CalibrationWidget = nullptr;

	UFUNCTION()
	void OnStartCalibration(const FInputActionValue& Value);

	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void ShowControllerCalibration();

	void SetupInputComponent();




	// Begin Actor interface
protected:

	virtual void BeginPlay() override;

	// End Actor interface
};
