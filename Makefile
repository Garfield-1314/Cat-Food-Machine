# Containerized ESP-IDF build for the Cat Food Machine.
#
# The same espressif/idf image is used by GitHub Actions, so local and CI builds
# run in an identical toolchain. Override DOCKER=podman to use Podman instead.

SHELL := /bin/bash

IMAGE ?= espressif/idf:v5.5.5
DOCKER ?= docker
HOST_UID := $(shell id -u)
HOST_GID := $(shell getent passwd $$(id -u) | cut -d: -f4)
DOCKER_USER ?= $(HOST_UID):$(HOST_GID)
DOCKER_USER_FLAG := $(if $(DOCKER_USER),--user $(DOCKER_USER),)
SERIAL_PORT ?= /dev/ttyACM0
SERIAL_GROUP ?= $(shell getent group dialout 2>/dev/null | cut -d: -f3)
MOUNT_OPTS ?=
CCACHE_DIR := $(CURDIR)/.cache/ccache

DEVICE_FLAGS := --device $(SERIAL_PORT)
ifneq ($(SERIAL_GROUP),)
DEVICE_FLAGS += --group-add $(SERIAL_GROUP)
endif

DOCKER_BASE := $(DOCKER) run --rm \
	$(DOCKER_USER_FLAG) \
	-e HOME=/tmp \
	-e IDF_CCACHE_ENABLE=1 \
	-e IDF_GIT_SAFE_DIR=/opt/esp/idf:/opt/esp/idf/components/openthread/openthread \
	-e CCACHE_DIR=/project/.cache/ccache \
	-v "$(CURDIR):/project$(MOUNT_OPTS)" \
	-w /project/src

RUN := $(DOCKER_BASE) $(IMAGE)
RUN_TTY := $(DOCKER_BASE) -it $(IMAGE)

.DEFAULT_GOAL := help
.PHONY: help pull build package size menuconfig shell clean flash monitor flash-monitor

help:
	@echo "Cat Food Machine - containerized build"
	@echo ""
	@echo "  make pull           Pull $(IMAGE)"
	@echo "  make build          Build the firmware"
	@echo "  make package        Build + stage dist/ (bins, merged image, SHA256SUMS.txt)"
	@echo "  make size           Show the firmware size report"
	@echo "  make menuconfig     Open menuconfig"
	@echo "  make shell          Open a shell inside the ESP-IDF container"
	@echo "  make clean          Remove build output and dist/"
	@echo "  make flash          Flash over SERIAL_PORT (default: $(SERIAL_PORT))"
	@echo "  make monitor        Open the serial monitor"
	@echo "  make flash-monitor  Flash and open the serial monitor"
	@echo ""
	@echo "Variables: IMAGE=... DOCKER=docker|podman SERIAL_PORT=... MOUNT_OPTS=:z DOCKER_USER=..."

$(CCACHE_DIR):
	@mkdir -p "$(CCACHE_DIR)"

pull:
	$(DOCKER) pull $(IMAGE)

build: $(CCACHE_DIR)
	$(RUN) idf.py build

package: $(CCACHE_DIR)
	$(RUN) bash /project/scripts/build_firmware.sh

size: $(CCACHE_DIR)
	$(RUN) idf.py size

menuconfig: $(CCACHE_DIR)
	$(RUN_TTY) idf.py menuconfig

shell: $(CCACHE_DIR)
	$(RUN_TTY) bash

clean: $(CCACHE_DIR)
	$(RUN) rm -rf /project/src/build /project/dist

flash: $(CCACHE_DIR)
	$(DOCKER_BASE) $(DEVICE_FLAGS) $(IMAGE) idf.py -p $(SERIAL_PORT) flash

monitor: $(CCACHE_DIR)
	$(DOCKER_BASE) -it $(DEVICE_FLAGS) $(IMAGE) idf.py -p $(SERIAL_PORT) monitor

flash-monitor: $(CCACHE_DIR)
	$(DOCKER_BASE) -it $(DEVICE_FLAGS) $(IMAGE) idf.py -p $(SERIAL_PORT) flash monitor
