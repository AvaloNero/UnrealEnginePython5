#include "UEPLyraWorldSubsystem.h"

#include "Character/LyraHeroComponent.h"
#include "Character/LyraPawnExtensionComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFeatureTypes.h"
#include "GameFeaturesSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/LyraExperienceDefinition.h"
#include "GameModes/LyraExperienceManagerComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogUEPLyraBridge, Log, All);

namespace
{
FString NetModeToString(const ENetMode NetMode)
{
    switch (NetMode)
    {
    case NM_Standalone:
        return TEXT("Standalone");
    case NM_DedicatedServer:
        return TEXT("DedicatedServer");
    case NM_ListenServer:
        return TEXT("ListenServer");
    case NM_Client:
        return TEXT("Client");
    default:
        return TEXT("Unknown");
    }
}

FString NetRoleToString(const ENetRole Role)
{
    switch (Role)
    {
    case ROLE_None:
        return TEXT("None");
    case ROLE_SimulatedProxy:
        return TEXT("SimulatedProxy");
    case ROLE_AutonomousProxy:
        return TEXT("AutonomousProxy");
    case ROLE_Authority:
        return TEXT("Authority");
    default:
        return TEXT("Unknown");
    }
}
}

bool UUEPLyraWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UUEPLyraWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    InWorld.GameStateSetEvent.AddUObject(this, &ThisClass::HandleGameStateSet);
    AttachExperienceManager(InWorld.GetGameState());
}

void UUEPLyraWorldSubsystem::Deinitialize()
{
    check(IsInGameThread());
    if (UWorld* World = GetWorld())
    {
        World->GameStateSetEvent.RemoveAll(this);
    }
    OnExperienceReady.Clear();
    ExperienceManager.Reset();
    CurrentExperience.Reset();
    bExperienceCallbackRegistered = false;
    Super::Deinitialize();
}

void UUEPLyraWorldSubsystem::AttachExperienceManager(AGameStateBase* GameState)
{
    check(IsInGameThread());
    UWorld* World = GetWorld();
    GameState = GameState ? GameState : (World ? World->GetGameState() : nullptr);
    ULyraExperienceManagerComponent* Manager = GameState ? GameState->FindComponentByClass<ULyraExperienceManagerComponent>() : nullptr;
    if (!Manager)
    {
        return;
    }
    if (bExperienceCallbackRegistered && ExperienceManager.Get() == Manager)
    {
        return;
    }

    CurrentExperience.Reset();
    ExperienceManager = Manager;
    bExperienceCallbackRegistered = true;
    Manager->CallOrRegister_OnExperienceLoaded(
        FOnLyraExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::HandleExperienceLoaded));
}

void UUEPLyraWorldSubsystem::HandleGameStateSet(AGameStateBase* GameState)
{
    AttachExperienceManager(GameState);
}

void UUEPLyraWorldSubsystem::HandleExperienceLoaded(const ULyraExperienceDefinition* Experience)
{
    check(IsInGameThread());
    CurrentExperience = const_cast<ULyraExperienceDefinition*>(Experience);
    OnExperienceReady.Broadcast(CurrentExperience.Get());
}

FUEPLyraRuntimeSnapshot UUEPLyraWorldSubsystem::CaptureSnapshot() const
{
    FUEPLyraRuntimeSnapshot Snapshot;
    Snapshot.bIsGameThread = IsInGameThread();
    if (!Snapshot.bIsGameThread)
    {
        UE_LOG(LogUEPLyraBridge, Error, TEXT("CaptureSnapshot must run on Unreal's game thread"));
        return Snapshot;
    }

    const UWorld* World = GetWorld();
    if (!World)
    {
        return Snapshot;
    }

    Snapshot.bIsGameWorld = World->IsGameWorld();
    Snapshot.WorldName = World->GetPathName();
    Snapshot.NetMode = NetModeToString(World->GetNetMode());
    Snapshot.bHasServerAuthority = World->GetNetMode() != NM_Client;

    APlayerController* PlayerController = nullptr;
    for (FConstPlayerControllerIterator Iterator = World->GetPlayerControllerIterator(); Iterator; ++Iterator)
    {
        APlayerController* Candidate = Iterator->Get();
        if (!Candidate)
        {
            continue;
        }
        ++Snapshot.PlayerControllerCount;
        if (Candidate->IsLocalController())
        {
            ++Snapshot.LocalPlayerControllerCount;
            PlayerController = Candidate;
        }
        else
        {
            ++Snapshot.RemotePlayerControllerCount;
            if (!PlayerController)
            {
                PlayerController = Candidate;
            }
        }
    }
    if (const AGameStateBase* GameState = World->GetGameState())
    {
        Snapshot.PlayerStateCount = GameState->PlayerArray.Num();
    }

    ULyraExperienceManagerComponent* Manager = ExperienceManager.Get();
    Snapshot.bHasExperienceManager = Manager != nullptr;
    Snapshot.bExperienceLoaded = Manager && Manager->IsExperienceLoaded();
    if (Snapshot.bExperienceLoaded)
    {
        Snapshot.CurrentExperience = const_cast<ULyraExperienceDefinition*>(Manager->GetCurrentExperienceChecked());
    }
    else
    {
        Snapshot.CurrentExperience = CurrentExperience.Get();
    }

    APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
    Snapshot.bHasPlayerPawn = Pawn != nullptr;
    if (Pawn)
    {
        Snapshot.bPawnLocallyControlled = Pawn->IsLocallyControlled();
        Snapshot.bPlayerStateReady = Pawn->GetPlayerState() != nullptr;
        Snapshot.PawnLocalRole = NetRoleToString(Pawn->GetLocalRole());
        Snapshot.PawnRemoteRole = NetRoleToString(Pawn->GetRemoteRole());
        const ULyraHeroComponent* Hero = ULyraHeroComponent::FindHeroComponent(Pawn);
        const ULyraPawnExtensionComponent* PawnExtension = ULyraPawnExtensionComponent::FindPawnExtensionComponent(Pawn);
        Snapshot.bHeroInputReady = Hero && Hero->IsReadyToBindInputs();
        Snapshot.bAbilitySystemReady = PawnExtension && PawnExtension->GetLyraAbilitySystemComponent();
    }

    UGameFeaturesSubsystem::Get().ForEachGameFeature(
        [&Snapshot](FGameFeatureInfo&& Info)
        {
            FUEPLyraGameFeatureState State;
            State.Name = MoveTemp(Info.Name);
            State.State = UE::GameFeatures::ToString(Info.CurrentState);
            State.bActive = Info.CurrentState == EGameFeaturePluginState::Active;
            Snapshot.GameFeatures.Add(MoveTemp(State));
        });
    Snapshot.GameFeatures.Sort(
        [](const FUEPLyraGameFeatureState& Left, const FUEPLyraGameFeatureState& Right)
        {
            return Left.Name < Right.Name;
        });
    return Snapshot;
}

void UUEPLyraWorldSubsystem::ClearPythonListeners()
{
    if (!IsInGameThread())
    {
        UE_LOG(LogUEPLyraBridge, Error, TEXT("ClearPythonListeners must run on Unreal's game thread"));
        return;
    }
    OnExperienceReady.Clear();
}

UUEPLyraWorldSubsystem* UUEPLyraBridgeLibrary::GetBridgeForWorld(const UObject* WorldContextObject)
{
    if (!IsInGameThread() || !GEngine || !WorldContextObject)
    {
        return nullptr;
    }
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    return World ? World->GetSubsystem<UUEPLyraWorldSubsystem>() : nullptr;
}

FUEPLyraRuntimeSnapshot UUEPLyraBridgeLibrary::CaptureSnapshotForWorld(const UObject* WorldContextObject)
{
    if (UUEPLyraWorldSubsystem* Bridge = GetBridgeForWorld(WorldContextObject))
    {
        return Bridge->CaptureSnapshot();
    }
    return FUEPLyraRuntimeSnapshot();
}
