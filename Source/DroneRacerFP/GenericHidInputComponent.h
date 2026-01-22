#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GenericHidInputComponent.generated.h"

USTRUCT(BlueprintType)
struct FGenericHidDeviceAxes
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FString DeviceId;
    UPROPERTY(BlueprintReadOnly) int32 VendorId = 0;
    UPROPERTY(BlueprintReadOnly) int32 ProductId = 0;
    UPROPERTY(BlueprintReadOnly) TArray<float> Axes;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGenericHidAxesUpdated, const FGenericHidDeviceAxes&, Axes);

UCLASS(ClassGroup = (Input), meta = (BlueprintSpawnableComponent))
class DRONERACERFP_API UGenericHidInputComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UGenericHidInputComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GenericHID")
    bool bAutoStart = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GenericHID")
    bool bLogDevices = true;

    UPROPERTY(BlueprintAssignable, Category = "GenericHID")
    FOnGenericHidAxesUpdated OnAxesUpdated;

    UFUNCTION(BlueprintCallable, Category = "GenericHID")
    void Start();

    UFUNCTION(BlueprintCallable, Category = "GenericHID")
    void Stop();

    UFUNCTION(BlueprintCallable, Category = "GenericHID")
    void GetKnownDevices(TArray<FGenericHidDeviceAxes>& OutDevices) const;

    UFUNCTION(BlueprintCallable, Category = "GenericHID")
    bool GetLatestAxesForDevice(const FString& DeviceId, FGenericHidDeviceAxes& OutDevice) const;

    // WM_INPUT entry point (called by message handler)
    void HandleRawInput(void* RawInputHandle);

    // Single active instance (simple drop-in)
    static UGenericHidInputComponent* GActiveInstance;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public: // <-- must be public because .cpp defines it as UGenericHidInputComponent::FDeviceState
    struct FDeviceState;

private:
    bool bStarted = false;

#if PLATFORM_WINDOWS
    TMap<void*, TSharedPtr<FDeviceState>> Devices;
#endif
};
