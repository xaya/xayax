WCHI_CONTRACTS = WCHI.json
ACCOUNTS_CONTRACTS = \
  NftMetadata.json \
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

forgeout = $(top_builddir)/eth/solidity/out
$(CONTRACTS): %.json: $(forgeout)/%.sol
	cp $</*.json $@
