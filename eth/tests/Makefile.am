AM_TESTS_ENVIRONMENT = \
  PYTHONPATH=$(top_srcdir):$(PYTHONPATH) \
  top_builddir=$(top_builddir)

TEST_LIBRARY = ethtest.py

REGTESTS = \
  attach_detach.py \
  block_data.py \
  moves_data.py \
  moves_multi.py \
  pending.py \
  pruning.py \
  verifymessage.py

EXTRA_DIST = $(REGTESTS) $(TEST_LIBRARY)
TESTS = $(REGTESTS)
