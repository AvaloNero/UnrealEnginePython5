#include "CoreMinimal.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "UnrealEnginePython.h"

DEFINE_LOG_CATEGORY_STATIC(LogUEP58Validation, Log, All);

class FUEP58HostModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (!FParse::Value(FCommandLine::Get(), TEXT("UEPValidationScript="), ValidationScript))
        {
            return;
        }

        FParse::Value(FCommandLine::Get(), TEXT("UEPValidationResult="), ValidationResult);
        AllModulesLoadedHandle = FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(
            this,
            &FUEP58HostModule::RunRequestedValidation
        );
    }

    virtual void ShutdownModule() override
    {
        if (AllModulesLoadedHandle.IsValid())
        {
            FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(AllModulesLoadedHandle);
            AllModulesLoadedHandle.Reset();
        }
    }

private:
    void RunRequestedValidation()
    {
        if (AllModulesLoadedHandle.IsValid())
        {
            FCoreDelegates::OnAllModuleLoadingPhasesComplete.Remove(AllModulesLoadedHandle);
            AllModulesLoadedHandle.Reset();
        }

        UE_LOG(LogUEP58Validation, Display, TEXT("Running packaged validation script: %s"), *ValidationScript);

        FUnrealEnginePythonModule& PythonModule =
            FModuleManager::LoadModuleChecked<FUnrealEnginePythonModule>(TEXT("UnrealEnginePython"));
        PythonModule.InitializePython();

        bool bPassed = PythonModule.IsPythonInitialized();
        if (bPassed)
        {
            PythonModule.RunFile(TCHAR_TO_UTF8(*ValidationScript));

            FString ResultContents;
            bPassed = !ValidationResult.IsEmpty()
                && FFileHelper::LoadFileToString(ResultContents, *ValidationResult)
                && ResultContents.Contains(TEXT("\"status\": \"passed\""));
        }

        if (bPassed)
        {
            UE_LOG(LogUEP58Validation, Display, TEXT("UEP_PACKAGED_VALIDATION_PASSED"));
        }
        else
        {
            UE_LOG(LogUEP58Validation, Error, TEXT("UEP_PACKAGED_VALIDATION_FAILED"));
        }

        FPlatformMisc::RequestExitWithStatus(
            false,
            bPassed ? 0 : 2,
            TEXT("FUEP58HostModule::RunRequestedValidation")
        );
    }

    FString ValidationScript;
    FString ValidationResult;
    FDelegateHandle AllModulesLoadedHandle;
};

IMPLEMENT_PRIMARY_GAME_MODULE(FUEP58HostModule, UEP58Host, "UEP58Host");
