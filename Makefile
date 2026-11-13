.PHONY: all check test clean

all:
	$(MAKE) -C openwrt

check test:
	$(MAKE) -C openwrt test
	sh -n openwrt/build-lcd.sh
	python3 tools/check_repo.py

clean:
	$(MAKE) -C openwrt clean
