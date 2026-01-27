// DroneControllerCalibrationWidget.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ControllerCalibration.h"
#include "ControllerAxisAggregatorComponent.h"
#include "DroneControllerCalibrationWidget.generated.h"

// Delegate: widget asks "give me the current raw state" once per tick
DECLARE_DELEGATE_RetVal_OneParam(bool, FGetRawStateDelegate, FControllerRawState& /*OutState*/);

// Delegate: fired when calibration is done
DECLARE_DELEGATE_OneParam(FCalibrationFinishedSignature, const FControllerCalibration& /*Result*/);

UENUM()
enum class ECalibrationStep : uint8
{
	NotStarted,
	DetectCenter,
	DetectPitch,
	DetectRoll,
	DetectYaw,
	DetectThrottle,
	Done
};

UCLASS()
class DRONERACERFP_API UDroneControllerCalibrationWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// Expose the DeviceId on spawn
	UPROPERTY(BlueprintReadWrite, meta = (ExposeOnSpawn = true))
	FString DeviceId;

	// Caller binds this to their input backend (GenericUSBController, etc.)
	FGetRawStateDelegate OnGetRawState;

	// Caller binds this to save the calibration when we're done
	FCalibrationFinishedSignature OnCalibrationFinished;

	// Start the calibration sequence (call after OnGetRawState is bound)
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void InitWithAxisAggregator(UControllerAxisAggregatorComponent* InAxisAgg);

	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void StartCalibration();

	/*Returns current raw snapshot(Axes[]) from the aggregator.*/
	UFUNCTION(BlueprintCallable, Category = "Calibration|UI")
	bool GetRawStateBP(FControllerRawState& OutState) const;

	/** Returns a copy of current per-axis calibs (size == NumAxes). */
	UFUNCTION(BlueprintCallable, Category = "Calibration|UI")
	TArray<FAxisCalibration> GetAxisCalibrationsBP() const;

	/** Get mapped raw axis index for a logical axis name (Pitch/Roll/Yaw/Throttle). */
	UFUNCTION(BlueprintCallable, Category = "Calibration|UI")
	bool GetMappedAxisIndexBP(FName LogicalName, int32& OutAxisIndex) const;

	/** Get calibration struct for a logical axis (from the mapping). */
	UFUNCTION(BlueprintCallable, Category = "Calibration|UI")
	bool GetLogicalCalibrationBP(FName LogicalName, FAxisCalibration& OutCalib) const;


protected:
	// Store a reference to the aggregator
	UPROPERTY(BlueprintReadOnly, Category = "Calibration")
	TObjectPtr<UControllerAxisAggregatorComponent> AxisAgg = nullptr;


protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// You implement these in a Blueprint subclass to update UI
	UFUNCTION(BlueprintImplementableEvent, Category = "Calibration")
	void UpdateInstructionText(const FText& InText);

	UFUNCTION(BlueprintImplementableEvent, Category = "Calibration")
	void UpdateProgress(float NormalizedProgress);

private:
	// The calibration we're building up
	FControllerCalibration PendingCalibration;

	// Which step we're in
	ECalibrationStep CurrentStep = ECalibrationStep::NotStarted;

	// Timing
	float StepElapsed = 0.f;
	float StepDuration = 2.0f;

	// Once we see the first state, we lock axis count and init buffers
	bool bHasAxisCount = false;
	int32 NumAxes = 0;

	// For center detection
	TArray<float> CenterAccumulator;
	int32 CenterSampleCount = 0;

	// For motion detection
	TArray<float> Baseline;           // starting values for this step
	TArray<float> MotionAccumulator;  // total movement per axis

	// For min/max during each step
	TArray<float> StepRawMin;
	TArray<float> StepRawMax;

	// Which physical axis indices we've already assigned (so we don't reuse)
	TArray<int32> UsedAxisIndices;

private:
	// Step control
	void BeginStep(ECalibrationStep NewStep);
	void TickStep(float DeltaTime);

	void EnsureAxisBuffersInitialized(const FControllerRawState& State);

	void TickDetectCenter(const FControllerRawState& State);
	void TickDetectAxis(const FControllerRawState& State, FName LogicalAxisName);

	int32 PickAxisWithLargestMotion() const;
	bool IsAxisAlreadyUsed(int32 AxisIndex) const;
};


