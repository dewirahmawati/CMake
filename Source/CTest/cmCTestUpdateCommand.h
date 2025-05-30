/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef cmCTestUpdateCommand_h
#define cmCTestUpdateCommand_h

#include "cmConfigure.h" // IWYU pragma: keep

#include "cmCTestHandlerCommand.h"
#include "cmCommand.h"

#include <string>
#include <utility>

#include <cm/memory>

class cmCTestGenericHandler;

/** \class cmCTestUpdate
 * \brief Run a ctest script
 *
 * cmCTestUpdateCommand defineds the command to updates the repository.
 */
class cmCTestUpdateCommand : public cmCTestHandlerCommand
{
public:
  /**
   * This is a virtual constructor for the command.
   */
  std::unique_ptr<cmCommand> Clone() override
  {
    auto ni = cm::make_unique<cmCTestUpdateCommand>();
    ni->CTest = this->CTest;
    ni->CTestScriptHandler = this->CTestScriptHandler;
    return std::unique_ptr<cmCommand>(std::move(ni));
  }

  /**
   * The name of the command as specified in CMakeList.txt.
   */
  std::string GetName() const override { return "ctest_update"; }

protected:
  cmCTestGenericHandler* InitializeHandler() override;
};

#endif
