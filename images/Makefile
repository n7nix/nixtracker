

IMAGES = aprs_pri_big.png aprs_sec_big.png
RESIZE_TYPE=-adaptive-resize

all: $(IMAGES)

aprs_pri_big.png: aprs_pri.png Makefile
	@echo "ICON size is $(APRS_ICON_SIZE)"
	convert $(RESIZE_TYPE) $(APRS_ICON_SIZE) $< $@

aprs_sec_big.png: aprs_sec.png Makefile
	convert $(RESIZE_TYPE) $(APRS_ICON_SIZE) $< $@

clean:
	rm -f $(IMAGES)
