CONFIG  ?= geomon.yaml
PORT    ?= /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_A4:CB:8F:F8:9A:C8-if00
HOST    ?= geomon.local
ESPHOME := uv run esphome

.DEFAULT_GOAL := help
.PHONY: help setup update config compile syncserial synceth logs logsserial test clean

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | \
		awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}'
	@echo ""
	@echo "  Variables: PORT=$(PORT)"
	@echo "             HOST=$(HOST)"

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

compile: secrets.yaml ## Build the firmware
	$(ESPHOME) compile $(CONFIG)

syncserial: compile ## Build and flash over USB serial (PORT=...)
	$(ESPHOME) upload $(CONFIG) --device "$(PORT)"

upload: compile ## Build and flash over Ethernet OTA (HOST=name-or-ip)
	$(ESPHOME) upload $(CONFIG) --device "$(HOST)"

logs: ## Stream logs over the network (HOST=...)
	$(ESPHOME) logs $(CONFIG) --device "$(HOST)"

logsserial: ## Stream logs over USB serial (PORT=...)
	$(ESPHOME) logs $(CONFIG) --device "$(PORT)"

test: ## Compile tests/register_sensors.yaml (every sniffer code path)
	$(ESPHOME) compile tests/register_sensors.yaml

clean: ## Remove build artifacts
	$(ESPHOME) clean $(CONFIG)
	$(ESPHOME) clean tests/register_sensors.yaml
