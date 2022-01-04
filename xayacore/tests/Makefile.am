AM_TESTS_ENVIRONMENT = \
  PYTHONPATH=$(top_srcdir):$(PYTHONPATH) \
  top_builddir=$(top_builddir)

TEST_LIBRARY = coretest.py

REGTESTS = \
  attach_detach.py \
  basechain_errors.py \
  block_data.py \
  moves.py \
  pending.py \
  pruning.py \
  verifymessage.py

EXTRA_DIST = $(REGTESTS) $(TEST_LIBRARY)
TESTS = $(REGTESTS)
