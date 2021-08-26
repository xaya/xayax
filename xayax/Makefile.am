WCHI_CONTRACTS = WCHI.json
ACCOUNTS_CONTRACTS = \
  XayaAccounts.json \
  XayaPolicy.json
CONTRACTS = $(WCHI_CONTRACTS) $(ACCOUNTS_CONTRACTS)
CLEANFILES = $(CONTRACTS)

xayaxdir = $(pythondir)/xayax
xayax_PYTHON = __init__.py \
  core.py \
  eth.py \
  testcase.py
xayax_DATA = $(CONTRACTS)

xayamod = $(top_srcdir)/node_modules/@xaya
$(WCHI_CONTRACTS): %: $(xayamod)/wchi/build/contracts/%
	cp $< $@
$(ACCOUNTS_CONTRACTS): %: $(xayamod)/eth-account-registry/build/contracts/%
	cp $< $@
