AM_TESTS_ENVIRONMENT = \
  PYTHONPATH=$(top_srcdir):$(PYTHONPATH) \
  top_builddir=$(top_builddir)

TEST_LIBRARY = ethtest.py

REGTESTS = \
  block_data.py

EXTRA_DIST = $(REGTESTS) $(TEST_LIBRARY)
TESTS = $(REGTESTS)
