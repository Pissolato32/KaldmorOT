# FindCrypto++.cmake
# Finds Crypto++ library
#
# This module defines:
#   Crypto++_FOUND - True if Crypto++ was found
#   Crypto++_INCLUDE_DIR - The Crypto++ include directory
#   Crypto++_LIBRARIES - The Crypto++ libraries

find_path(CRYPTOPP_INCLUDE_DIR NAMES cryptopp/cryptlib.h cryptlib.h PATH_SUFFIXES cryptopp crypto++)
find_library(CRYPTOPP_LIBRARY NAMES cryptopp crypto++)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Crypto++ DEFAULT_MSG CRYPTOPP_LIBRARY CRYPTOPP_INCLUDE_DIR)

if(CRYPTOPP_FOUND)
  set(Crypto++_INCLUDE_DIR ${CRYPTOPP_INCLUDE_DIR})
  set(Crypto++_LIBRARIES ${CRYPTOPP_LIBRARY})
endif()
