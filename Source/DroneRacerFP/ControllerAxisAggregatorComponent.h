#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ControllerCalibration.h" // FControllerRawState, FAxisCalibration
#include "ControllerAxisAggregatorComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class DRONERACERFP_API UControllerAxisAggregatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UControllerAxisAggregatorComponent();

	/** Clamp NumAxes and size arrays. */
	UFUNCTION(BlueprintCallable, Category = "AxisAggregator")
	void EnsureAxesSize();

	/** Clears Axes[] to 0. */
	UFUNCTION(BlueprintCallable, Category = "AxisAggregator")
	void ClearAxes();

	/** Copies current raw state into OutState. */
	UFUNCTION(BlueprintCallable, Category = "AxisAggregator")
	bool GetRawState(FControllerRawState& OutState) const;

	/**
	 * Bind axis mappings on a classic UInputComponent (BindAxis).
	 * Uses AxisMappingNames if provided, otherwise defaults to RawAxis1..RawAxisN.
	 */
	UFUNCTION(BlueprintCallable, Category = "AxisAggregator")
	void BindAxisMappings(UInputComponent* InputComponent);

	// ---------------- Calibration ----------------

	/** Start capturing min/max (and optionally center) per axis. */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void StartCalibration();

	/** Stop capturing. If bKeepResults=false, resets to current values. */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void StopCalibration(bool bKeepResults = true);

	/** Convenience: press once start, press again stop. */
	UFUNCTION(BlueprintCallable, Category = "Calibration")
	void ToggleCalibration();

	UFUNCTION(BlueprintPure, Category = "Calibration")
	bool IsCalibrating() const { return bIsCalibrating; }

	/** One FAxisCalibration per axis index (size == NumAxes). Useful for UI. */
	UFUNCTION(BlueprintPure, Category = "Calibration")
	const TArray<FAxisCalibration>& GetAxisCalibrations() const { return AxisCalibs; }

public:
	/** Device id string you can set upstream (or leave empty). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AxisAggregator")
	FString DeviceId;

	/** How many axes to aggregate (clamped 1..16). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AxisAggregator", meta = (ClampMin = "1", ClampMax = "16"))
	int32 NumAxes = 8;

	/**
	 * Optional list of axis mapping names (length can be >= NumAxes).
	 * If empty, defaults to RawAxis1..RawAxisN.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AxisAggregator")
	TArray<FName> AxisMappingNames;

	/** Current aggregated axis values (size == NumAxes). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AxisAggregator")
	TArray<float> Axes;

	/**
	 * If true: while calibrating, RawCenter is slowly pulled toward current value.
	 * This helps if you release the stick to center during calibration.
	 * If false: RawCenter is captured once at StartCalibration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
	bool bUpdateCenterWhileCalibrating = true;

	/** How quickly center adapts during calibration (0..1). Smaller = slower. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CenterLerpAlpha = 0.02f;

protected:
	void SetAxisValue(int32 Index0, float v);

	// BindAxis requires UFUNCTION signature void Func(float)
	UFUNCTION() void Axis1(float v);
	UFUNCTION() void Axis2(float v);
	UFUNCTION() void Axis3(float v);
	UFUNCTION() void Axis4(float v);
	UFUNCTION() void Axis5(float v);
	UFUNCTION() void Axis6(float v);
	UFUNCTION() void Axis7(float v);
	UFUNCTION() void Axis8(float v);
	UFUNCTION() void Axis9(float v);
	UFUNCTION() void Axis10(float v);
	UFUNCTION() void Axis11(float v);
	UFUNCTION() void Axis12(float v);
	UFUNCTION() void Axis13(float v);
	UFUNCTION() void Axis14(float v);
	UFUNCTION() void Axis15(float v);
	UFUNCTION() void Axis16(float v);

private:
	bool bIsCalibrating = false;

	/** One per axis index (size == NumAxes). */
	UPROPERTY(VisibleAnywhere, Category = "Calibration")
	TArray<FAxisCalibration> AxisCalibs;
};
