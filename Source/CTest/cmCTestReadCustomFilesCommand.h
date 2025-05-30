/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef cmCTestReadCustomFilesCommand_h
#define cmCTestReadCustomFilesCommand_h

#include "cmConfigure.h" // IWYU pragma: keep

#include "cmCTestCommand.h"
#include "cmCommand.h"

#include <string>
#include <utility>
#include <vector>

#include <cm/memory>

class cmExecutionStatus;

/** \class cmCTestReadCustomFiles
 * \brief Run a ctest script
 *
 * cmLibrarysCommand defines a list of executable (i.e., test)
 * programs to create.
 */
class cmCTestReadCustomFilesCommand : public cmCTestCommand
{
public:
  cmCTestReadCustomFilesCommand() {}

  /**
   * This is a virtual constructor for the command.
   */
  std::unique_ptr<cmCommand> Clone() override
  {
    auto ni = cm::make_unique<cmCTestReadCustomFilesCommand>();
    ni->CTest = this->CTest;
    return std::unique_ptr<cmCommand>(std::move(ni));
  }

  /**
   * This is called when the command is first encountered in
   * the CMakeLists.txt file.
   */
  bool InitialPass(std::vector<std::string> const& args,
                   cmExecutionStatus& status) override;
};

#endif
