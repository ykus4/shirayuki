THEOS_DEVICE_IP = iphone
ARCHS = arm64
TARGET := iphone:clang:latest:15.0

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = Shirayuki

Shirayuki_FILES = Tweak/Tweak.xm \
	ShirayukiMemory/ShirayukiMemory.cpp \
	ShirayukiMemory/Freeze.cpp \
	ShirayukiMemory/PointerScan.cpp \
	ShirayukiMemory/Watchpoint.cpp \
	ShirayukiMemory/Session.mm \
	GUI/ShirayukiWindow.m \
	GUI/ShirayukiViewController.m \
	GUI/SYTheme.m \
	GUI/SYResultCell.m \
	GUI/SYDragButton.m \
	GUI/SYToast.m \
	GUI/Handlers/SYSearchHandler.mm \
	GUI/Handlers/SYPatchHandler.mm \
	GUI/Handlers/SYFreezeHandler.mm \
	GUI/Handlers/SYWatchHandler.mm \
	GUI/Handlers/SYPointerHandler.mm \
	GUI/Handlers/SYDumpHandler.mm

Shirayuki_CFLAGS = -fobjc-arc -std=c++17 \
	-I$(THEOS_PROJECT_DIR)/ShirayukiMemory \
	-I$(THEOS_PROJECT_DIR)/GUI \
	-I$(THEOS_PROJECT_DIR)/GUI/Handlers
Shirayuki_CCFLAGS = -std=c++17
Shirayuki_FRAMEWORKS = Foundation UIKit CoreGraphics QuartzCore
Shirayuki_PRIVATE_FRAMEWORKS =
Shirayuki_LDFLAGS = -lc++

include $(THEOS_MAKE_PATH)/tweak.mk

after-install::
	install.exec "killall -9 SpringBoard"
