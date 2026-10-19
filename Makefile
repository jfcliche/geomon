CONFIG  ?= geomon.yaml
PORT    ?=
HOST    ?=
DEVICE  := $(strip $(PORT) $(HOST))
ESPHOME := uv run esphome

.DEFAULT_GOAL := help
.PHONY: help setup update config _build compile syncserial upload logs logsserial test docs clean

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "  PORT and HOST are interchangeable (serial path, hostname, or IP)."
	@echo "    make compile PORT=<device>    or    make compile HOST=<device>"
	@echo "    make logs PORT=<device>       or    make logs HOST=<device>"
	@echo "  DEVICE=$(DEVICE)"

setup: secrets.yaml ## Create/update the uv environment with ESPHome
	uv sync

secrets.yaml:
	@cp secrets.example.yaml secrets.yaml
	@key=$$(uv run python -c "import base64,os;print(base64.b64encode(os.urandom(32)).decode())") && \
		sed -i "s|^api_encryption_key:.*|api_encryption_key: \"$$key\"|" secrets.yaml
	@echo "Created secrets.yaml with a new API encryption key. Edit ota_password before flashing."

update: ## Upgrade ESPHome to the latest release
	uv lock --upgrade
	uv sync

config: secrets.yaml ## Validate the configuration
	$(ESPHOME) config $(CONFIG)

_build: secrets.yaml
	$(ESPHOME) compile $(CONFIG)

compile: _build ## Build the firmware. PORT= or HOST= also flashes that device
ifneq ($(DEVICE),)
	$(ESPHOME) upload $(CONFIG) $(foreach d,$(DEVICE),--device "$(d)")
endif

syncserial: _build ## Build and flash over PORT or HOST
	$(ESPHOME) upload $(CONFIG) $(foreach d,$(DEVICE),--device "$(d)")

upload: _build ## Build and flash over PORT or HOST
	$(ESPHOME) upload $(CONFIG) $(foreach d,$(DEVICE),--device "$(d)")

logs: ## Stream logs over PORT or HOST
	$(ESPHOME) logs $(CONFIG) $(foreach d,$(DEVICE),--device "$(d)")

logsserial: ## Stream logs over PORT or HOST
	$(ESPHOME) logs $(CONFIG) $(foreach d,$(DEVICE),--device "$(d)")

test: ## Compile tests/register_sensors.yaml (every sniffer code path)
	$(ESPHOME) compile tests/register_sensors.yaml

docs: ## Build the Sphinx HTML documentation into docs/_build/html
	uv run --project docs sphinx-build -b html docs docs/_build/html

clean: ## Remove build artifacts
	$(ESPHOME) clean $(CONFIG)
	$(ESPHOME) clean tests/register_sensors.yaml
	rm -rf docs/_build
