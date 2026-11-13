CC      = x86_64-w64-mingw32-gcc
WINDRES = x86_64-w64-mingw32-windres
VERSION ?= dev
ICO     = logos/ico
TRAY_SIZES = 16 20 24 32
# No 256 px entry: ImageMagick stores it uncompressed, which alone adds 260 kB to the exe.
APP_SIZES  = 16 24 32 48 64

nightcap.exe: main.c nightcap.rc $(wildcard $(ICO)/*.ico)
	$(WINDRES) nightcap.rc -o nightcap.res.o
	$(CC) -Os -s -mwindows -Wall -Wextra -DVERSION='L"$(VERSION)"' main.c nightcap.res.o -o $@ -lwtsapi32 -lwinhttp

installer: nightcap.exe
	makensis -V2 -DVERSION=$(VERSION) installer.nsi

# Checks the version comparison and does one real update check against GitHub. Runs the exe, so needs Windows or WSL.
test:
	$(CC) -DSELFTEST -DVERSION='L"0.0.1"' -Wall -Wextra main.c -o selftest.exe -lwtsapi32 -lwinhttp
	./selftest.exe

# Regenerate the committed .ico files from the artwork. Needs ImageMagick and rsvg-convert.
icons:
	for s in $(APP_SIZES); do rsvg-convert -w $$s logos/logo.svg -o $(ICO)/app-$$s.png; done
	magick $(APP_SIZES:%=$(ICO)/app-%.png) $(ICO)/app.ico
	rm $(APP_SIZES:%=$(ICO)/app-%.png)
	magick $(TRAY_SIZES:%=logos/png/tray-on-dark-%.png) $(ICO)/tray-dark.ico
	magick $(TRAY_SIZES:%=logos/png/tray-on-light-%.png) $(ICO)/tray-light.ico

.PHONY: installer test icons
