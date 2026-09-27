
########################################################
# Files

set (WMS_SRCS
  qgswms.cpp
  qgswmsutils.cpp
  qgsdxfwriter.cpp
  qgspdfwriter.cpp
  qgswmsdescribelayer.cpp
  qgswmsgetcapabilities.cpp
  qgswmsgetcontext.cpp
  qgswmsgetfeatureinfo.cpp
  qgswmsgetlegendgraphics.cpp
  qgswmsgetmap.cpp
  qgswmsgetprint.cpp
  qgswmsgetschemaextension.cpp
  qgswmsgetstyles.cpp
  qgsmaprendererjobproxy.cpp
  qgsmediancut.cpp
  qgswmsrenderer.cpp
  qgswmsparameters.cpp
  qgswmsrestorer.cpp
  qgswmsrendercontext.cpp
  qgswmsrequest.cpp
  qgswmslayerinfos.cpp
)

set (WMS_HDRS
  qgswmsparameters.h
  qgswmsserviceexception.h
)

########################################################
# Build

set(_library_suffix_MODULE "")
set(_library_suffix_STATIC "_static")

# optional, only used for faster PNG encoding (QGIS_SERVER_WMS_PNG_COMPRESSION_LEVEL)
option(WITH_LIBDEFLATE "Use libdeflate for faster PNG encoding in the WMS service" ON)
if(WITH_LIBDEFLATE)
  find_package(libdeflate CONFIG QUIET)
  if(TARGET libdeflate::libdeflate_shared)
    set(LIBDEFLATE_TARGET libdeflate::libdeflate_shared)
  elseif(TARGET libdeflate::libdeflate_static)
    set(LIBDEFLATE_TARGET libdeflate::libdeflate_static)
  endif()
  if(LIBDEFLATE_TARGET)
    message(STATUS "Found libdeflate for WMS PNG encoding: ${libdeflate_VERSION}")
  endif()
endif()

foreach(_library_type MODULE STATIC)
  set(_library_name "wms${_library_suffix_${_library_type}}")

  add_library(${_library_name} ${_library_type} ${WMS_SRCS} ${WMS_HDRS})

  target_include_directories(${_library_name} PUBLIC
    ${CMAKE_SOURCE_DIR}/src/server
    ${CMAKE_SOURCE_DIR}/src/server/services
    ${CMAKE_SOURCE_DIR}/src/server/services/wms

    ${CMAKE_BINARY_DIR}/src/python
    ${CMAKE_BINARY_DIR}/src/analysis
    ${CMAKE_BINARY_DIR}/src/server
    ${CMAKE_CURRENT_BINARY_DIR}
    ${ZLIB_INCLUDE_DIRS}
  )

  target_link_libraries(${_library_name}
    qgis_core
    qgis_server
    ${ZLIB_LIBRARIES}
  )
  if(LIBDEFLATE_TARGET)
    target_link_libraries(${_library_name} ${LIBDEFLATE_TARGET})
    target_compile_definitions(${_library_name} PRIVATE HAVE_LIBDEFLATE)
  endif()
endforeach()

# only install module, static library is for testing only
install(TARGETS wms
    RUNTIME DESTINATION ${QGIS_SERVER_MODULE_DIR}
    LIBRARY DESTINATION ${QGIS_SERVER_MODULE_DIR}
)

