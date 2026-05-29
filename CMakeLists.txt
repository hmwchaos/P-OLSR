add_library(
  scratch-polsr-lib
  polsr-module/helper/polsr-helper.cc
  polsr-module/model/polsr-header.cc
  polsr-module/model/polsr-routing-protocol.cc
  polsr-module/model/polsr-state.cc
)

target_include_directories(
  scratch-polsr-lib
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/polsr-module/helper
    ${CMAKE_CURRENT_SOURCE_DIR}/polsr-module/model
)

target_link_libraries(
  scratch-polsr-lib
  PUBLIC
    ${libinternet}
    ${libmobility}
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/polsr_paper_reproduction.cc)
  build_exec(
    EXECNAME polsr_paper_reproduction
    SOURCE_FILES polsr_paper_reproduction.cc
    LIBRARIES_TO_LINK
      ${libcore}
      ${libnetwork}
      ${libmobility}
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/P-OLSR
  )
endif()

build_exec(
  EXECNAME polsr_protocol_experiment
  SOURCE_FILES polsr_protocol_experiment.cc
  LIBRARIES_TO_LINK
    scratch-polsr-lib
    ${libolsr}
    ${libinternet}
    ${libnetwork}
    ${libapplications}
    ${libmobility}
    ${libwifi}
    ${libflow-monitor}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/P-OLSR
)

build_exec(
  EXECNAME polsr_header_smoke_test
  SOURCE_FILES polsr_header_smoke_test.cc
  LIBRARIES_TO_LINK
    scratch-polsr-lib
    ${libcore}
    ${libnetwork}
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/P-OLSR
)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/olsr_paper_baseline_experiments.cc)
  build_exec(
    EXECNAME olsr_paper_baseline_experiments
    SOURCE_FILES olsr_paper_baseline_experiments.cc
    LIBRARIES_TO_LINK
      ${libcore}
      ${libolsr}
      ${libinternet}
      ${libnetwork}
      ${libapplications}
      ${libmobility}
      ${libwifi}
      ${libflow-monitor}
    EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/P-OLSR
  )
endif()
