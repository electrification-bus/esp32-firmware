"""Builders for small device.yml documents used across the CLI tests."""


def device_doc(nodes=None, buses=None, **device):
    """A minimal valid device.yml as a dict."""
    doc = {"device": {"name": "test-device", "environment": "esp32-poe-iso", **device}}
    if buses is not None:
        doc["buses"] = buses
    doc["nodes"] = nodes if nodes is not None else []
    return doc


def relay(node_id="relay1", pin=14, **extra):
    """A gpio/digital_output node entry."""
    return {"component": "gpio/digital_output", "id": node_id,
            "config": {"pin": pin, "active_low": False, "initial_state": False, **extra}}
