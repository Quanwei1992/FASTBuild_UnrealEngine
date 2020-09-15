// Copyright Epic Games, Inc. All Rights Reserved.
#include "ShaderCompiler.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeLock.h"

#if PLATFORM_WINDOWS

namespace FASTBuildShaderCompilerVariables
{
	int32 Enabled = 1;

	FAutoConsoleVariableRef CVarFastBuildShaderCompile(
		TEXT("r.FASTBuildShaderCompile"),
		Enabled,
		TEXT("Enables or disables the use of FASTBuild to build shaders.\n")
		TEXT("0: Local builds only. \n")
		TEXT("1: Distribute builds using FASTBuild."),
		ECVF_Default);

	/** The maximum number of shaders to group into a single FastBuild task. */
	int32 BatchSize = 16;
	FAutoConsoleVariableRef CVarFastBuildShaderCompileBatchSize(
		TEXT("r.FastBuildShaderCompile.Xml.BatchSize"),
		BatchSize,
		TEXT("Specifies the number of shaders to batch together into a single XGE task.\n")
		TEXT("Default = 16\n"),
		ECVF_Default);

	/** The total number of batches to fill with shaders before creating another group of batches. */
	int32 BatchGroupSize = 128;
	FAutoConsoleVariableRef CVarFastBuildShaderCompileBatchGroupSize(
		TEXT("r.FastBuildShaderCompile.Xml.BatchGroupSize"),
		BatchGroupSize,
		TEXT("Specifies the number of batches to fill with shaders.\n")
		TEXT("Shaders are spread across this number of batches until all the batches are full.\n")
		TEXT("This allows the XGE compile to go wider when compiling a small number of shaders.\n")
		TEXT("Default = 128\n"),
		ECVF_Default);

	/**
	* The number of seconds to wait after a job is submitted before kicking off the FastBuild process.
	* This allows time for the engine to enqueue more shaders, so we get better batching.
	*/
	float JobTimeout = 0.5f;
	FAutoConsoleVariableRef CVarFastBuildShaderCompileJobTimeout(
		TEXT("r.FastBuildShaderCompile.Xml.JobTimeout"),
		JobTimeout,
		TEXT("The number of seconds to wait for additional shader jobs to be submitted before starting a build.\n")
		TEXT("Default = 0.5\n"),
		ECVF_Default);


	void Init()
	{
		static bool bInitialized = false;
		if (!bInitialized)
		{
			// Allow command line to override the value of the console variables.
			if (FParse::Param(FCommandLine::Get(), TEXT("fastbuildshadercompile")))
			{
				FASTBuildShaderCompilerVariables::Enabled = 1;
			}

			static const IConsoleVariable* CVarAllowCompilingThroughWorkers = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shaders.AllowCompilingThroughWorkers"), false);
			if (FParse::Param(FCommandLine::Get(), TEXT("nofastbuildshadercompile")) || FParse::Param(FCommandLine::Get(), TEXT("noshaderworker")) || (CVarAllowCompilingThroughWorkers && CVarAllowCompilingThroughWorkers->GetInt() == 0))
			{
				FASTBuildShaderCompilerVariables::Enabled = 0;
			}

			bInitialized = true;
		}
	}
}



#endif



static FString FASTBuild_ConsolePath;
static const FString FASTBuild_ScriptFileName(TEXT("fbshader.bff"));
static const FString FASTBuild_FDBFileName(TEXT("fbshader.windows.fdb"));
static const FString FASTBuild_InputFileName(TEXT("Woker.in"));
static const FString FASTBuild_OutputFileName(TEXT("Woker.out"));
static const FString FASTBuild_SuccessFileName(TEXT("Success"));
static const FString FASTBuild_CachePath(TEXT("..\\Saved\\FASTBuildCache"));
static const FString FASTBuild_Toolchain[]
{
	TEXT("Engine\\Binaries\\ThirdParty\\Windows\\DirectX\\x64"),
	TEXT("Engine\\Binaries\\ThirdParty\\AppLocalDependencies\\Win64")
};


bool FShaderCompileFASTBuildThreadRunnable::IsSupported()
{
	FASTBuildShaderCompilerVariables::Init();
	if (FASTBuildShaderCompilerVariables::Enabled == 0) return false;

	// 检查FASTBuild可执行文件是否存在
	IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();
	FString executeablePath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries"), TEXT("ThirdParty"), TEXT("FastBuild"), TEXT("FBuild.exe"));
	if (!platformFile.FileExists(*executeablePath))
	{
		FASTBuildShaderCompilerVariables::Enabled = 0;
		return false;
	}

	FASTBuild_ConsolePath = executeablePath;
	return true;
}

FArchive* FShaderCompileFASTBuildThreadRunnable::CreateFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	FArchive* File = nullptr;
	int32 RetryCount = 0;
	// Retry over the next two seconds if we can't write out the file.
	// Anti-virus and indexing applications can interfere and cause this to fail.
	while (File == nullptr && RetryCount < 200)
	{
		if (RetryCount > 0)
		{
			FPlatformProcess::Sleep(0.01f);
		}
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly);
		RetryCount++;
	}
	if (File == nullptr)
	{
		File = IFileManager::Get().CreateFileWriter(*Filename, FILEWRITE_EvenIfReadOnly | FILEWRITE_NoFail);
	}
	checkf(File, TEXT("Failed to create file %s!"), *Filename);
	return File;
}

void  FShaderCompileFASTBuildThreadRunnable::MoveFileHelper(const FString& To, const FString& From)
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (PlatformFile.FileExists(*From))
	{
		FString DirectoryName;
		int32 LastSlashIndex;
		if (To.FindLastChar('/', LastSlashIndex))
		{
			DirectoryName = To.Left(LastSlashIndex);
		}
		else
		{
			DirectoryName = To;
		}

		// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
		// We can't avoid code duplication unless we refactored the local worker too.

		bool Success = false;
		int32 RetryCount = 0;
		// Retry over the next two seconds if we can't move the file.
		// Anti-virus and indexing applications can interfere and cause this to fail.
		while (!Success && RetryCount < 200)
		{
			if (RetryCount > 0)
			{
				FPlatformProcess::Sleep(0.01f);
			}

			// MoveFile does not create the directory tree, so try to do that now...
			Success = PlatformFile.CreateDirectoryTree(*DirectoryName);
			if (Success)
			{
				Success = PlatformFile.MoveFile(*To, *From);
			}
			RetryCount++;
		}
		checkf(Success, TEXT("Failed to move file %s to %s!"), *From, *To);
	}
}

void FShaderCompileFASTBuildThreadRunnable::DeleteFileHelper(const FString& Filename)
{
	// TODO: This logic came from FShaderCompileThreadRunnable::WriteNewTasks().
	// We can't avoid code duplication unless we refactored the local worker too.

	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*Filename))
	{
		bool bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);

		// Retry over the next two seconds if we couldn't delete it
		int32 RetryCount = 0;
		while (!bDeletedOutput && RetryCount < 200)
		{
			FPlatformProcess::Sleep(0.01f);
			bDeletedOutput = IFileManager::Get().Delete(*Filename, true, true);
			RetryCount++;
		}
		checkf(bDeletedOutput, TEXT("Failed to delete %s!"), *Filename);
	}
}





FShaderCompileFASTBuildThreadRunnable::FShaderCompileFASTBuildThreadRunnable(class FShaderCompilingManager* InManager)
	: FShaderCompileThreadRunnableBase(InManager)
	, ShaderBatchesInFlightCompleted(0)
	, FASTBuildWorkingDirectory(InManager->AbsoluteShaderBaseWorkingDirectory / TEXT("FASTBuild"))
	, FASTBuildDirectoryIndex(0)
	, LastAddTime(0)
	, StartTime(0)
	, BatchIndexToCreate(0)
	, BatchIndexToFill(0)
	, BuildProcessID(INDEX_NONE)
{

}

FShaderCompileFASTBuildThreadRunnable::~FShaderCompileFASTBuildThreadRunnable()
{
	if (BuildProcessHandle.IsValid())
	{
		// We still have a build in progress.
		// Kill it...
		FPlatformProcess::TerminateProc(BuildProcessHandle);
		FPlatformProcess::CloseProc(BuildProcessHandle);
	}

	// Clean up any intermediate files/directories we've got left over.
	IFileManager::Get().DeleteDirectory(*FASTBuildWorkingDirectory, false, true);

	// Delete all the shader batch instances we have.
	for (FShaderBatch* Batch : ShaderBatchesIncomplete)
		delete Batch;

	for (FShaderBatch* Batch : ShaderBatchesInFlight)
		delete Batch;

	for (FShaderBatch* Batch : ShaderBatchesFull)
		delete Batch;

	ShaderBatchesIncomplete.Empty();
	ShaderBatchesInFlight.Empty();
	ShaderBatchesFull.Empty();
}


void FShaderCompileFASTBuildThreadRunnable::PostCompletedJobsForBatch(FShaderBatch* Batch)
{
	// Enter the critical section so we can access the input and output queues
	FScopeLock Lock(&Manager->CompileQueueSection);
	for (auto Job : Batch->GetJobs())
	{
		FShaderMapCompileResults& ShaderMapResults = Manager->ShaderMapJobs.FindChecked(Job->Id);
		ShaderMapResults.FinishedJobs.Add(Job);
		ShaderMapResults.bAllJobsSucceeded = ShaderMapResults.bAllJobsSucceeded && Job->bSucceeded;
	}

	// Using atomics to update NumOutstandingJobs since it is read outside of the critical section
	FPlatformAtomics::InterlockedAdd(&Manager->NumOutstandingJobs, -Batch->NumJobs());
}


void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::WriteTransferFile()
{
	// Write out the file that the worker app is waiting for, which has all the information needed to compile the shader.
	FArchive* TransferFile = CreateFileHelper(InputFileNameAndPath);
	FShaderCompileUtilities::DoWriteTasks(Jobs, *TransferFile);
	delete TransferFile;

	bTransferFileWritten = true;
}

void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::SetIndices(int32 InDirectoryIndex, int32 InBatchIndex)
{
	DirectoryIndex = InDirectoryIndex;
	BatchIndex = InBatchIndex;

	WorkingDirectory = FString::Printf(TEXT("%s/%d/%d"), *DirectoryBase, DirectoryIndex, BatchIndex);

	InputFileNameAndPath = WorkingDirectory / InputFileName;
	OutputFileNameAndPath = WorkingDirectory / OutputFileName;
	SuccessFileNameAndPath = WorkingDirectory / SuccessFileName;
}

void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::CleanUpFiles(bool keepInputFile)
{
	if (!keepInputFile)
	{
		DeleteFileHelper(InputFileNameAndPath);
	}

	DeleteFileHelper(OutputFileNameAndPath);
	DeleteFileHelper(SuccessFileNameAndPath);
}



void FShaderCompileFASTBuildThreadRunnable::FShaderBatch::AddJob(TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe> Job)
{
	// We can only add jobs to a batch which hasn't been written out yet.
	if (bTransferFileWritten)
	{
		UE_LOG(LogShaderCompilers, Fatal, TEXT("Attempt to add shader compile jobs to an XGE shader batch which has already been written to disk."));
	}
	else
	{
		Jobs.Add(Job);
	}
}



void FShaderCompileFASTBuildThreadRunnable::GatherResultsFromFASTBuild()
{
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileManager& FileManager = IFileManager::Get();

	// Reverse iterate so we can remove batches that have completed as we go.
	for (int32 Index = ShaderBatchesInFlight.Num() - 1; Index >= 0; Index--)
	{
		FShaderBatch* Batch = ShaderBatchesInFlight[Index];

		// If this batch is completed already, skip checks.
		if (Batch->bSuccessfullyCompleted)
		{
			continue;
		}

		// Instead of checking for another file, we just check to see if the file exists, and if it does, we check if it has a write lock on it. FASTBuild never does partial writes, so this should tell us if FASTBuild is complete.
		// Perform the same checks on the worker output file to verify it came from this build.
		if (PlatformFile.FileExists(*Batch->OutputFileNameAndPath))
		{
			if (FileManager.FileSize(*Batch->OutputFileNameAndPath) > 0)
			{
				IFileHandle* Handle = PlatformFile.OpenWrite(*Batch->OutputFileNameAndPath, true);
				if (Handle)
				{
					delete Handle;

					if (PlatformFile.GetTimeStamp(*Batch->OutputFileNameAndPath) >= ScriptFileCreationTime)
					{
						FArchive* OutputFilePtr = FileManager.CreateFileReader(*Batch->OutputFileNameAndPath, FILEREAD_Silent);
						if (OutputFilePtr)
						{
							FArchive& OutputFile = *OutputFilePtr;
							FShaderCompileUtilities::DoReadTaskResults(Batch->GetJobs(), OutputFile);

							// Close the output file.
							delete OutputFilePtr;

							// Cleanup the worker files
							// Do NOT clean up files until the whole batch is done, so we can clean them all up once the fastbuild process exits. Otherwise there is a race condition between FastBuild checking the output files, and us deleting them here.
							//Batch->CleanUpFiles(false);			// (false = don't keep the input file)
							Batch->bSuccessfullyCompleted = true;
							PostCompletedJobsForBatch(Batch);
							//ShaderBatchesInFlight.RemoveAt(Index);
							ShaderBatchesInFlightCompleted++;

							UE_LOG(LogShaderCompilers, Display, TEXT(" Shaders left to compile %d"), Manager->GetNumRemainingJobs());
						}
					}
				}
			}
		}
	}
}

/*
*  生成 .bff 文件头信息，自定义 Compiler 信息，依赖文件列表
*/

void FShaderCompileFASTBuildThreadRunnable::WriteScriptFileHeader(FArchive* ScriptFile, const FString& WokerName)
{

	static const TCHAR headerTemplate[] =
		TEXT("Settings\r\n")
		TEXT("{\r\n")
		TEXT("\t.CachePath = '%s'\r\n")
		TEXT("\t.DisableDBMigration = true\r\n")
		TEXT("}\r\n")
		TEXT("\r\n")
		TEXT("Compiler('ShaderCompiler')\r\n")
		TEXT("{\r\n")
		TEXT("\t.CompilerFamily = 'custom'\r\n")
		TEXT("\t.Executable = '%s'\r\n")
		TEXT("\t.ExecutableRootPath = '%s'\r\n")
		TEXT("\t.SimpleDistributionMode = true\r\n")
		TEXT("\t.ExtraFiles = \r\n")
		TEXT("{\r\n");



	FString headerString = FString::Printf(headerTemplate, *FASTBuild_CachePath, *WokerName,
		*IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::RootDir()));

	ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*headerString, headerString.Len()).Get(), sizeof(ANSICHAR) * headerString.Len());

	// 自定义枚举器类，将枚举到的文件加入到ExtraFiles列表中
	class FDependencyEnumerator : public IPlatformFile::FDirectoryVisitor
	{
	public:
		FDependencyEnumerator(FArchive* inScriptFile, const TCHAR* inPrefix, const TCHAR* inExtension,
			const TCHAR* inExcludeExtensions = nullptr)
			: scriptFile(inScriptFile)
			, prefix(inPrefix)
			, extension(inExtension)
		{
			if (inExcludeExtensions != nullptr)
			{
				FString ext = inExcludeExtensions;
				while (true)
				{
					FString curExt = TEXT("");
					FString leftExts = TEXT("");
					if (ext.Split(TEXT("|"), &curExt, &leftExts))
					{
						excludedExtensions.Add(curExt);
						ext = leftExts;
					}
					else {
						excludedExtensions.Add(ext);
						break;
					}
				}
			}
		}


		virtual bool Visit(const TCHAR* fileNameChar, bool isDirectory) override
		{
			if (isDirectory) return true;
			FString fileName = FPaths::GetBaseFilename(fileNameChar);
			FString fileExtension = FPaths::GetExtension(fileNameChar);
			bool excluded = excludedExtensions.Find(fileExtension) != INDEX_NONE;
			if (!excluded && (!prefix || fileName.StartsWith(prefix)) && (!extension || fileExtension.Equals(extension)))
			{
				FString extraFile = TEXT("\t\t'") +
					IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(fileNameChar) +
					TEXT("',\r\n");
				scriptFile->Serialize((void*)StringCast<ANSICHAR>(*extraFile, extraFile.Len()).Get(), sizeof(ANSICHAR) * extraFile.Len());
			}
			return true;
		}


		FArchive* const scriptFile;
		const TCHAR* prefix;
		const TCHAR* extension;
		TArray<FString> excludedExtensions;
	};


	// 枚举平台工具链依赖目录下的所有文件
	// 以便任意干净的远程Windows 系统都可以顺利运行 ShaderCompilerWoker.exe
	FDependencyEnumerator toolChainDeps = FDependencyEnumerator(ScriptFile, NULL, NULL);
	for (const FString& path : FASTBuild_Toolchain)
	{
		FString fullPath = FPaths::Combine(FPaths::RootDir(), path);
		IFileManager::Get().IterateDirectoryRecursively(*fullPath, toolChainDeps);
	}

	// 枚举所有ShaderWokder相关文件

	FDependencyEnumerator moduleFileDeps = FDependencyEnumerator(ScriptFile, TEXT("ShaderCompileWorker"),
		nullptr, TEXT("exe|EXE|pdb|PDB"));

	IFileManager::Get().IterateDirectoryRecursively(*FPlatformProcess::GetModulesDirectory(), moduleFileDeps);

	const auto& directoryMappings = AllShaderSourceDirectoryMappings();
	// 根据着色器所在目录列表，枚举所有着色器
	for (const auto& mappingEntry : directoryMappings)
	{
		FDependencyEnumerator usfDeps = FDependencyEnumerator(ScriptFile, NULL, NULL);
		IFileManager::Get().IterateDirectoryRecursively(*mappingEntry.Value, usfDeps);
	}

	// 结束文件头描述
	const FString extraFilesFooter = FString(
		TEXT("\t}\r\n")
		TEXT("}\r\n")
	);

	ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*extraFilesFooter, extraFilesFooter.Len()).Get(), sizeof(ANSICHAR) * extraFilesFooter.Len());
}


int32 FShaderCompileFASTBuildThreadRunnable::CompilingLoop()
{

	bool bWorkRemaining = false;

	// We can only run one XGE build at a time.
	// Check if a build is currently in progress.
	if (BuildProcessHandle.IsValid())
	{
		// Read back results from the current batches in progress.
		GatherResultsFromFASTBuild();

		bool bDoExitCheck = false;
		if (FPlatformProcess::IsProcRunning(BuildProcessHandle))
		{
			if (ShaderBatchesInFlight.Num() == ShaderBatchesInFlightCompleted)
			{
				// We've processed all batches.
				// Wait for the XGE console process to exit
				FPlatformProcess::WaitForProc(BuildProcessHandle);
				bDoExitCheck = true;
			}
		}
		else
		{
			bDoExitCheck = true;
		}

		if (bDoExitCheck)
		{
			if (ShaderBatchesInFlight.Num() > 0)
			{
				// The build process has stopped.
				// Do one final pass over the output files to gather any remaining results.
				GatherResultsFromFASTBuild();
			}

			// The build process is no longer running.
			// We need to check the return code for possible failure
			int32 ReturnCode = 0;
			FPlatformProcess::GetProcReturnCode(BuildProcessHandle, &ReturnCode);

			switch (ReturnCode)
			{
			case 0:
				// No error
				break;

			case 1:
				// One or more of the shader compile worker processes crashed.
				UE_LOG(LogShaderCompilers, Fatal, TEXT("An error occurred during an FASTBuild shader compilation job. One or more of the shader compile worker processes exited unexpectedly (Code 1)."));
				break;

			case 2:
				// Fatal IncrediBuild error
				UE_LOG(LogShaderCompilers, Fatal, TEXT("An error occurred during an FASTBuild shader compilation job. XGConsole.exe returned a fatal Incredibuild error (Code 2)."));
				break;

			case 3:
				// User canceled the build
				UE_LOG(LogShaderCompilers, Display, TEXT("The user terminated an FASTBuild shader compilation job. Incomplete shader jobs will be redispatched in another FASTBuild build."));
				break;

			default:
				UE_LOG(LogShaderCompilers, Display, TEXT("An unknown error occurred during an FASTBuild shader compilation job (Code %d). Incomplete shader jobs will be redispatched in another FASTBuild build."), ReturnCode);
				break;
			}





			// Reclaim jobs from the workers which did not succeed (if any).
			for (FShaderBatch* Batch : ShaderBatchesInFlight)
			{
				if (Batch->bSuccessfullyCompleted)
				{
					// If we completed successfully, clean up.
					// PostCompletedJobsForBatch(Batch);
					Batch->CleanUpFiles(false);
					delete Batch;

				}
				else {
					
					// Delete any output/success files, but keep the input file so we don't have to write it out again.
					Batch->CleanUpFiles(true);
					// We can't add any jobs to a shader batch which has already been written out to disk,
					// so put the batch back into the full batches list, even if the batch isn't full.
					ShaderBatchesFull.Add(Batch);

					// Reset the batch/directory indices and move the input file to the correct place.
					FString OldInputFilename = Batch->InputFileNameAndPath;
					Batch->SetIndices(FASTBuildDirectoryIndex, BatchIndexToCreate++);
					MoveFileHelper(Batch->InputFileNameAndPath, OldInputFilename);
				}
			}

			ShaderBatchesInFlightCompleted = 0;
			ShaderBatchesInFlight.Empty();
			FPlatformProcess::CloseProc(BuildProcessHandle);
		}

		bWorkRemaining |= ShaderBatchesInFlight.Num() > 0;
	}
	// No build process running. Check if we can kick one off now.
	else
	{
		// Determine if enough time has passed to allow a build to kick off.
		// Since shader jobs are added to the shader compile manager asynchronously by the engine, 
		// we want to give the engine enough time to queue up a large number of shaders.
		// Otherwise we will only be kicking off a small number of shader jobs at once.
		bool BuildDelayElapsed = (((FPlatformTime::Cycles() - LastAddTime) * FPlatformTime::GetSecondsPerCycle()) >= FASTBuildShaderCompilerVariables::JobTimeout);
		bool HasJobsToRun = (ShaderBatchesIncomplete.Num() > 0 || ShaderBatchesFull.Num() > 0);

		if (BuildDelayElapsed && HasJobsToRun && ShaderBatchesInFlight.Num() == ShaderBatchesInFlightCompleted)
		{
			// Move all the pending shader batches into the in-flight list.
			ShaderBatchesInFlight.Reserve(ShaderBatchesIncomplete.Num() + ShaderBatchesFull.Num());

			for (FShaderBatch* Batch : ShaderBatchesIncomplete)
			{
				// Check we've actually got jobs for this batch.
				check(Batch->NumJobs() > 0);

				// Make sure we've written out the worker files for any incomplete batches.
				Batch->WriteTransferFile();
				ShaderBatchesInFlight.Add(Batch);
			}

			for (FShaderBatch* Batch : ShaderBatchesFull)
			{
				// Check we've actually got jobs for this batch.
				check(Batch->NumJobs() > 0);

				ShaderBatchesInFlight.Add(Batch);
			}

			ShaderBatchesFull.Empty();
			ShaderBatchesIncomplete.Empty(FASTBuildShaderCompilerVariables::BatchGroupSize);

			FString ScriptFilename = FASTBuildWorkingDirectory / FString::FromInt(FASTBuildDirectoryIndex) / FASTBuild_ScriptFileName;

			IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();
			FString ScriptFDBFilename = FASTBuildWorkingDirectory / FString::FromInt(FASTBuildDirectoryIndex) / FASTBuild_FDBFileName;
			if (platformFile.FileExists(*ScriptFDBFilename))
			{
				platformFile.DeleteFile(*ScriptFDBFilename);
			}


			// Create the XGE script file.
			FArchive* ScriptFile = CreateFileHelper(ScriptFilename);
			WriteScriptFileHeader(ScriptFile, Manager->ShaderCompileWorkerName);

			// Write the XML task line for each shader batch
			for (FShaderBatch* Batch : ShaderBatchesInFlight)
			{
				FString WorkerAbsoluteDirectory = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*Batch->WorkingDirectory);
				FPaths::NormalizeDirectoryName(WorkerAbsoluteDirectory);

				FString ExecFunction = FString::Printf(
					TEXT("ObjectList('ShaderBatch-%d')\r\n")
					TEXT("{\r\n")
					TEXT("\t.Compiler = 'ShaderCompiler'\r\n")
					TEXT("\t.CompilerOptions = '\"\" %d %d \"%%1\" \"%%2\" -xge_xml %s'\r\n")
					TEXT("\t.CompilerOutputExtension = '.out'\r\n")
					TEXT("\t.CompilerInputFiles = { '%s' }\r\n")
					TEXT("\t.CompilerOutputPath = '%s'\r\n")
					TEXT("\t.GenerateSuccessFile = '%s'\r\n")
					TEXT("}\r\n\r\n"),
					Batch->BatchIndex,
					Manager->ProcessId,
					Batch->BatchIndex,
					*FCommandLine::GetSubprocessCommandline(),
					*Batch->InputFileNameAndPath,
					*WorkerAbsoluteDirectory,
					*Batch->SuccessFileNameAndPath);

				ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*ExecFunction, ExecFunction.Len()).Get(), sizeof(ANSICHAR) * ExecFunction.Len());
			}

			// 生成 All Targets 信息
			FString AliasBuildTargetOpen = FString(
				TEXT("Alias('all')\r\n")
				TEXT("{\r\n")
				TEXT("\t.Targets = {\r\n")
			);

			ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*AliasBuildTargetOpen, AliasBuildTargetOpen.Len()).Get(), sizeof(ANSICHAR) * AliasBuildTargetOpen.Len());
			for (FShaderBatch* Batch : ShaderBatchesInFlight)
			{
				FString TargetExport = FString::Printf(TEXT("'ShaderBatch-%d',\n"), Batch->BatchIndex);
				ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*TargetExport, TargetExport.Len()).Get(), sizeof(ANSICHAR) * TargetExport.Len());
			}

			FString AliasBuildTargetClose = FString(
				TEXT("\t}\r\n")
				TEXT("}\r\n")
			);
			ScriptFile->Serialize((void*)StringCast<ANSICHAR>(*AliasBuildTargetClose, AliasBuildTargetClose.Len()).Get(), sizeof(ANSICHAR) * AliasBuildTargetClose.Len());


			// End script file and close it.
			delete ScriptFile;
			ScriptFile = nullptr;

			// Grab the timestamp from the script file.
			// We use this to ignore any left over files from previous builds by only accepting files created after the script file.
			ScriptFileCreationTime = IFileManager::Get().GetTimeStamp(*ScriptFilename);

			StartTime = FPlatformTime::Cycles();

			// Use stop on errors so we can respond to shader compile worker crashes immediately.
			// Regular shader compilation errors are not returned as worker errors.
			FString FASTBuildConsoleArgs = FString::Printf(TEXT("-config \"%s\" -dist -monitor -j%d -cache"), *ScriptFilename, Manager->NumShaderCompilingThreads);

			// Kick off the XGE process...
			BuildProcessHandle = FPlatformProcess::CreateProc(*FASTBuild_ConsolePath, *FASTBuildConsoleArgs, false, false, true, &BuildProcessID, 0, nullptr, nullptr);
			if (!BuildProcessHandle.IsValid())
			{
				UE_LOG(LogShaderCompilers, Fatal, TEXT("Failed to launch %s during shader compilation."), *FASTBuild_ConsolePath);
			}

			// If the engine crashes, we don't get a chance to kill the build process.
			// Start up the build monitor process to monitor for engine crashes.
			uint32 BuildMonitorProcessID;
			FProcHandle BuildMonitorHandle = FPlatformProcess::CreateProc(*Manager->ShaderCompileWorkerName, *FString::Printf(TEXT("-xgemonitor %d %d"), Manager->ProcessId, BuildProcessID), true, false, false, &BuildMonitorProcessID, 0, nullptr, nullptr);
			FPlatformProcess::CloseProc(BuildMonitorHandle);

			// Reset batch counters and switch directories
			BatchIndexToFill = 0;
			BatchIndexToCreate = 0;
			FASTBuildDirectoryIndex = 1 - FASTBuildDirectoryIndex;

			bWorkRemaining = true;
		}
	}

	// Try to prepare more shader jobs (even if a build is in flight).
	TArray<TSharedRef<FShaderCommonCompileJob, ESPMode::ThreadSafe>> JobQueue;
	{
		// Enter the critical section so we can access the input and output queues
		FScopeLock Lock(&Manager->CompileQueueSection);

		// Grab as many jobs from the job queue as we can.
		int32 NumNewJobs = Manager->CompileQueue.Num();
		if (NumNewJobs > 0)
		{
			//	int32 DestJobIndex = JobQueue.SetNum(NumNewJobs);
			//	for (int32 SrcJobIndex = 0; SrcJobIndex < NumNewJobs; SrcJobIndex++, DestJobIndex++)
			//	{
			//		JobQueue[DestJobIndex] = Manager->CompileQueue[SrcJobIndex];
			//	}

			////	Manager->CompileQueue.RemoveAt(0, NumNewJobs);
			////	Manager->CompileQueue;

			JobQueue = Manager->CompileQueue;
			Manager->CompileQueue.RemoveAt(0, NumNewJobs);

		}
	}

	if (JobQueue.Num() > 0)
	{
		// We have new jobs in the queue.
		// Group the jobs into batches and create the worker input files.
		for (int32 JobIndex = 0; JobIndex < JobQueue.Num(); JobIndex++)
		{
			if (BatchIndexToFill >= ShaderBatchesIncomplete.GetMaxIndex() || !ShaderBatchesIncomplete.IsAllocated(BatchIndexToFill))
			{
				// There are no more incomplete shader batches available.
				// Create another one...
				ShaderBatchesIncomplete.Insert(BatchIndexToFill, new FShaderBatch(
					FASTBuildWorkingDirectory,
					FASTBuild_InputFileName,
					FASTBuild_SuccessFileName,
					FASTBuild_OutputFileName,
					FASTBuildDirectoryIndex,
					BatchIndexToCreate));

				BatchIndexToCreate++;
			}

			// Add a single job to this batch
			FShaderBatch* CurrentBatch = ShaderBatchesIncomplete[BatchIndexToFill];
			CurrentBatch->AddJob(JobQueue[JobIndex]);

			// If the batch is now full...
			if (CurrentBatch->NumJobs() == FASTBuildShaderCompilerVariables::BatchSize)
			{
				CurrentBatch->WriteTransferFile();

				// Move the batch to the full list.
				ShaderBatchesFull.Add(CurrentBatch);
				ShaderBatchesIncomplete.RemoveAt(BatchIndexToFill);
			}

			BatchIndexToFill++;
			BatchIndexToFill %= FASTBuildShaderCompilerVariables::BatchGroupSize;
		}

		// Keep track of the last time we added jobs.
		LastAddTime = FPlatformTime::Cycles();

		bWorkRemaining = true;
	}

	if (Manager->bAllowAsynchronousShaderCompiling)
	{
		// Yield for a short while to stop this thread continuously polling the disk.
		FPlatformProcess::Sleep(0.01f);
	}

	return bWorkRemaining ? 1 : 0;
}