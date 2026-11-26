.PHONY: all debug release server test clean env

DIST_DIR    := dist
CLIENT_DIR  := android-client
APK_OUT_DIR := $(CLIENT_DIR)/app/build/outputs/apk

# The APK builds the same way on Windows, macOS and Linux. When ANDROID_HOME is unset or
# points at a missing folder, the Android Studio SDK is used. When JAVA_HOME is unset, an
# installed JDK 21 or 17 is used, since Gradle 8.14.3 only runs on JDK 17-24.
ifeq ($(OS),Windows_NT)
GRADLEW := gradlew.bat

ifeq ($(shell if exist "$(ANDROID_HOME)\platform-tools" echo ok),)
DETECTED_SDK := $(shell if exist "$(LOCALAPPDATA)\Android\Sdk\platform-tools" echo $(LOCALAPPDATA)\Android\Sdk)
endif

ifeq ($(JAVA_HOME),)
DETECTED_JDK := $(shell if exist "C:\Program Files\Java\jdk-21\bin\java.exe" (echo C:\Program Files\Java\jdk-21) else if exist "C:\Program Files\Java\jdk-17\bin\java.exe" (echo C:\Program Files\Java\jdk-17))
endif
else
GRADLEW := ./gradlew
UNAME_S ?= $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
SDK_CANDIDATES := $(HOME)/Library/Android/sdk
JDK_SEARCH      = $(shell /usr/libexec/java_home -v 21 2>/dev/null || /usr/libexec/java_home -v 17 2>/dev/null)
else
# Linux: Android Studio's default SDK folder, and distribution JDK folders
# (Debian/Ubuntu java-21-openjdk-amd64, Fedora java-21-openjdk, Arch java-21-openjdk).
SDK_CANDIDATES := $(HOME)/Android/Sdk $(HOME)/Android/sdk
JDK_SEARCH      = $(firstword $(wildcard /usr/lib/jvm/java-21-openjdk* /usr/lib/jvm/jdk-21* /usr/lib/jvm/temurin-21* \
                                         /usr/lib/jvm/java-17-openjdk* /usr/lib/jvm/jdk-17* /usr/lib/jvm/temurin-17*))
endif

ifeq ($(wildcard $(ANDROID_HOME)/platform-tools),)
DETECTED_SDK := $(patsubst %/platform-tools,%,$(firstword $(foreach d,$(SDK_CANDIDATES),$(wildcard $(d)/platform-tools))))
endif

ifeq ($(JAVA_HOME),)
DETECTED_JDK := $(JDK_SEARCH)
endif
endif

ifneq ($(DETECTED_SDK),)
export ANDROID_HOME := $(DETECTED_SDK)
endif
ifneq ($(DETECTED_JDK),)
export JAVA_HOME := $(DETECTED_JDK)
endif

all: debug

# Shows which Android SDK and JDK the APK build will use.
env:
	@echo ANDROID_HOME=$(ANDROID_HOME)
	@echo JAVA_HOME=$(JAVA_HOME)

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
