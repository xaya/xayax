set(FORGEOUT ${CMAKE_SOURCE_DIR}/eth/solidity/out)

set(CONTRACTS
  WCHI.json
  NftMetadata.json
  XayaAccounts.json
  XayaPolicy.json
)

foreach(JSON_FILE ${CONTRACTS})
  set(NAME "${JSON_FILE}")
  string(REGEX REPLACE "\.json$" "" CONTRACT "${JSON_FILE}")
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}
    COMMAND ${CMAKE_COMMAND} -E copy
      ${FORGEOUT}/${CONTRACT}.sol/${CONTRACT}.json
      ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE}
    DEPENDS solidity-build
    COMMENT "Copying ${JSON_FILE} from Solidity build"
  )
  list(APPEND JSON_OUTPUTS ${CMAKE_CURRENT_BINARY_DIR}/${JSON_FILE})
endforeach()

add_custom_target(copy-contract-jsons ALL DEPENDS ${JSON_OUTPUTS})
