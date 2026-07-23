set(REGTESTS
  attach_detach.py
  blockcache.py
  block_data.py
  moves_data.py
  moves_multi.py
  pending.py
  pruning.py
  verifymessage.py
)

foreach(TESTFILE ${REGTESTS})
  add_test(
    NAME eth-${TESTFILE}
    COMMAND ${VENV_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/${TESTFILE}
  )
  set_tests_properties(eth-${TESTFILE} PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR};top_builddir=${CMAKE_BINARY_DIR}"
  )
endforeach()
