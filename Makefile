THEOS_DEVICE_IP = iphone
ARCHS = arm64
TARGET := iphone:clang:latest:15.0

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = Shirayuki

Shirayuki_FILES = Tweak/Tweak.xm \
	ShirayukiMemory/Memory.cpp \
	ShirayukiMemory/Image.cpp \
	ShirayukiMemory/Scanner.cpp \
	ShirayukiMemory/Disasm.cpp \
	ShirayukiMemory/Hex.cpp \
	ShirayukiMemory/Patch.cpp \
	ShirayukiMemory/ValueFormat.cpp \
	ShirayukiMemory/Freeze.cpp \
	ShirayukiMemory/PointerScan.cpp \
	ShirayukiMemory/Speedhack.cpp \
	ShirayukiMemory/ThreadList.cpp \
	ShirayukiMemory/Watchpoint.cpp \
	ShirayukiMemory/Session.mm \
	GUI/ShirayukiWindow.m \
	GUI/ShirayukiViewController.mm \
	GUI/SYTheme.m \
	GUI/SYResultCell.m \
	GUI/SYDragButton.m \
	GUI/SYToast.m \
	GUI/SYHotkey.m \
	GUI/Handlers/SYScanHelper.cpp \
	GUI/Handlers/SYSearchHandler.mm \
	GUI/Handlers/SYPatchHandler.mm \
	GUI/Handlers/SYFreezeHandler.mm \
	GUI/Handlers/SYWatchHandler.mm \
	GUI/Handlers/SYPointerHandler.mm \
	GUI/Handlers/SYDumpHandler.mm \
	GUI/Handlers/SYThreadHandler.mm \
	GUI/Handlers/SYModuleHandler.mm

Shirayuki_CFLAGS = -fobjc-arc \
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

FMT_FILES = $(shell find ShirayukiMemory GUI Tweak \
	-name '*.cpp' -o -name '*.hpp' -o -name '*.mm' \
	-o -name '*.m' -o -name '*.h')

fmt:
	clang-format -i $(FMT_FILES)

fmt-check:
	clang-format --dry-run --Werror $(FMT_FILES)
