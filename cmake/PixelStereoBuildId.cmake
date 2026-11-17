# Build identity shared by the Pixel Stereo applications.
#
#   pxs_build_suffix(<source dir> <out variable>)
#
# The application is named after the release it comes from:
#   0.2.1      the release itself (a tagged commit, nothing modified)
#   0.2.1+3    built 3 commits after the 0.2.1 release
#   0.2.1+3+   the same, with changes that are not committed (a trailing "+")
# The suffix is empty for the release; callers do not add it at all on a release build (a tag built by CI).
# A source archive without Git metadata gets no suffix. The suffix is computed when CMake configures: build.sh and
# the CI configure on every run, an IDE that does not reconfigure shows the state of its last configuration.
function(pxs_build_suffix root out)
  set(suffix "")
  execute_process(COMMAND git describe --tags --long --match "v[0-9]*" --match "[0-9]*" HEAD
    WORKING_DIRECTORY "${root}" OUTPUT_VARIABLE described OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE described_status ERROR_QUIET)
  if(described_status EQUAL 0 AND described MATCHES "-([0-9]+)-g[0-9a-f]+$")
    set(count "${CMAKE_MATCH_1}")
  else()
    # No release tag yet: count every commit; no Git at all: no suffix
    execute_process(COMMAND git rev-list --count HEAD WORKING_DIRECTORY "${root}"
      OUTPUT_VARIABLE count OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE counted ERROR_QUIET)
    if(NOT counted EQUAL 0)
      set(${out} "" PARENT_SCOPE)
      return()
    endif()
  endif()
  execute_process(COMMAND git status --porcelain --untracked-files=normal
    WORKING_DIRECTORY "${root}" OUTPUT_VARIABLE changes OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(NOT count EQUAL 0 OR NOT changes STREQUAL "")
    set(suffix "+${count}")
    if(NOT changes STREQUAL "")
      string(APPEND suffix "+")
    endif()
  endif()
  set(${out} "${suffix}" PARENT_SCOPE)
endfunction()

# The release number the commits are counted from ("0.2.1" for the tag v0.2.1), or "" when there is no tag.
# An application that keeps its number in a VERSION file uses it, so that "+3" never follows a stale number.
function(pxs_nearest_release root out)
  execute_process(COMMAND git describe --tags --abbrev=0 --match "v[0-9]*" --match "[0-9]*" HEAD
    WORKING_DIRECTORY "${root}" OUTPUT_VARIABLE tag OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE status ERROR_QUIET)
  if(NOT status EQUAL 0)
    set(tag "")
  endif()
  string(REGEX REPLACE "^v" "" tag "${tag}")
  set(${out} "${tag}" PARENT_SCOPE)
endfunction()
