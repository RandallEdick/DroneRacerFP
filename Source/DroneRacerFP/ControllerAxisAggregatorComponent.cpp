#include "ControllerAxisAggregatorComponent.h"
#include "GameFramework/Actor.h"
#include "Components/InputComponent.h"

UControllerAxisAggregatorComponent::UControllerAxisAggregatorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    EnsureAxesSize();
}

void UControllerAxisAggregatorComponent::EnsureAxesSize()
{
    const int32 Clamped = FMath::Clamp(NumAxes, 1, 16);
    NumAxes = Clamped;

    if (Axes.Num() != NumAxes)
    {
        Axes.SetNumZeroed(NumAxes);
    }
}

void UControllerAxisAggregatorComponent::ClearAxes()
{
    EnsureAxesSize();
    for (float& v : Axes)
    {
        v = 0.f;
    }
}

bool UControllerAxisAggregatorComponent::GetRawState(FControllerRawState& OutState) const
{
    // We treat "having an axes array" as "ready".
    // If you want to detect "no input connected", do that upstream.
    OutState.DeviceId = DeviceId;
    OutState.Axes = Axes;
    return (Axes.Num() > 0);
}

void UControllerAxisAggregatorComponent::BindAxisMappings(UInputComponent* InputComponent)
{
    if (!InputComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("AxisAggregator: BindAxisMappings called with null InputComponent"));
        return;
    }

    EnsureAxesSize();

    // If user didn't fill AxisMappingNames, provide a common default pattern:
    // RawAxis1..RawAxisN
    if (AxisMappingNames.Num() == 0)
    {
        AxisMappingNames.SetNum(NumAxes);
        for (int32 i = 0; i < NumAxes; ++i)
        {
            AxisMappingNames[i] = *FString::Printf(TEXT("RawAxis%d"), i + 1);
        }
    }

    const int32 BindCount = FMath::Min(NumAxes, AxisMappingNames.Num());

    for (int32 i = 0; i < BindCount; ++i)
    {
        const FName AxisName = AxisMappingNames[i];
        if (AxisName.IsNone())
        {
            continue;
        }

        // BindAxis requires a UFUNCTION pointer; hence the Axis1..Axis16 handlers.
        switch (i)
        {
        case 0:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis1);  break;
        case 1:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis2);  break;
        case 2:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis3);  break;
        case 3:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis4);  break;
        case 4:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis5);  break;
        case 5:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis6);  break;
        case 6:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis7);  break;
        case 7:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis8);  break;
        case 8:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis9);  break;
        case 9:  InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis10); break;
        case 10: InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis11); break;
        case 11: InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis12); break;
        case 12: InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis13); break;
        case 13: InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis14); break;
        case 14: InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis15); break;
        case 15: InputComponent->BindAxis(AxisName, this, &UControllerAxisAggregatorComponent::Axis16); break;
        default: break;
        }
    }

    UE_LOG(LogTemp, Log, TEXT("AxisAggregator: Bound %d axis mappings on %s"),
        BindCount, *GetNameSafe(GetOwner()));
}

void UControllerAxisAggregatorComponent::SetAxisValue(int32 Index0, float v)
{
    if (!Axes.IsValidIndex(Index0))
    {
        return;
    }
    Axes[Index0] = v;
}

// Handlers
void UControllerAxisAggregatorComponent::Axis1(float v) { SetAxisValue(0, v); }
void UControllerAxisAggregatorComponent::Axis2(float v) { SetAxisValue(1, v); }
void UControllerAxisAggregatorComponent::Axis3(float v) { SetAxisValue(2, v); }
void UControllerAxisAggregatorComponent::Axis4(float v) { SetAxisValue(3, v); }
void UControllerAxisAggregatorComponent::Axis5(float v) { SetAxisValue(4, v); }
void UControllerAxisAggregatorComponent::Axis6(float v) { SetAxisValue(5, v); }
void UControllerAxisAggregatorComponent::Axis7(float v) { SetAxisValue(6, v); }
void UControllerAxisAggregatorComponent::Axis8(float v) { SetAxisValue(7, v); }
void UControllerAxisAggregatorComponent::Axis9(float v) { SetAxisValue(8, v); }
void UControllerAxisAggregatorComponent::Axis10(float v) { SetAxisValue(9, v); }
void UControllerAxisAggregatorComponent::Axis11(float v) { SetAxisValue(10, v); }
void UControllerAxisAggregatorComponent::Axis12(float v) { SetAxisValue(11, v); }
void UControllerAxisAggregatorComponent::Axis13(float v) { SetAxisValue(12, v); }
void UControllerAxisAggregatorComponent::Axis14(float v) { SetAxisValue(13, v); }
void UControllerAxisAggregatorComponent::Axis15(float v) { SetAxisValue(14, v); }
void UControllerAxisAggregatorComponent::Axis16(float v) { SetAxisValue(15, v); }
