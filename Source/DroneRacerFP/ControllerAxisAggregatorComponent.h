#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ControllerCalibration.h"          // for FControllerRawState
#include "ControllerAxisAggregatorComponent.generated.h"

/**
 * Collects "raw axis" values from UE input axis mappings (Project Settings -> Input)
 * into a simple array that can be polled each frame.
 *
 * You bind axis mappings (FName) to Axis1..Axis16 handler functions.
 * Then call GetRawState() to retrieve DeviceId + Axes[].
 */
UCLASS(ClassGroup = (Input), meta = (BlueprintSpawnableComponent))
class UControllerAxisAggregatorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UControllerAxisAggregatorComponent();

    /** Stable id used to key calibration (start with "Player0"; later upgrade if you can). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Calibration")
    FString DeviceId = TEXT("Player0");

    /** How many axes you plan to collect (1..16). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axes", meta = (ClampMin = "1", ClampMax = "16"))
    int32 NumAxes = 8;

    /**
     * Axis mapping names in the order you want to expose them in OutState.Axes.
     * Example: ["RawAxis1","RawAxis2","RawAxis3","RawAxis4",...]
     *
     * These must match the names you created in Project Settings -> Input -> Axis Mappings.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axes")
    TArray<FName> AxisMappingNames;

    /** Returns DeviceId + the latest axis array. Safe to call every frame. */
    UFUNCTION(BlueprintCallable, Category = "Calibration")
    bool GetRawState(FControllerRawState& OutState) const;

    /**
     * Bind axis mapping names to the internal handlers.
     * Call this from Pawn/Controller after InputComponent exists (e.g. SetupPlayerInputComponent).
     */
    void BindAxisMappings(class UInputComponent* InputComponent);

    /** Optional: zero all axes */
    UFUNCTION(BlueprintCallable, Category = "Axes")
    void ClearAxes();

private:
    /** Latest axis values (size = NumAxes). */
    UPROPERTY()
    TArray<float> Axes;

private:
    void EnsureAxesSize();

    // 16 fixed handlers so we can BindAxis by function pointer
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

    void SetAxisValue(int32 Index0, float v);
};

