THEOS_DEVICE_IP = iphone
ARCHS = arm64
TARGET := iphone:clang:latest:15.0

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = Shirayuki

Shirayuki_FILES = Tweak/Tweak.xm \
	ShirayukiMemory/ShirayukiMemory.cpp \
	ShirayukiMemory/Freeze.cpp \
	ShirayukiMemory/PointerScan.cpp \
	GUI/ShirayukiWindow.m \
	GUI/ShirayukiViewController.m \
	GUI/SYTheme.m \
	GUI/SYResultCell.m
Shirayuki_CFLAGS = -fobjc-arc -std=c++17 -I$(THEOS_PROJECT_DIR)/ShirayukiMemory -I$(THEOS_PROJECT_DIR)/GUI
Shirayuki_CCFLAGS = -std=c++17
Shirayuki_FRAMEWORKS = Foundation UIKit CoreGraphics QuartzCore
Shirayuki_PRIVATE_FRAMEWORKS =
Shirayuki_LDFLAGS = -lc++

include $(THEOS_MAKE_PATH)/tweak.mk

after-install::
	install.exec "killall -9 SpringBoard"
