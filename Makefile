# Dev workflow for building and deploying akira.nro to Nintendo Switch
#
# Usage:
#   make build                          Build the NRO
#   make deploy SWITCH_IP=192.168.x.x   Build and deploy to Switch
#   make rebuild                         Force full rebuild (clean libs + rebuild docker image)
#   make shell                           Open shell in build container
#   make clean-libs                      Clean library build artifacts
#   make build MUTE_CHIAKI=true          Build with chiaki library logs muted

.PHONY: help build deploy rebuild shell clean-libs docker-image submodules

DOCKER_IMAGE := akira-builder
NRO_FILE     := $(CURDIR)/build/akira.nro
MUTE_CHIAKI  ?= false
SWITCH_IP    ?=

# Colors
GREEN  := \033[0;32m
YELLOW := \033[1;33m
RED    := \033[0;31m
NC     := \033[0m

help:
	@echo "Usage: make <target> [SWITCH_IP=<ip>] [MUTE_CHIAKI=true]"
	@echo ""
	@echo "Targets:"
	@echo "  build        Build the NRO"
	@echo "  deploy       Build and deploy to Switch (requires SWITCH_IP)"
	@echo "  rebuild      Force full rebuild (clean libs + rebuild docker image)"
	@echo "  shell        Open shell in build container"
	@echo "  clean-libs   Clean library build artifacts"
	@echo "  help         Show this help"
	@echo ""
	@echo "Environment:"
	@echo "  SWITCH_IP      IP address of Nintendo Switch"
	@echo "  MUTE_CHIAKI    Set to 'true' to mute chiaki library logs"
	@echo ""
	@echo "On your Switch:"
	@echo "  1. Open Homebrew Menu"
	@echo "  2. Press Y for NetLoader mode"
	@echo "  3. Note the IP address shown"

submodules:
	@if [ ! -f "$(CURDIR)/library/borealis/README.md" ]; then \
		printf "$(GREEN)[*]$(NC) Initializing submodules...\n"; \
		git -C "$(CURDIR)" submodule update --init --recursive; \
	fi

docker-image: submodules
	@IMAGE_EXISTS=$$(docker image inspect $(DOCKER_IMAGE) > /dev/null 2>&1 && echo yes || echo no); \
	if [ "$$IMAGE_EXISTS" = "no" ]; then \
		printf "$(GREEN)[*]$(NC) Building Docker image...\n"; \
		docker build -t $(DOCKER_IMAGE) "$(CURDIR)"; \
	fi

build: docker-image
	$(eval ORIGINAL_REVISION := $(shell grep 'set(VERSION_REVISION' "$(CURDIR)/CMakeLists.txt" | sed 's/.*"\(.*\)".*/\1/'))
	$(eval DEV_TIMESTAMP := $(shell date +%d%m%y-%H%M%S))
	$(eval DEV_REVISION := $(ORIGINAL_REVISION)-dev-$(DEV_TIMESTAMP))
	@printf "$(GREEN)[*]$(NC) Setting dev version: $(DEV_REVISION)\n"
	@sed -i '' 's/set(VERSION_REVISION "$(ORIGINAL_REVISION)")/set(VERSION_REVISION "$(DEV_REVISION)")/' "$(CURDIR)/CMakeLists.txt"
	@printf "$(GREEN)[*]$(NC) Building...\n"
	@docker run --rm \
		-v "$(CURDIR):/build" \
		-w /build \
		-e "MUTE_CHIAKI=$(MUTE_CHIAKI)" \
		$(DOCKER_IMAGE) \
		bash -c " \
			set -e; \
			git config --global --add safe.directory /build; \
			git config --global --add safe.directory /build/library/borealis; \
			git config --global --add safe.directory /build/library/chiaki-ng; \
			git config --global --add safe.directory /build/library/curl-libnx; \
			chmod +x /build/scripts/build-docker.sh; \
			/build/scripts/build-docker.sh \
		"; \
	EXIT_CODE=$$?; \
	sed -i '' 's/set(VERSION_REVISION "$(DEV_REVISION)")/set(VERSION_REVISION "$(ORIGINAL_REVISION)")/' "$(CURDIR)/CMakeLists.txt"; \
	if [ $$EXIT_CODE -ne 0 ]; then \
		printf "$(RED)[x]$(NC) Build failed\n"; \
		exit 1; \
	fi
	@if [ ! -f "$(NRO_FILE)" ]; then \
		printf "$(RED)[x]$(NC) Build failed - NRO not found at $(NRO_FILE)\n"; \
		exit 1; \
	fi
	@printf "$(GREEN)[*]$(NC) Build successful: $(NRO_FILE)\n"

deploy: build
	@if [ -z "$(SWITCH_IP)" ]; then \
		printf "$(YELLOW)[!]$(NC) No SWITCH_IP provided - skipping deployment\n"; \
		echo ""; \
		echo "To deploy, run: make deploy SWITCH_IP=<ip>"; \
		exit 0; \
	fi
	@pkill -f nxlink 2>/dev/null || true
	@mkdir -p "$(CURDIR)/logs"
	$(eval LOG_FILE := $(CURDIR)/logs/$(shell date +%d%m%y%H%M%S).log)
	@printf "$(GREEN)[*]$(NC) Deploying to Switch at $(SWITCH_IP)...\n"
	@printf "$(GREEN)[*]$(NC) Logging to: $(LOG_FILE)\n"
	@printf "$(GREEN)[*]$(NC) Press Ctrl+C to stop receiving logs\n"
	@docker run --rm -it --init \
		--network host \
		-v "$(CURDIR):/build" \
		-w /build \
		$(DOCKER_IMAGE) \
		nxlink -s -a "$(SWITCH_IP)" /build/build/akira.nro 2>&1 | tee "$(LOG_FILE)"
	@printf "$(GREEN)[*]$(NC) Done\n"

rebuild: clean-libs
	@printf "$(GREEN)[*]$(NC) Rebuilding Docker image...\n"
	@docker build -t $(DOCKER_IMAGE) "$(CURDIR)"
	@$(MAKE) build

shell: docker-image
	@printf "$(GREEN)[*]$(NC) Opening shell in build container...\n"
	@docker run --rm -it \
		-v "$(CURDIR):/build" \
		-w /build \
		$(DOCKER_IMAGE) \
		bash

clean-libs:
	@printf "$(GREEN)[*]$(NC) Cleaning library build artifacts...\n"
	@git -C "$(CURDIR)/library/chiaki-ng" clean -fdx 2>/dev/null || true
	@git -C "$(CURDIR)/library/curl-libnx" clean -fdx 2>/dev/null || true
	@printf "$(GREEN)[*]$(NC) Library artifacts cleaned\n"
