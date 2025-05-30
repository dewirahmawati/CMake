/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef cmCheckCustomOutputs_h
#define cmCheckCustomOutputs_h

#include "cmConfigure.h" // IWYU pragma: keep

#include <cm/string_view>

#include <string>
#include <vector>

class cmExecutionStatus;

bool cmCheckCustomOutputs(const std::vector<std::string>& outputs,
                          cm::string_view keyword, cmExecutionStatus& status);

#endif
