#include "UEPValidationTypes.h"

#include "Components/SceneComponent.h"

int32 UUEPValidationObject::AddIntegers(const int32 A, const int32 B) const
{
    return A + B;
}

bool UUEPValidationObject::ComputeOutputs(const int32 Input, int32& Doubled, FString& Label) const
{
    Doubled = Input * 2;
    Label = FString::Printf(TEXT("Value=%d"), Input);
    return true;
}

FUEPValidationStruct UUEPValidationObject::EchoStruct(const FUEPValidationStruct& Input) const
{
    return Input;
}

void UUEPValidationObject::BroadcastSignal(const int32 Value)
{
    OnSignal.Broadcast(Value);
}

AUEPValidationActor::AUEPValidationActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

int32 AUEPValidationActor::IncrementCounter(const int32 Amount)
{
    Counter += Amount;
    return Counter;
}
