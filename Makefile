.PHONY: all debug release server test clean

DIST_DIR    := dist
CLIENT_DIR  := android-client
APK_OUT_DIR := $(CLIENT_DIR)/app/build/outputs/apk

ifeq ($(OS),Windows_NT)
GRADLEW := gradlew.bat

ifeq ($(ANDROID_HOME),)
DETECTED_SDK := $(shell if exist "$(LOCALAPPDATA)\Android\Sdk\platform-tools" echo $(LOCALAPPDATA)\Android\Sdk)
ifneq ($(DETECTED_SDK),)
export ANDROID_HOME := $(DETECTED_SDK)
endif
endif

# Gradle 8.14.3 supports JDK 17-24; fall back to JDK 21 or 17 when JAVA_HOME is unset.
ifeq ($(JAVA_HOME),)
DETECTED_JDK := $(shell if exist "C:\Program Files\Java\jdk-21\bin\java.exe" (echo C:\Program Files\Java\jdk-21) else if exist "C:\Program Files\Java\jdk-17\bin\java.exe" (echo C:\Program Files\Java\jdk-17))
ifneq ($(DETECTED_JDK),)
export JAVA_HOME := $(DETECTED_JDK)
endif
endif
else
GRADLEW := ./gradlew
endif

all: debug

debug:
	cmake -E chdir $(CLIENT_DIR) $(GRADLEW) assembleDebug --no-daemon
	cmake -E make_directory $(DIST_DIR)
	cmake -E copy_if_different $(APK_OUT_DIR)/debug/app-debug.apk $(DIST_DIR)/stylus-cast-debug.apk

release:
	cmake -E chdir $(CLIENT_DIR) $(GRADLEW) assembleRelease --no-daemon
	cmake -E make_directory $(DIST_DIR)
ifeq ($(OS),Windows_NT)
	@if exist "$(subst /,\,$(APK_OUT_DIR)/release/app-release.apk)" (cmake -E copy_if_different $(APK_OUT_DIR)/release/app-release.apk $(DIST_DIR)/stylus-cast-release.apk) else (cmake -E copy_if_different $(APK_OUT_DIR)/release/app-release-unsigned.apk $(DIST_DIR)/stylus-cast-release-unsigned.apk)
else
	@if [ -f "$(APK_OUT_DIR)/release/app-release.apk" ]; then cmake -E copy_if_different $(APK_OUT_DIR)/release/app-release.apk $(DIST_DIR)/stylus-cast-release.apk; else cmake -E copy_if_different $(APK_OUT_DIR)/release/app-release-unsigned.apk $(DIST_DIR)/stylus-cast-release-unsigned.apk; fi
endif

server:
	$(MAKE) -C server

test:
	$(MAKE) -C server test

clean:
	cmake -E rm -rf $(DIST_DIR)
	cmake -E chdir $(CLIENT_DIR) $(GRADLEW) clean --no-daemon
	$(MAKE) -C server clean
