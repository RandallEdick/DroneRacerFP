// DroneControllerCalibrationWidget.cpp

#include "DroneControllerCalibrationWidget.h"

void UDroneControllerCalibrationWidget::NativeConstruct()
{
    Super::NativeConstruct();

    PendingCalibration = FControllerCalibration();
    PendingCalibration.DeviceId = DeviceId;

    CurrentStep = ECalibrationStep::NotStarted;
}

void UDroneControllerCalibrationWidget::StartCalibration()
{
    PendingCalibration.Mappings.Empty();
    UsedAxisIndices.Empty();
    bHasAxisCount = false;

    BeginStep(ECalibrationStep::DetectCenter);
}

void UDroneControllerCalibrationWidget::BeginStep(ECalibrationStep NewStep)
{
    CurrentStep = NewStep;
    StepElapsed = 0.f;
    CenterSampleCount = 0;

    CenterAccumulator.Empty();
    MotionAccumulator.Empty();
    Baseline.Empty();
    StepRawMin.Empty();
    StepRawMax.Empty();

    switch (CurrentStep)
    {
    case ECalibrationStep::DetectCenter:
        StepDuration = 2.0f;
        UpdateInstructionText(FText::FromString(TEXT(
            "Controller Calibration\n\n"
            "Step 1: Leave all sticks centered and do not touch them."
        )));
        break;

    case ECalibrationStep::DetectPitch:
        StepDuration = 2.0f;
        UpdateInstructionText(FText::FromString(TEXT(
            "Step 2: Move the PITCH stick fully up and down repeatedly.\n"
            "(Right stick: forward/back)"
        )));
        break;

    case ECalibrationStep::DetectRoll:
        StepDuration = 2.0f;
        UpdateInstructionText(FText::FromString(TEXT(
            "Step 3: Move the ROLL stick fully left and right repeatedly.\n"
            "(Right stick: left/right)"
        )));
        break;

    case ECalibrationStep::DetectYaw:
        StepDuration = 2.0f;
        UpdateInstructionText(FText::FromString(TEXT(
            "Step 4: Move the YAW stick fully left and right repeatedly.\n"
            "(Left stick: yaw)"
        )));
        break;

    case ECalibrationStep::DetectThrottle:
        StepDuration = 2.0f;
        UpdateInstructionText(FText::FromString(TEXT(
            "Step 5: Move the THROTTLE stick from bottom to top and back repeatedly.\n"
            "(Left stick: throttle)"
        )));
        break;

    case ECalibrationStep::Done:
        UpdateInstructionText(FText::FromString(TEXT("Calibration complete.")));
        UpdateProgress(1.f);

        if (OnCalibrationFinished.IsBound())
        {
            OnCalibrationFinished.Execute(PendingCalibration);
        }
        break;

    default:
        break;
    }

    UpdateProgress(0.f);
}

void UDroneControllerCalibrationWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    if (CurrentStep == ECalibrationStep::NotStarted ||
        CurrentStep == ECalibrationStep::Done)
    {
        return;
    }

    if (!OnGetRawState.IsBound())
    {
        return; // no input source
    }

    FControllerRawState State;
    if (!OnGetRawState.Execute(State))
    {
        return; // device not ready / disconnected
    }

    EnsureAxisBuffersInitialized(State);

    StepElapsed += InDeltaTime;
    const float Alpha = FMath::Clamp(StepElapsed / StepDuration, 0.f, 1.f);
    UpdateProgress(Alpha);

    TickStep(InDeltaTime);

    // Step transition
    if (StepElapsed >= StepDuration)
    {
        switch (CurrentStep)
        {
        case ECalibrationStep::DetectCenter:
            BeginStep(ECalibrationStep::DetectPitch);
            break;
        case ECalibrationStep::DetectPitch:
            BeginStep(ECalibrationStep::DetectRoll);
            break;
        case ECalibrationStep::DetectRoll:
            BeginStep(ECalibrationStep::DetectYaw);
            break;
        case ECalibrationStep::DetectYaw:
            BeginStep(ECalibrationStep::DetectThrottle);
            break;
        case ECalibrationStep::DetectThrottle:
            BeginStep(ECalibrationStep::Done);
            break;
        default:
            break;
        }
    }
}

void UDroneControllerCalibrationWidget::TickStep(float DeltaTime)
{
    FControllerRawState State;
    if (!OnGetRawState.Execute(State))
    {
        return;
    }

    switch (CurrentStep)
    {
    case ECalibrationStep::DetectCenter:
        TickDetectCenter(State);
        break;

    case ECalibrationStep::DetectPitch:
        TickDetectAxis(State, TEXT("Pitch"));
        break;

    case ECalibrationStep::DetectRoll:
        TickDetectAxis(State, TEXT("Roll"));
        break;

    case ECalibrationStep::DetectYaw:
        TickDetectAxis(State, TEXT("Yaw"));
        break;

    case ECalibrationStep::DetectThrottle:
        TickDetectAxis(State, TEXT("Throttle"));
        break;

    default:
        break;
    }
}

void UDroneControllerCalibrationWidget::EnsureAxisBuffersInitialized(const FControllerRawState& State)
{
    if (bHasAxisCount)
    {
        return;
    }

    NumAxes = State.Axes.Num();
    if (NumAxes <= 0)
    {
        return;
    }

    CenterAccumulator.Init(0.f, NumAxes);
    MotionAccumulator.Init(0.f, NumAxes);
    Baseline = State.Axes;

    StepRawMin.Init(FLT_MAX, NumAxes);
    StepRawMax.Init(-FLT_MAX, NumAxes);

    bHasAxisCount = true;
}

// -------- Center detection step --------

void UDroneControllerCalibrationWidget::TickDetectCenter(const FControllerRawState& State)
{
    if (!bHasAxisCount || State.Axes.Num() != NumAxes)
    {
        return;
    }

    ++CenterSampleCount;

    for (int32 Axis = 0; Axis < NumAxes; ++Axis)
    {
        const float Val = State.Axes[Axis];
        CenterAccumulator[Axis] += Val;

        StepRawMin[Axis] = FMath::Min(StepRawMin[Axis], Val);
        StepRawMax[Axis] = FMath::Max(StepRawMax[Axis], Val);
    }

    // At the end of this step we *could* compute jitter-based deadzones,
    // but for simplicity we keep that logic in the per-axis steps.
}

// -------- Per-axis detection step (Pitch / Roll / Yaw / Throttle) --------

void UDroneControllerCalibrationWidget::TickDetectAxis(const FControllerRawState& State, FName LogicalAxisName)
{
    if (!bHasAxisCount || State.Axes.Num() != NumAxes)
    {
        return;
    }

    // Accumulate how much each axis moves compared to the baseline
    for (int32 Axis = 0; Axis < NumAxes; ++Axis)
    {
        const float Val = State.Axes[Axis];
        const float Delta = FMath::Abs(Val - Baseline[Axis]);

        MotionAccumulator[Axis] += Delta;

        StepRawMin[Axis] = FMath::Min(StepRawMin[Axis], Val);
        StepRawMax[Axis] = FMath::Max(StepRawMax[Axis], Val);
    }

    // When this step's time is up, pick the axis that moved the most
    if (StepElapsed >= StepDuration)
    {
        const int32 AxisIndex = PickAxisWithLargestMotion();
        if (AxisIndex == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("Calibration: No moving axis detected for %s"),
                *LogicalAxisName.ToString());
            return;
        }

        UsedAxisIndices.Add(AxisIndex);

        // Commit mapping
        FAxisMapping& Mapping = PendingCalibration.FindOrAddMapping(LogicalAxisName);
        Mapping.AxisIndex = AxisIndex;

        FAxisCalibration& Cal = Mapping.Calibration;
        const float RawMin = StepRawMin[AxisIndex];
        const float RawMax = StepRawMax[AxisIndex];

        Cal.RawMin = RawMin;
        Cal.RawMax = RawMax;

        if (LogicalAxisName == TEXT("Throttle"))
        {
            // For throttle we mostly use min/max; center is not critical
            Cal.RawCenter = (RawMin + RawMax) * 0.5f;
            Cal.DeadZone = 0.02f;
        }
        else
        {
            Cal.RawCenter = (RawMin + RawMax) * 0.5f;
            Cal.DeadZone = 0.05f;
        }

        Cal.bInvert = false; // you can add UI later to flip this if user wants
    }
}

int32 UDroneControllerCalibrationWidget::PickAxisWithLargestMotion() const
{
    float BestScore = 0.f;
    int32 BestAxis = INDEX_NONE;

    for (int32 Axis = 0; Axis < MotionAccumulator.Num(); ++Axis)
    {
        if (IsAxisAlreadyUsed(Axis))
        {
            continue;
        }

        const float Score = MotionAccumulator[Axis];
        if (Score > BestScore)
        {
            BestScore = Score;
            BestAxis = Axis;
        }
    }

    return BestAxis;
}

bool UDroneControllerCalibrationWidget::IsAxisAlreadyUsed(int32 AxisIndex) const
{
    return UsedAxisIndices.Contains(AxisIndex);
}

