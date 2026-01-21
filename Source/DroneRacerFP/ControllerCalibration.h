// ControllerCalibration.h

#pragma once

#include "CoreMinimal.h"
#include "ControllerCalibration.generated.h"

// Raw state coming from your input backend (GenericUSBController, Raw Input, etc.)
USTRUCT(BlueprintType)
struct FControllerRawState
{
    GENERATED_BODY()

    // Something stable per device; you decide how to build this:
    // e.g. "Vendor_1234_Product_5678_Instance0"
    UPROPERTY(BlueprintReadWrite)
    FString DeviceId;

    // Raw axis values as floats. Could be -1..1, 0..1, 0..255, etc.
    // The calibration code doesn't care about the absolute range,
    // it just looks at relative movement and min/max.
    UPROPERTY(BlueprintReadWrite)
    TArray<float> Axes;
};

// Per-axis calibration data
USTRUCT(BlueprintType)
struct FAxisCalibration
{
    GENERATED_BODY()

    // Raw values directly from your backend (whatever range it uses)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RawMin = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RawCenter = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float RawMax = 1.f;

    // Deadzone around center (for centered axes) or near min (for throttle)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DeadZone = 0.05f;

    // Optional inversion (for user preference)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bInvert = false;
};

// Logical axis -> physical axis index + calibration
USTRUCT(BlueprintType)
struct FAxisMapping
{
    GENERATED_BODY()

    // "Pitch", "Roll", "Yaw", "Throttle"
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName LogicalName;

    // Index into FControllerRawState::Axes
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AxisIndex = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FAxisCalibration Calibration;
};

// Per-device calibration (for one controller)
USTRUCT(BlueprintType)
struct FControllerCalibration
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString DeviceId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAxisMapping> Mappings;

    const FAxisMapping* FindMapping(FName LogicalName) const
    {
        for (const FAxisMapping& M : Mappings)
        {
            if (M.LogicalName == LogicalName)
            {
                return &M;
            }
        }
        return nullptr;
    }

    FAxisMapping* FindMapping(FName LogicalName)
    {
        for (FAxisMapping& M : Mappings)
        {
            if (M.LogicalName == LogicalName)
            {
                return &M;
            }
        }
        return nullptr;
    }

    FAxisMapping& FindOrAddMapping(FName LogicalName)
    {
        if (FAxisMapping* Existing = FindMapping(LogicalName))
        {
            return *Existing;
        }
        FAxisMapping NewMapping;
        NewMapping.LogicalName = LogicalName;
        int32 Index = Mappings.Add(NewMapping);
        return Mappings[Index];
    }
};

// -------- Normalization helpers (from raw -> normalized) --------

// For centered axes: -1..+1 (pitch, roll, yaw)
static inline float NormalizeCenteredAxis(float Raw, const FAxisCalibration& C)
{
    float Norm = 0.f;

    if (Raw >= C.RawCenter)
    {
        const float Den = FMath::Max(1.f, C.RawMax - C.RawCenter);
        Norm = (Raw - C.RawCenter) / Den;  // 0..+1
    }
    else
    {
        const float Den = FMath::Max(1.f, C.RawCenter - C.RawMin);
        Norm = (Raw - C.RawCenter) / Den;  // 0..-1
    }

    Norm = FMath::Clamp(Norm, -1.f, 1.f);

    float AbsVal = FMath::Abs(Norm);
    if (AbsVal < C.DeadZone)
    {
        Norm = 0.f;
    }
    else
    {
        float Scaled = (AbsVal - C.DeadZone) / (1.f - C.DeadZone);
        Norm = FMath::Sign(Norm) * Scaled;
    }

    if (C.bInvert)
    {
        Norm = -Norm;
    }

    return Norm;
}

// For throttle: 0..1
static inline float NormalizeThrottleAxis(float Raw, const FAxisCalibration& C)
{
    float Norm = 0.f;

    const float Den = FMath::Max(1.f, C.RawMax - C.RawMin);
    Norm = (Raw - C.RawMin) / Den;   // 0..1 ideally

    Norm = FMath::Clamp(Norm, 0.f, 1.f);

    // Small deadzone near minimum to guarantee "zero throttle"
    const float DZ = C.DeadZone;
    if (Norm < DZ)
    {
        Norm = 0.f;
    }
    else
    {
        Norm = (Norm - DZ) / (1.f - DZ);
    }

    if (C.bInvert)
    {
        Norm = 1.f - Norm;
    }

    return Norm;
}

