THEOS_DEVICE_IP = iphone
ARCHS = arm64
TARGET := iphone:clang:latest:15.0

# Rootless is the only layout that works on iOS 15+ jailbreaks (palera1n,
# Dopamine, XinaA15); a rooted .deb installs to paths that do not exist there.
# Override with `make THEOS_PACKAGE_SCHEME=` to produce a legacy rooted package.
THEOS_PACKAGE_SCHEME ?= rootless

# The verification targets at the bottom of this file run without Theos, so that
# tests and the iOS syntax check work on any machine with Xcode. Only the tweak
# build itself needs $THEOS.
ifneq ($(wildcard $(THEOS)/makefiles/common.mk),)
THEOS_AVAILABLE = 1
endif

ifdef THEOS_AVAILABLE

include $(THEOS)/makefiles/common.mk

TWEAK_NAME = Shirayuki

Shirayuki_FILES = Tweak/Tweak.xm \
	ShirayukiMemory/Memory.cpp \
	ShirayukiMemory/Image.cpp \
	ShirayukiMemory/Scanner.cpp \
	ShirayukiMemory/Patch.cpp \
	ShirayukiMemory/Hex.cpp \
	ShirayukiMemory/Disasm.cpp \
	ShirayukiMemory/ValueType.cpp \
	ShirayukiMemory/Freeze.cpp \
	ShirayukiMemory/PointerScan.cpp \
	ShirayukiMemory/Watchpoint.cpp \
	ShirayukiMemory/Json.cpp \
	ShirayukiMemory/Session.cpp \
	GUI/ShirayukiWindow.m \
	GUI/ShirayukiViewController.mm \
	GUI/SYTheme.m \
	GUI/SYResultCell.m \
	GUI/SYDragButton.m \
	GUI/SYToast.m \
	GUI/SYInput.m \
	GUI/Handlers/SYScanHelper.cpp \
	GUI/Handlers/SYBaseHandler.mm \
	GUI/Handlers/SYSearchHandler.mm \
	GUI/Handlers/SYPatchStore.mm \
	GUI/Handlers/SYPatchHandler.mm \
	GUI/Handlers/SYFreezeHandler.mm \
	GUI/Handlers/SYWatchHandler.mm \
	GUI/Handlers/SYPointerHandler.mm \
	GUI/Handlers/SYDumpHandler.mm

Shirayuki_CFLAGS = -fobjc-arc \
	-I$(THEOS_PROJECT_DIR)/ShirayukiMemory \
	-I$(THEOS_PROJECT_DIR)/GUI \
	-I$(THEOS_PROJECT_DIR)/GUI/Handlers
# GNU extensions are required, not optional: plain -std=c++17 removes `typeof`,
# which ARC's __weak/__strong self idiom depends on in ObjC++.
Shirayuki_CCFLAGS = -std=gnu++17
Shirayuki_FRAMEWORKS = Foundation UIKit CoreGraphics QuartzCore
Shirayuki_PRIVATE_FRAMEWORKS =
Shirayuki_LDFLAGS = -lc++

include $(THEOS_MAKE_PATH)/tweak.mk

after-install::
	install.exec "killall -9 SpringBoard"

else

# Without Theos, building the tweak is the only thing that is unavailable.
all package install:
	@echo "\$$THEOS is not set — install Theos to build the tweak."
	@echo "Verification targets work without it: make check (fmt-check, test, syntax-check)"
	@exit 1

endif

FMT_FILES = $(shell find ShirayukiMemory GUI Tweak tests \
	-name '*.cpp' -o -name '*.hpp' -o -name '*.mm' \
	-o -name '*.m' -o -name '*.h')

fmt:
	clang-format -i $(FMT_FILES)

fmt-check:
	clang-format --dry-run --Werror $(FMT_FILES)

# --- Verification without a device -----------------------------------------
#
# The core is pure C++ and the Mach VM APIs it uses exist on macOS, so it builds
# and runs natively here. This is the only automated test coverage the project
# has; `make test` should pass before any commit that touches ShirayukiMemory/.

BUILD_DIR = build/tests

test:
	cmake -S tests -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(BUILD_DIR) -j
	cd $(BUILD_DIR) && ctest --output-on-failure

# Type-check the ObjC/ObjC++ layer against the real iPhoneOS SDK. Catches
# everything a Theos build would except the Logos preprocessing of Tweak.xm.
syntax-check:
	./scripts/check-ios-syntax.sh

# Everything that can be verified on a machine without Theos or a device.
check: fmt-check test syntax-check

.PHONY: fmt fmt-check test syntax-check check
