/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef cmQtAutoGenInitializer_h
#define cmQtAutoGenInitializer_h

#include "cmConfigure.h" // IWYU pragma: keep
#include "cmFilePathChecksum.h"
#include "cmQtAutoGen.h"

#include <cm/string_view>

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class cmGeneratorTarget;
class cmGlobalGenerator;
class cmLocalGenerator;
class cmMakefile;
class cmQtAutoGenGlobalInitializer;
class cmSourceFile;
class cmTarget;

/** \class cmQtAutoGenerator
 * \brief Initializes the QtAutoGen generators
 */
class cmQtAutoGenInitializer : public cmQtAutoGen
{
public:
  /** String value with per configuration variants.  */
  class ConfigString
  {
  public:
    std::string Default;
    std::unordered_map<std::string, std::string> Config;
  };

  /** String values with per configuration variants.  */
  template <typename C>
  class ConfigStrings
  {
  public:
    C Default;
    std::unordered_map<std::string, C> Config;
  };

  /** rcc job.  */
  class Qrc
  {
  public:
    std::string LockFile;
    std::string QrcFile;
    std::string QrcName;
    std::string QrcPathChecksum;
    std::string InfoFile;
    ConfigString SettingsFile;
    std::string OutputFile;
    bool Generated = false;
    bool Unique = false;
    std::vector<std::string> Options;
    std::vector<std::string> Resources;
  };

  /** moc and/or uic file.  */
  struct MUFile
  {
    std::string FullPath;
    cmSourceFile* SF = nullptr;
    bool Generated = false;
    bool SkipMoc = false;
    bool SkipUic = false;
    bool MocIt = false;
    bool UicIt = false;
  };
  using MUFileHandle = std::unique_ptr<MUFile>;

  /** Abstract moc/uic/rcc generator variables base class.  */
  struct GenVarsT
  {
    bool Enabled = false;
    // Generator type/name
    GenT Gen;
    cm::string_view GenNameUpper;
    // Executable
    std::string ExecutableTargetName;
    cmGeneratorTarget* ExecutableTarget = nullptr;
    std::string Executable;
    CompilerFeaturesHandle ExecutableFeatures;

    GenVarsT(GenT gen)
      : Gen(gen)
      , GenNameUpper(cmQtAutoGen::GeneratorNameUpper(gen)){};
  };

public:
  /** @return The detected Qt version and the required Qt major version.  */
  static std::pair<IntegerVersion, unsigned int> GetQtVersion(
    cmGeneratorTarget const* genTarget);

  cmQtAutoGenInitializer(cmQtAutoGenGlobalInitializer* globalInitializer,
                         cmGeneratorTarget* genTarget,
                         IntegerVersion const& qtVersion, bool mocEnabled,
                         bool uicEnabled, bool rccEnabled,
                         bool globalAutogenTarget, bool globalAutoRccTarget);

  bool InitCustomTargets();
  bool SetupCustomTargets();

private:
  /** If moc or uic is enabled, the autogen target will be generated.  */
  bool MocOrUicEnabled() const
  {
    return (this->Moc.Enabled || this->Uic.Enabled);
  }

  bool InitMoc();
  bool InitUic();
  bool InitRcc();

  bool InitScanFiles();
  bool InitAutogenTarget();
  bool InitRccTargets();

  bool SetupWriteAutogenInfo();
  bool SetupWriteRccInfo();

  void RegisterGeneratedSource(std::string const& filename);
  bool AddGeneratedSource(std::string const& filename, GenVarsT const& genVars,
                          bool prepend = false);
  bool AddToSourceGroup(std::string const& fileName,
                        cm::string_view genNameUpper);
  void AddCleanFile(std::string const& fileName);

  void ConfigFileNames(ConfigString& configString, cm::string_view prefix,
                       cm::string_view suffix);
  void ConfigFileClean(ConfigString& configString);

  std::string GetMocBuildPath(MUFile const& muf);

  bool GetQtExecutable(GenVarsT& genVars, const std::string& executable,
                       bool ignoreMissingTarget) const;

private:
  cmQtAutoGenGlobalInitializer* GlobalInitializer = nullptr;
  cmGeneratorTarget* GenTarget = nullptr;
  cmGlobalGenerator* GlobalGen = nullptr;
  cmLocalGenerator* LocalGen = nullptr;
  cmMakefile* Makefile = nullptr;
  cmFilePathChecksum const PathCheckSum;

  // -- Configuration
  IntegerVersion QtVersion;
  unsigned int Verbosity = 0;
  bool MultiConfig = false;
  bool CMP0071Accept = false;
  bool CMP0071Warn = false;
  std::string ConfigDefault;
  std::vector<std::string> ConfigsList;
  std::string TargetsFolder;

  /** Common directories.  */
  struct
  {
    std::string Info;
    std::string Build;
    std::string Work;
    ConfigString Include;
    std::string IncludeGenExp;
  } Dir;

  /** Autogen target variables.  */
  struct
  {
    std::string Name;
    bool GlobalTarget = false;
    // Settings
    unsigned int Parallel = 1;
    // Configuration files
    std::string InfoFile;
    ConfigString SettingsFile;
    ConfigString ParseCacheFile;
    // Dependencies
    bool DependOrigin = false;
    std::set<std::string> DependFiles;
    std::set<cmTarget*> DependTargets;
    // Sources to process
    std::unordered_map<cmSourceFile*, MUFileHandle> Headers;
    std::unordered_map<cmSourceFile*, MUFileHandle> Sources;
    std::vector<MUFile*> FilesGenerated;
  } AutogenTarget;

  /** moc variables.  */
  struct MocT : public GenVarsT
  {
    MocT()
      : GenVarsT(GenT::MOC){};

    bool RelaxedMode = false;
    bool PathPrefix = false;
    std::string CompilationFile;
    // Compiler implicit pre defines
    std::vector<std::string> PredefsCmd;
    ConfigString PredefsFile;
    // Defines
    ConfigStrings<std::set<std::string>> Defines;
    // Includes
    ConfigStrings<std::vector<std::string>> Includes;
    // Options
    std::vector<std::string> Options;
    // Filters
    std::vector<std::string> MacroNames;
    std::vector<std::pair<std::string, std::string>> DependFilters;
    // Utility
    std::unordered_set<std::string> EmittedBuildPaths;
  } Moc;

  /** uic variables.  */
  struct UicT : public GenVarsT
  {
    using UiFileT = std::pair<std::string, std::vector<std::string>>;

    UicT()
      : GenVarsT(GenT::UIC){};

    std::set<std::string> SkipUi;
    std::vector<UiFileT> UiFiles;
    ConfigStrings<std::vector<std::string>> Options;
    std::vector<std::string> SearchPaths;
  } Uic;

  /** rcc variables.  */
  struct RccT : public GenVarsT
  {
    RccT()
      : GenVarsT(GenT::RCC){};

    bool GlobalTarget = false;
    std::vector<Qrc> Qrcs;
  } Rcc;
};

#endif
