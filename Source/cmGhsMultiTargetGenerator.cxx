/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmGhsMultiTargetGenerator.h"

#include "cmCustomCommand.h"
#include "cmCustomCommandGenerator.h"
#include "cmGeneratedFileStream.h"
#include "cmGeneratorTarget.h"
#include "cmGlobalGhsMultiGenerator.h"
#include "cmLinkLineComputer.h"
#include "cmLocalGenerator.h"
#include "cmLocalGhsMultiGenerator.h"
#include "cmMakefile.h"
#include "cmOutputConverter.h"
#include "cmSourceFile.h"
#include "cmSourceFileLocation.h"
#include "cmSourceGroup.h"
#include "cmStateDirectory.h"
#include "cmStateSnapshot.h"
#include "cmStateTypes.h"
#include "cmStringAlgorithms.h"
#include "cmSystemTools.h"
#include "cmTarget.h"

#include <algorithm>
#include <memory>
#include <ostream>
#include <set>
#include <utility>
#include <vector>

cmGhsMultiTargetGenerator::cmGhsMultiTargetGenerator(cmGeneratorTarget* target)
  : GeneratorTarget(target)
  , LocalGenerator(
      static_cast<cmLocalGhsMultiGenerator*>(target->GetLocalGenerator()))
  , Makefile(target->Target->GetMakefile())
  , Name(target->GetName())
#ifdef _WIN32
  , CmdWindowsShell(true)
#else
  , CmdWindowsShell(false)
#endif
{
  // Store the configuration name that is being used
  if (const char* config = this->Makefile->GetDefinition("CMAKE_BUILD_TYPE")) {
    // Use the build type given by the user.
    this->ConfigName = config;
  } else {
    // No configuration type given.
    this->ConfigName.clear();
  }
}

cmGhsMultiTargetGenerator::~cmGhsMultiTargetGenerator() = default;

void cmGhsMultiTargetGenerator::Generate()
{
  // Determine type of target for this project
  switch (this->GeneratorTarget->GetType()) {
    case cmStateEnums::EXECUTABLE: {
      // Get the name of the executable to generate.
      this->TargetNameReal =
        this->GeneratorTarget->GetExecutableNames(this->ConfigName).Real;
      if (cmGhsMultiTargetGenerator::DetermineIfIntegrityApp()) {
        this->TagType = GhsMultiGpj::INTERGRITY_APPLICATION;
      } else {
        this->TagType = GhsMultiGpj::PROGRAM;
      }
      break;
    }
    case cmStateEnums::STATIC_LIBRARY: {
      this->TargetNameReal =
        this->GeneratorTarget->GetLibraryNames(this->ConfigName).Real;
      this->TagType = GhsMultiGpj::LIBRARY;
      break;
    }
    case cmStateEnums::SHARED_LIBRARY: {
      std::string msg =
        cmStrCat("add_library(<name> SHARED ...) not supported: ", this->Name);
      cmSystemTools::Message(msg);
      return;
    }
    case cmStateEnums::OBJECT_LIBRARY: {
      this->TargetNameReal =
        this->GeneratorTarget->GetLibraryNames(this->ConfigName).Real;
      this->TagType = GhsMultiGpj::SUBPROJECT;
      break;
    }
    case cmStateEnums::MODULE_LIBRARY: {
      std::string msg =
        cmStrCat("add_library(<name> MODULE ...) not supported: ", this->Name);
      cmSystemTools::Message(msg);
      return;
    }
    case cmStateEnums::UTILITY: {
      this->TargetNameReal = this->GeneratorTarget->GetName();
      this->TagType = GhsMultiGpj::CUSTOM_TARGET;
      break;
    }
    case cmStateEnums::GLOBAL_TARGET: {
      this->TargetNameReal = this->GeneratorTarget->GetName();
      if (this->TargetNameReal ==
          this->GetGlobalGenerator()->GetInstallTargetName()) {
        this->TagType = GhsMultiGpj::CUSTOM_TARGET;
      } else {
        return;
      }
      break;
    }
    default:
      return;
  }

  // Tell the global generator the name of the project file
  this->GeneratorTarget->Target->SetProperty("GENERATOR_FILE_NAME",
                                             this->Name.c_str());
  this->GeneratorTarget->Target->SetProperty(
    "GENERATOR_FILE_NAME_EXT", GhsMultiGpj::GetGpjTag(this->TagType));

  this->GenerateTarget();
}

void cmGhsMultiTargetGenerator::GenerateTarget()
{
  // Open the target file in copy-if-different mode.
  std::string fproj =
    cmStrCat(this->LocalGenerator->GetCurrentBinaryDirectory(), '/',
             this->Name, cmGlobalGhsMultiGenerator::FILE_EXTENSION);
  cmGeneratedFileStream fout(fproj);
  fout.SetCopyIfDifferent(true);

  this->GetGlobalGenerator()->WriteFileHeader(fout);
  GhsMultiGpj::WriteGpjTag(this->TagType, fout);

  if (this->TagType != GhsMultiGpj::CUSTOM_TARGET) {
    const std::string language(
      this->GeneratorTarget->GetLinkerLanguage(this->ConfigName));
    this->WriteTargetSpecifics(fout, this->ConfigName);
    this->SetCompilerFlags(this->ConfigName, language);
    this->WriteCompilerFlags(fout, this->ConfigName, language);
    this->WriteCompilerDefinitions(fout, this->ConfigName, language);
    this->WriteIncludes(fout, this->ConfigName, language);
    this->WriteTargetLinkLine(fout, this->ConfigName);
    this->WriteBuildEvents(fout);
  }
  this->WriteSources(fout);
  fout.Close();
}

cmGlobalGhsMultiGenerator* cmGhsMultiTargetGenerator::GetGlobalGenerator()
  const
{
  return static_cast<cmGlobalGhsMultiGenerator*>(
    this->LocalGenerator->GetGlobalGenerator());
}

void cmGhsMultiTargetGenerator::WriteTargetSpecifics(std::ostream& fout,
                                                     const std::string& config)
{
  std::string outpath;
  std::string rootpath = this->LocalGenerator->GetCurrentBinaryDirectory();

  if (this->TagType != GhsMultiGpj::SUBPROJECT) {
    // set target binary file destination
    outpath = this->GeneratorTarget->GetDirectory(config);
    outpath =
      this->LocalGenerator->MaybeConvertToRelativePath(rootpath, outpath);
    fout << "    :binDirRelative=\"" << outpath << "\"" << std::endl;
    fout << "    -o \"" << this->TargetNameReal << "\"" << std::endl;
  }

  // set target object file destination
  outpath = this->LocalGenerator->GetTargetDirectory(this->GeneratorTarget);
  fout << "    :outputDirRelative=\"" << outpath << "\"" << std::endl;
}

void cmGhsMultiTargetGenerator::SetCompilerFlags(std::string const& config,
                                                 const std::string& language)
{
  auto i = this->FlagsByLanguage.find(language);
  if (i == this->FlagsByLanguage.end()) {
    std::string flags;
    const char* lang = language.c_str();

    this->LocalGenerator->AddLanguageFlags(flags, this->GeneratorTarget, lang,
                                           config);

    this->LocalGenerator->AddCMP0018Flags(flags, this->GeneratorTarget, lang,
                                          config);
    this->LocalGenerator->AddVisibilityPresetFlags(
      flags, this->GeneratorTarget, lang);

    // Append old-style preprocessor definition flags.
    if (this->Makefile->GetDefineFlags() != " ") {
      this->LocalGenerator->AppendFlags(flags,
                                        this->Makefile->GetDefineFlags());
    }

    // Add target-specific flags.
    this->LocalGenerator->AddCompileOptions(flags, this->GeneratorTarget, lang,
                                            config);

    std::map<std::string, std::string>::value_type entry(language, flags);
    i = this->FlagsByLanguage.insert(entry).first;
  }
}

std::string cmGhsMultiTargetGenerator::GetDefines(const std::string& language,
                                                  std::string const& config)
{
  auto i = this->DefinesByLanguage.find(language);
  if (i == this->DefinesByLanguage.end()) {
    std::set<std::string> defines;
    const char* lang = language.c_str();
    // Add preprocessor definitions for this target and configuration.
    this->LocalGenerator->GetTargetDefines(this->GeneratorTarget, config,
                                           language, defines);

    std::string definesString;
    this->LocalGenerator->JoinDefines(defines, definesString, lang);

    std::map<std::string, std::string>::value_type entry(language,
                                                         definesString);
    i = this->DefinesByLanguage.insert(entry).first;
  }
  return i->second;
}

void cmGhsMultiTargetGenerator::WriteCompilerFlags(std::ostream& fout,
                                                   std::string const&,
                                                   const std::string& language)
{
  auto flagsByLangI = this->FlagsByLanguage.find(language);
  if (flagsByLangI != this->FlagsByLanguage.end()) {
    if (!flagsByLangI->second.empty()) {
      std::vector<std::string> ghsCompFlags =
        cmSystemTools::ParseArguments(flagsByLangI->second);
      for (auto& f : ghsCompFlags) {
        fout << "    " << f << std::endl;
      }
    }
  }
}

void cmGhsMultiTargetGenerator::WriteCompilerDefinitions(
  std::ostream& fout, const std::string& config, const std::string& language)
{
  std::vector<std::string> compileDefinitions;
  this->GeneratorTarget->GetCompileDefinitions(compileDefinitions, config,
                                               language);
  for (std::string const& compileDefinition : compileDefinitions) {
    fout << "    -D" << compileDefinition << std::endl;
  }
}

void cmGhsMultiTargetGenerator::WriteIncludes(std::ostream& fout,
                                              const std::string& config,
                                              const std::string& language)
{
  std::vector<std::string> includes;
  this->LocalGenerator->GetIncludeDirectories(includes, this->GeneratorTarget,
                                              language, config);

  for (std::string const& include : includes) {
    fout << "    -I\"" << include << "\"" << std::endl;
  }
}

void cmGhsMultiTargetGenerator::WriteTargetLinkLine(std::ostream& fout,
                                                    std::string const& config)
{
  if (this->TagType == GhsMultiGpj::INTERGRITY_APPLICATION) {
    return;
  }

  std::string linkLibraries;
  std::string flags;
  std::string linkFlags;
  std::string frameworkPath;
  std::string linkPath;

  std::unique_ptr<cmLinkLineComputer> linkLineComputer(
    this->GetGlobalGenerator()->CreateLinkLineComputer(
      this->LocalGenerator,
      this->LocalGenerator->GetStateSnapshot().GetDirectory()));

  this->LocalGenerator->GetTargetFlags(
    linkLineComputer.get(), config, linkLibraries, flags, linkFlags,
    frameworkPath, linkPath, this->GeneratorTarget);

  // write out link options
  std::vector<std::string> lopts = cmSystemTools::ParseArguments(linkFlags);
  for (auto& l : lopts) {
    fout << "    " << l << std::endl;
  }

  // write out link search paths
  // must be quoted for paths that contain spaces
  std::vector<std::string> lpath = cmSystemTools::ParseArguments(linkPath);
  for (auto& l : lpath) {
    fout << "    -L\"" << l << "\"" << std::endl;
  }

  // write out link libs
  // must be quoted for filepaths that contains spaces
  std::string cbd = this->LocalGenerator->GetCurrentBinaryDirectory();

  std::vector<std::string> llibs =
    cmSystemTools::ParseArguments(linkLibraries);
  for (auto& l : llibs) {
    if (l.compare(0, 2, "-l") == 0) {
      fout << "    \"" << l << "\"" << std::endl;
    } else {
      std::string rl = cmSystemTools::CollapseFullPath(l, cbd);
      fout << "    -l\"" << rl << "\"" << std::endl;
    }
  }
}

void cmGhsMultiTargetGenerator::WriteBuildEvents(std::ostream& fout)
{
  this->WriteBuildEventsHelper(
    fout, this->GeneratorTarget->GetPreBuildCommands(),
    std::string("prebuild"), std::string("preexecShell"));

  if (this->TagType != GhsMultiGpj::CUSTOM_TARGET) {
    this->WriteBuildEventsHelper(
      fout, this->GeneratorTarget->GetPreLinkCommands(),
      std::string("prelink"), std::string("preexecShell"));
  }

  this->WriteBuildEventsHelper(
    fout, this->GeneratorTarget->GetPostBuildCommands(),
    std::string("postbuild"), std::string("postexecShell"));
}

void cmGhsMultiTargetGenerator::WriteBuildEventsHelper(
  std::ostream& fout, const std::vector<cmCustomCommand>& ccv,
  std::string const& name, std::string const& cmd)
{
  int cmdcount = 0;

  for (cmCustomCommand const& cc : ccv) {
    cmCustomCommandGenerator ccg(cc, this->ConfigName, this->LocalGenerator);
    // Open the filestream for this custom command
    std::string fname =
      cmStrCat(this->LocalGenerator->GetCurrentBinaryDirectory(), '/',
               this->LocalGenerator->GetTargetDirectory(this->GeneratorTarget),
               '/', this->Name, '_', name, cmdcount++,
               this->CmdWindowsShell ? ".bat" : ".sh");
    cmGeneratedFileStream f(fname);
    f.SetCopyIfDifferent(true);
    this->WriteCustomCommandsHelper(f, ccg);
    f.Close();
    if (this->TagType != GhsMultiGpj::CUSTOM_TARGET) {
      fout << "    :" << cmd << "=\"" << fname << "\"" << std::endl;
    } else {
      fout << fname << std::endl;
      fout << "    :outputName=\"" << fname << ".rule\"" << std::endl;
    }
    for (auto& byp : ccg.GetByproducts()) {
      fout << "    :extraOutputFile=\"" << byp << "\"" << std::endl;
    }
  }
}

void cmGhsMultiTargetGenerator::WriteCustomCommandsHelper(
  std::ostream& fout, cmCustomCommandGenerator const& ccg)
{
  std::vector<std::string> cmdLines;

  // if the command specified a working directory use it.
  std::string dir = this->LocalGenerator->GetCurrentBinaryDirectory();
  std::string currentBinDir = dir;
  std::string workingDir = ccg.GetWorkingDirectory();
  if (!workingDir.empty()) {
    dir = workingDir;
  }

  // Line to check for error between commands.
#ifdef _WIN32
  std::string check_error = "if %errorlevel% neq 0 exit /b %errorlevel%";
#else
  std::string check_error = "if [[ $? -ne 0 ]]; then exit 1; fi";
#endif

#ifdef _WIN32
  cmdLines.push_back("@echo off");
#endif
  // Echo the custom command's comment text.
  const char* comment = ccg.GetComment();
  if (comment && *comment) {
    std::string echocmd = cmStrCat("echo ", comment);
    cmdLines.push_back(std::move(echocmd));
  }

  // Switch to working directory
  std::string cdCmd;
#ifdef _WIN32
  std::string cdStr = "cd /D ";
#else
  std::string cdStr = "cd ";
#endif
  cdCmd = cdStr +
    this->LocalGenerator->ConvertToOutputFormat(dir, cmOutputConverter::SHELL);
  cmdLines.push_back(std::move(cdCmd));

  for (unsigned int c = 0; c < ccg.GetNumberOfCommands(); ++c) {
    // Build the command line in a single string.
    std::string cmd = ccg.GetCommand(c);
    if (!cmd.empty()) {
      // Use "call " before any invocations of .bat or .cmd files
      // invoked as custom commands in the WindowsShell.
      //
      bool useCall = false;

      if (this->CmdWindowsShell) {
        std::string suffix;
        if (cmd.size() > 4) {
          suffix = cmSystemTools::LowerCase(cmd.substr(cmd.size() - 4));
          if (suffix == ".bat" || suffix == ".cmd") {
            useCall = true;
          }
        }
      }

      cmSystemTools::ReplaceString(cmd, "/./", "/");
      // Convert the command to a relative path only if the current
      // working directory will be the start-output directory.
      bool had_slash = cmd.find('/') != std::string::npos;
      if (workingDir.empty()) {
        cmd =
          this->LocalGenerator->MaybeConvertToRelativePath(currentBinDir, cmd);
      }
      bool has_slash = cmd.find('/') != std::string::npos;
      if (had_slash && !has_slash) {
        // This command was specified as a path to a file in the
        // current directory.  Add a leading "./" so it can run
        // without the current directory being in the search path.
        cmd = cmStrCat("./", cmd);
      }
      cmd = this->LocalGenerator->ConvertToOutputFormat(
        cmd, cmOutputConverter::SHELL);
      if (useCall) {
        cmd = cmStrCat("call ", cmd);
      }
      ccg.AppendArguments(c, cmd);
      cmdLines.push_back(std::move(cmd));
    }
  }

  // push back the custom commands
  for (auto const& c : cmdLines) {
    fout << c << std::endl;
    fout << check_error << std::endl;
  }
}

void cmGhsMultiTargetGenerator::WriteSourceProperty(
  std::ostream& fout, const cmSourceFile* sf, std::string const& propName,
  std::string const& propFlag)
{
  const char* prop = sf->GetProperty(propName);
  if (prop) {
    std::vector<std::string> list = cmExpandedList(prop);
    for (auto& p : list) {
      fout << "    " << propFlag << p << std::endl;
    }
  }
}

void cmGhsMultiTargetGenerator::WriteSources(std::ostream& fout_proj)
{
  /* vector of all sources for this target */
  std::vector<cmSourceFile*> sources;
  this->GeneratorTarget->GetSourceFiles(sources, this->ConfigName);

  /* vector of all groups defined for this target
   * -- but the vector is not expanded with sub groups or in any useful order
   */
  std::vector<cmSourceGroup> sourceGroups = this->Makefile->GetSourceGroups();

  /* for each source file assign it to its group */
  std::map<std::string, std::vector<cmSourceFile*>> groupFiles;
  std::set<std::string> groupNames;
  for (auto& sf : sources) {
    cmSourceGroup* sourceGroup =
      this->Makefile->FindSourceGroup(sf->ResolveFullPath(), sourceGroups);
    std::string gn = sourceGroup->GetFullName();
    groupFiles[gn].push_back(sf);
    groupNames.insert(std::move(gn));
  }

  /* list of known groups and the order they are displayed in a project file */
  const std::vector<std::string> standardGroups = {
    "CMake Rules",  "Header Files",     "Source Files",
    "Object Files", "Object Libraries", "Resources"
  };

  /* list of groups in the order they are displayed in a project file*/
  std::vector<std::string> groupFilesList(groupFiles.size());

  /* put the groups in the order they should be listed
   * - standard groups first, and then everything else
   *   in the order used by std::map.
   */
  int i = 0;
  for (const std::string& gn : standardGroups) {
    auto n = groupNames.find(gn);
    if (n != groupNames.end()) {
      groupFilesList[i] = *n;
      i += 1;
      groupNames.erase(gn);
    } else if (this->TagType == GhsMultiGpj::CUSTOM_TARGET &&
               gn == "CMake Rules") {
      /* make sure that rules folder always exists in case of custom targets
       * that have no custom commands except for pre or post build events.
       */
      groupFilesList.resize(groupFilesList.size() + 1);
      groupFilesList[i] = gn;
      i += 1;
    }
  }

  { /* catch-all group - is last item */
    std::string gn;
    auto n = groupNames.find(gn);
    if (n != groupNames.end()) {
      groupFilesList.back() = *n;
      groupNames.erase(gn);
    }
  }

  for (auto& n : groupNames) {
    groupFilesList[i] = n;
    i += 1;
  }

  /* sort the files within each group */
  for (auto& n : groupFilesList) {
    std::sort(groupFiles[n].begin(), groupFiles[n].end(),
              [](cmSourceFile* l, cmSourceFile* r) {
                return l->ResolveFullPath() < r->ResolveFullPath();
              });
  }

  /* list of open project files */
  std::vector<cmGeneratedFileStream*> gfiles;

  /* write files into the proper project file
   * -- groups go into main project file
   *    unless NO_SOURCE_GROUP_FILE property or variable is set.
   */
  for (auto& sg : groupFilesList) {
    std::ostream* fout;
    bool useProjectFile =
      cmIsOn(this->GeneratorTarget->GetProperty("GHS_NO_SOURCE_GROUP_FILE")) ||
      cmIsOn(this->Makefile->GetDefinition("CMAKE_GHS_NO_SOURCE_GROUP_FILE"));
    if (useProjectFile || sg.empty()) {
      fout = &fout_proj;
    } else {
      // Open the filestream in copy-if-different mode.
      std::string gname = sg;
      cmsys::SystemTools::ReplaceString(gname, "\\", "_");
      std::string lpath = cmStrCat(
        this->LocalGenerator->GetTargetDirectory(this->GeneratorTarget), '/',
        gname, cmGlobalGhsMultiGenerator::FILE_EXTENSION);
      std::string fpath = cmStrCat(
        this->LocalGenerator->GetCurrentBinaryDirectory(), '/', lpath);
      cmGeneratedFileStream* f = new cmGeneratedFileStream(fpath);
      f->SetCopyIfDifferent(true);
      gfiles.push_back(f);
      fout = f;
      this->GetGlobalGenerator()->WriteFileHeader(*f);
      GhsMultiGpj::WriteGpjTag(GhsMultiGpj::SUBPROJECT, *f);
      fout_proj << lpath << " ";
      GhsMultiGpj::WriteGpjTag(GhsMultiGpj::SUBPROJECT, fout_proj);
    }

    if (useProjectFile) {
      if (sg.empty()) {
        *fout << "{comment} Others" << std::endl;
      } else {
        *fout << "{comment} " << sg << std::endl;
      }
    } else if (sg.empty()) {
      *fout << "{comment} Others" << std::endl;
    }

    if (sg != "CMake Rules") {
      /* output rule for each source file */
      for (const cmSourceFile* si : groupFiles[sg]) {
        bool compile = true;
        // Convert filename to native system
        // WORKAROUND: GHS MULTI 6.1.4 and 6.1.6 are known to need backslash on
        // windows when opening some files from the search window.
        std::string fname(si->GetFullPath());
        cmSystemTools::ConvertToOutputSlashes(fname);

        /* For custom targets list any associated sources,
         * comment out source code to prevent it from being
         * compiled when processing this target.
         * Otherwise, comment out any custom command (main) dependencies that
         * are listed as source files to prevent them from being considered
         * part of the build.
         */
        std::string comment;
        if ((this->TagType == GhsMultiGpj::CUSTOM_TARGET &&
             !si->GetLanguage().empty()) ||
            si->GetCustomCommand()) {
          comment = "{comment} ";
          compile = false;
        }

        *fout << comment << fname << std::endl;
        if (compile) {
          if ("ld" != si->GetExtension() && "int" != si->GetExtension() &&
              "bsp" != si->GetExtension()) {
            WriteObjectLangOverride(*fout, si);
          }

          this->WriteSourceProperty(*fout, si, "INCLUDE_DIRECTORIES", "-I");
          this->WriteSourceProperty(*fout, si, "COMPILE_DEFINITIONS", "-D");
          this->WriteSourceProperty(*fout, si, "COMPILE_OPTIONS", "");

          /* to avoid clutter in the GUI only print out the objectName if it
           * has been renamed */
          std::string objectName = this->GeneratorTarget->GetObjectName(si);
          if (!objectName.empty() &&
              this->GeneratorTarget->HasExplicitObjectName(si)) {
            *fout << "    -o " << objectName << std::endl;
          }
        }
      }
    } else {
      std::vector<cmSourceFile const*> customCommands;
      if (ComputeCustomCommandOrder(customCommands)) {
        std::string message = "The custom commands for target [" +
          this->GeneratorTarget->GetName() + "] had a cycle.\n";
        cmSystemTools::Error(message);
      } else {
        /* Custom targets do not have a dependency on SOURCES files.
         * Therefore the dependency list may include SOURCES files after the
         * custom target. Because nothing can depend on the custom target just
         * move it to the last item.
         */
        for (auto sf = customCommands.begin(); sf != customCommands.end();
             ++sf) {
          if (((*sf)->GetLocation()).GetName() == this->Name + ".rule") {
            std::rotate(sf, sf + 1, customCommands.end());
            break;
          }
        }
        int cmdcount = 0;
        for (auto& sf : customCommands) {
          const cmCustomCommand* cc = sf->GetCustomCommand();
          cmCustomCommandGenerator ccg(*cc, this->ConfigName,
                                       this->LocalGenerator);

          // Open the filestream for this custom command
          std::string fname = cmStrCat(
            this->LocalGenerator->GetCurrentBinaryDirectory(), '/',
            this->LocalGenerator->GetTargetDirectory(this->GeneratorTarget),
            '/', this->Name, "_cc", cmdcount++, '_',
            (sf->GetLocation()).GetName(),
            this->CmdWindowsShell ? ".bat" : ".sh");
          cmGeneratedFileStream f(fname);
          f.SetCopyIfDifferent(true);
          this->WriteCustomCommandsHelper(f, ccg);
          f.Close();
          this->WriteCustomCommandLine(*fout, fname, ccg);
        }
      }
      if (this->TagType == GhsMultiGpj::CUSTOM_TARGET) {
        this->WriteBuildEvents(*fout);
      }
    }
  }

  for (cmGeneratedFileStream* f : gfiles) {
    f->Close();
  }
}

void cmGhsMultiTargetGenerator::WriteCustomCommandLine(
  std::ostream& fout, std::string& fname, cmCustomCommandGenerator const& ccg)
{
  /* NOTE: Customization Files are not well documented.  Testing showed
   * that ":outputName=file" can only be used once per script.  The
   * script will only run if ":outputName=file" is missing or just run
   * once if ":outputName=file" is not specified.  If there are
   * multiple outputs then the script needs to be listed multiple times
   * for each output.  Otherwise it won't rerun the script if one of
   * the outputs is manually deleted.
   */
  bool specifyExtra = true;
  for (auto& out : ccg.GetOutputs()) {
    fout << fname << std::endl;
    fout << "    :outputName=\"" << out << "\"" << std::endl;
    if (specifyExtra) {
      for (auto& byp : ccg.GetByproducts()) {
        fout << "    :extraOutputFile=\"" << byp << "\"" << std::endl;
      }
      for (auto& dep : ccg.GetDepends()) {
        fout << "    :depends=\"" << dep << "\"" << std::endl;
      }
      specifyExtra = false;
    }
  }
}

void cmGhsMultiTargetGenerator::WriteObjectLangOverride(
  std::ostream& fout, const cmSourceFile* sourceFile)
{
  const char* rawLangProp = sourceFile->GetProperty("LANGUAGE");
  if (nullptr != rawLangProp) {
    std::string sourceLangProp(rawLangProp);
    std::string const& extension = sourceFile->GetExtension();
    if ("CXX" == sourceLangProp && ("c" == extension || "C" == extension)) {
      fout << "    -dotciscxx" << std::endl;
    }
  }
}

bool cmGhsMultiTargetGenerator::DetermineIfIntegrityApp()
{
  const char* p = this->GeneratorTarget->GetProperty("ghs_integrity_app");
  if (p) {
    return cmIsOn(this->GeneratorTarget->GetProperty("ghs_integrity_app"));
  }
  std::vector<cmSourceFile*> sources;
  this->GeneratorTarget->GetSourceFiles(sources, this->ConfigName);
  for (auto& sf : sources) {
    if ("int" == sf->GetExtension()) {
      return true;
    }
  }
  return false;
}

bool cmGhsMultiTargetGenerator::ComputeCustomCommandOrder(
  std::vector<cmSourceFile const*>& order)
{
  std::set<cmSourceFile const*> temp;
  std::set<cmSourceFile const*> perm;

  // Collect all custom commands for this target
  std::vector<cmSourceFile const*> customCommands;
  this->GeneratorTarget->GetCustomCommands(customCommands, this->ConfigName);

  for (cmSourceFile const* si : customCommands) {
    bool r = VisitCustomCommand(temp, perm, order, si);
    if (r) {
      return r;
    }
  }
  return false;
}

bool cmGhsMultiTargetGenerator::VisitCustomCommand(
  std::set<cmSourceFile const*>& temp, std::set<cmSourceFile const*>& perm,
  std::vector<cmSourceFile const*>& order, cmSourceFile const* si)
{
  /* check if permanent mark is set*/
  if (perm.find(si) == perm.end()) {
    /* set temporary mark; check if revisit*/
    if (temp.insert(si).second) {
      for (auto& di : si->GetCustomCommand()->GetDepends()) {
        cmSourceFile const* sf = this->GeneratorTarget->GetLocalGenerator()
                                   ->GetMakefile()
                                   ->GetSourceFileWithOutput(di);
        /* if sf exists then visit */
        if (sf && this->VisitCustomCommand(temp, perm, order, sf)) {
          return true;
        }
      }
      /* mark as complete; insert into beginning of list*/
      perm.insert(si);
      order.push_back(si);
      return false;
    }
    /* revisiting item - not a DAG */
    return true;
  }
  /* already complete */
  return false;
}
