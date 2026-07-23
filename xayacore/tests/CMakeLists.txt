set(REGTESTS
  attach_detach.py
  basechain_errors.py
  block_data.py
  moves.py
  pending.py
  pruning.py
  verifymessage.py
)

foreach(TESTFILE ${REGTESTS})
  add_test(
    NAME xayacore-${TESTFILE}
    COMMAND ${VENV_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/${TESTFILE}
  )
  set_tests_properties(xayacore-${TESTFILE} PROPERTIES
    ENVIRONMENT "PYTHONPATH=${CMAKE_SOURCE_DIR};top_builddir=${CMAKE_BINARY_DIR}"
  )
endforeach()
