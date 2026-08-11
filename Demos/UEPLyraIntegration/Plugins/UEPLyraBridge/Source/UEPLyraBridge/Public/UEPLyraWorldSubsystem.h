#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Subsystems/WorldSubsystem.h"

#include "UEPLyraWorldSubsystem.generated.h"

class ULyraExperienceDefinition;
class ULyraExperienceManagerComponent;
class AGameStateBase;

USTRUCT(BlueprintType)
struct UEPLYRABRIDGE_API FUEPLyraGameFeatureState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString Name;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString State;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bActive = false;
};

USTRUCT(BlueprintType)
struct UEPLYRABRIDGE_API FUEPLyraRuntimeSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bIsGameThread = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bIsGameWorld = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString WorldName;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    FString NetMode;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHasServerAuthority = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHasExperienceManager = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bExperienceLoaded = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    TObjectPtr<UObject> CurrentExperience = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHasPlayerPawn = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bHeroInputReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    bool bAbilitySystemReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "UEP|Lyra")
    TArray<FUEPLyraGameFeatureState> GameFeatures;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUEPLyraExperienceReady, UObject*, Experience);

/**
 * Per-world adapter for Python. It observes Lyra state but never changes
 * Experience, Game Feature, input, ability, authority, or replicated state.
 */
UCLASS()
class UEPLYRABRIDGE_API UUEPLyraWorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;

    UFUNCTION(BlueprintPure, Category = "UEP|Lyra")
    FUEPLyraRuntimeSnapshot CaptureSnapshot() const;

    UFUNCTION(BlueprintCallable, Category = "UEP|Lyra")
    void ClearPythonListeners();

    UPROPERTY(BlueprintAssignable, Category = "UEP|Lyra")
    FUEPLyraExperienceReady OnExperienceReady;

protected:
    virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;

private:
    void AttachExperienceManager(AGameStateBase* GameState = nullptr);
    void HandleGameStateSet(AGameStateBase* GameState);
    void HandleExperienceLoaded(const ULyraExperienceDefinition* Experience);

    TWeakObjectPtr<ULyraExperienceManagerComponent> ExperienceManager;
    TWeakObjectPtr<UObject> CurrentExperience;
    bool bExperienceCallbackRegistered = false;
};

/** Static lookup keeps Python independent of Unreal's subsystem templates. */
UCLASS()
class UEPLYRABRIDGE_API UUEPLyraBridgeLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintPure, Category = "UEP|Lyra", meta = (WorldContext = "WorldContextObject"))
    static UUEPLyraWorldSubsystem* GetBridgeForWorld(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category = "UEP|Lyra", meta = (WorldContext = "WorldContextObject"))
    static FUEPLyraRuntimeSnapshot CaptureSnapshotForWorld(const UObject* WorldContextObject);
};
