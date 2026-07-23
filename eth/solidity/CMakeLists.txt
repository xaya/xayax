find_program(FORGE forge)
if(NOT FORGE)
  message(FATAL_ERROR "Forge (foundry) was not found on PATH.\n"
    "Install Foundry: https://book.getfoundry.sh/getting-started/installation")
endif()

add_custom_target(solidity-build ALL
  COMMAND ${FORGE} build
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Building Solidity contracts with Forge"
)

add_custom_target(solidity-clean
  COMMAND rm -rf cache out
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  COMMENT "Cleaning Solidity build artefacts"
)

add_test(
  NAME solidity
  COMMAND ${FORGE} test
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
