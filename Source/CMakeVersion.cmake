# CMake version number components.
set(CMake_VERSION_MAJOR 3)
set(CMake_VERSION_MINOR 15)
set(CMake_VERSION_PATCH 20191001)
#set(CMake_VERSION_RC 0)
set(CMake_VERSION_IS_DIRTY 0)

# Start with the full version number used in tags.  It has no dev info.
set(CMake_VERSION
  "${CMake_VERSION_MAJOR}.${CMake_VERSION_MINOR}.${CMake_VERSION_PATCH}")
if(DEFINED CMake_VERSION_RC)
  set(CMake_VERSION "${CMake_VERSION}-rc${CMake_VERSION_RC}")
endif()

# Releases define a small patch level.
if("${CMake_VERSION_PATCH}" VERSION_LESS 20000000)
  set(CMake_VERSION_IS_RELEASE 1)
else()
  set(CMake_VERSION_IS_RELEASE 0)
endif()

if(EXISTS ${CMake_SOURCE_DIR}/.git)
  find_package(Git QUIET)
  if(GIT_FOUND)
    macro(_git)
      execute_process(
        COMMAND ${GIT_EXECUTABLE} ${ARGN}
        WORKING_DIRECTORY ${CMake_SOURCE_DIR}
        RESULT_VARIABLE _git_res
        OUTPUT_VARIABLE _git_out OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_VARIABLE _git_err ERROR_STRIP_TRAILING_WHITESPACE
        )
    endmacro()
  endif()
endif()

# Try to identify the current development source version.
if(COMMAND _git)
  # Get the commit checked out in this work tree.
  _git(log -n 1 HEAD "--pretty=format:%h %s" --)
  set(git_info "${_git_out}")
else()
  # Get the commit exported by 'git archive'.
  set(git_info [==[$Format:%h %s$]==])
endif()

# Extract commit information if available.
if(git_info MATCHES "^([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]?[0-9a-f]?)[0-9a-f]* (.*)$")
  # Have commit information.
  set(git_hash "${CMAKE_MATCH_1}")
  set(git_subject "${CMAKE_MATCH_2}")

  # If this is not the exact commit of a release, add dev info.
  if(NOT "${git_subject}" MATCHES "^[Cc][Mm]ake ${CMake_VERSION}$")
    set(CMake_VERSION "${CMake_VERSION}-g${git_hash}")
  endif()

  # If this is a work tree, check whether it is dirty.
  if(COMMAND _git)
    _git(update-index -q --refresh)
    _git(diff-index --name-only HEAD --)
    if(_git_out)
      set(CMake_VERSION_IS_DIRTY 1)
    endif()
  endif()
else()
  # No commit information.
  if(NOT CMake_VERSION_IS_RELEASE)
    # Generic development version.
    set(CMake_VERSION "${CMake_VERSION}-git")
  endif()
endif()

# Extract the version suffix component.
if(CMake_VERSION MATCHES "-(.*)$")
  set(CMake_VERSION_SUFFIX "${CMAKE_MATCH_1}")
else()
  set(CMake_VERSION_SUFFIX "")
endif()
if(CMake_VERSION_IS_DIRTY)
  set(CMake_VERSION ${CMake_VERSION}-dirty)
endif()
