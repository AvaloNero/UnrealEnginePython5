// Copyright 20Tab S.r.l.

#include "PyCommandlet.h"

#include "UEPyModule.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

#include "Runtime/Core/Public/Internationalization/Regex.h"

UPyCommandlet::UPyCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LogToConsole = 1;
}

int32 UPyCommandlet::Main(const FString& CommandLine)
{
	FScopePythonGIL gil;

#if WITH_EDITOR
	// this allows commandlet's to use factories
	GEditor->Trans = GEditor->CreateTrans();

#endif

	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*CommandLine, Tokens, Switches, Params);

	FString Filepath = Tokens[0];
	if (!FPaths::FileExists(*Filepath))
	{
		UE_LOG(LogPython, Error, TEXT("Python file could not be found: %s"), *Filepath);
		return -1;
	}

	FString RegexString = FString::Printf(TEXT("(?<=%s).*"), *(Filepath.Replace(TEXT("\\"), TEXT("\\\\"))));
	const FRegexPattern myPattern(RegexString);
	FRegexMatcher myMatcher(myPattern, *CommandLine);
	myMatcher.FindNext();
#if UEP_LEGACY_ENGINE_MINOR_VERSION >= 18
	FString PyCommandLine = myMatcher.GetCaptureGroup(0).TrimStart().TrimEnd();
#else
	FString PyCommandLine = myMatcher.GetCaptureGroup(0).Trim().TrimTrailing();
#endif

	TArray<FString> PyArgv;
	PyArgv.Add(FString());
	bool escaped = false;
	for (int i = 0; i < PyCommandLine.Len(); i++)
	{
		if (PyCommandLine[i] == ' ')
		{
			PyArgv.Add(FString());
			continue;
		}
		else if (PyCommandLine[i] == '\"' && !escaped)
		{
			i++;
			while (i < PyCommandLine.Len() && !(PyCommandLine[i] == '"'))
			{
				PyArgv[PyArgv.Num() - 1].AppendChar(PyCommandLine[i]);
				i++;
				if (i == PyCommandLine.Len())
				{
					PyArgv[PyArgv.Num() - 1].InsertAt(0, "\"");
				}
			}
		}
		else
		{
			if (PyCommandLine[i] == '\\')
				escaped = true;
			else
				escaped = false;
			PyArgv[PyArgv.Num() - 1].AppendChar(PyCommandLine[i]);
		}
	}
	PyArgv.Insert(Filepath, 0);

	PyObject *PyArgvList = PyList_New(PyArgv.Num());
	if (!PyArgvList)
	{
		unreal_engine_py_log_error();
		return -1;
	}

	for (int32 Index = 0; Index < PyArgv.Num(); ++Index)
	{
		// Command-line arguments are already tokenized above. Unescaping them here
		// corrupts ordinary Windows paths (for example, "\\Results" becomes a
		// carriage return followed by "esults"). Preserve argv exactly as passed.
		const FString& Argument = PyArgv[Index];
		PyObject *PyArgument = PyUnicode_FromString(TCHAR_TO_UTF8(*Argument));
		if (!PyArgument)
		{
			Py_DECREF(PyArgvList);
			unreal_engine_py_log_error();
			return -1;
		}
		PyList_SET_ITEM(PyArgvList, Index, PyArgument);
	}

	if (PySys_SetObject("argv", PyArgvList) != 0)
	{
		Py_DECREF(PyArgvList);
		unreal_engine_py_log_error();
		return -1;
	}
	Py_DECREF(PyArgvList);

	Py_BEGIN_ALLOW_THREADS;

	FUnrealEnginePythonModule &PythonModule = FModuleManager::GetModuleChecked<FUnrealEnginePythonModule>("UnrealEnginePython");
	PythonModule.BrutalFinalize = true;
	PythonModule.RunFile(TCHAR_TO_UTF8(*Filepath));

	Py_END_ALLOW_THREADS;
	return 0;
}
