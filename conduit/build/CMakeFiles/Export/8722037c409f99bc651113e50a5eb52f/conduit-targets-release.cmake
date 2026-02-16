#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "conduit::conduit" for configuration "Release"
set_property(TARGET conduit::conduit APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(conduit::conduit PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libconduit.a"
  )

list(APPEND _cmake_import_check_targets conduit::conduit )
list(APPEND _cmake_import_check_files_for_conduit::conduit "${_IMPORT_PREFIX}/lib/libconduit.a" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
