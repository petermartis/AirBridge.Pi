.PHONY: all check test check-media clean

all:
	$(MAKE) -C openwrt

check test:
	$(MAKE) -C openwrt test
	sh -n openwrt/build-lcd.sh
	python3 tools/check_repo.py

clean:
	$(MAKE) -C openwrt clean

check-media:
	$(MAKE) -C media/controller check
	python3 media/engine/test_benchmark.py
	python3 media/kernel/tests.py
	python3 media/kernel/hdmi-codec/tests.py
	node --check openwrt/packages/luci-app-airbridge-media/htdocs/luci-static/resources/view/airbridge/media.js
	node openwrt/packages/luci-app-airbridge-media/tests/test-ui.js
	node openwrt/packages/luci-app-airbridge-media/tests/test-files.js
	sh -n openwrt/packages/airbridge-media/files/airbridge-media.init
	sh -n media/engine/activate-on-device.sh
