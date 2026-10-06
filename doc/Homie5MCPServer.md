# Homie 5 MCP Server — LLM access to your eBus device

Once your ESP32 is running this firmware and publishing as a Homie 5 device, you can give an LLM (Claude Code, Claude Desktop, any MCP client) read/write access to that device's topic tree via an [MCP](https://modelcontextprotocol.io/) server: [`Tieske/homie5-mcp-server`](https://github.com/Tieske/homie5-mcp-server).

The MCP server subscribes to the same MQTT broker your device publishes to, caches the device descriptions and property values it sees, and exposes them to the LLM through a small set of tools (`homie_get_devices`, `homie_get_value`, `homie_set_value`, etc.). Useful for testing, debugging, exploratory poking, or having Claude reason about the device's actual state instead of you describing it in prose.

This is **not** a fork or a vendored copy — it's an independent project by Thijs Schreijer (GitHub: [Tieske](https://github.com/Tieske)). The notes here only cover the specific configuration this project needs.

## What you get when it's running

- List the Homie devices that have published to your broker
- Inspect any device's full description (nodes, properties, datatypes, units)
- Read any property's current cached value
- Write to any settable property (with type/range validation against the property's schema)
- Connect / disconnect to a different broker on the fly (interactive mode)

The LLM never touches your firmware directly — it goes through MQTT, exactly like any other Homie client would.

## Setup

```sh
# Clone alongside esp32-sdk (a sibling directory is convenient but not required)
cd <your-workspace>                                     # wherever your clones live
git clone https://github.com/Tieske/homie5-mcp-server.git

cd homie5-mcp-server
npm install
npm run build                                           # produces build/index.js
```

Verify the build by running the server standalone for a moment:

```sh
HOMIE_BROKER_URL='mqtt://user:pass@your-broker:1883' \
HOMIE_DOMAIN=ebus \
node build/index.js
```

You should see log output as it subscribes and starts caching devices. `Ctrl-C` to stop.

## Configuration for this project

**Two settings matter most:**

| Variable | This project's value | Notes |
|---|---|---|
| `HOMIE_BROKER_URL` | `mqtt://<user>:<pass>@<host>:<port>` | Same broker your firmware uses — copy from `config-overlay.yml`, or from `device.yml` |
| `HOMIE_DOMAIN` | **`ebus`** | This project's Homie topic prefix is `ebus`, not the default `homie`. The MCP server defaults to `homie` and won't find anything if you forget this. |

If your `mqtt:` section has `host: broker.example.com`, `username: user`, `password: pass`, `port: 1883`, your broker URL is:

```
mqtt://user:pass@broker.example.com:1883
```

(For brokers with no auth, omit the `user:pass@` chunk.)

See [Configuration.md](Configuration.md) for the canonical place credentials live (`config-overlay.yml`).

## Wiring it into Claude Code

The simplest path — preconfigured broker, no per-session reconnect:

```sh
claude mcp add homie-mcp -- \
    env HOMIE_BROKER_URL='mqtt://user:pass@broker.example.com:1883' \
        HOMIE_DOMAIN=ebus \
        node /path/to/homie5-mcp-server/build/index.js
```

Replace the URL/path with your own. After running this, Claude Code reloads its MCP server list and you'll see `homie-mcp` available. The Homie tools (`mcp__homie-mcp__homie_get_devices` etc.) become callable from any session in any project — the server doesn't have to live inside this repo.

Interactive mode (LLM chooses broker via `homie_connect`):

```sh
claude mcp add homie-mcp -- \
    env HOMIE_DOMAIN=ebus \
        node /path/to/homie5-mcp-server/build/index.js
```

In this mode the LLM will need to call `homie_connect` with a broker URL before any other tool will work.

## Try it

With the firmware running and the MCP server registered, an LLM session like this should work:

> **You:** Which Homie devices are live on the broker?
>
> **LLM (calls `homie_get_devices`):** I see one device: `b0b21c90f570` (eBus device, state: ready).
>
> **You:** Show me everything that device exposes.
>
> **LLM (calls `homie_get_device`):** Returns the full node/property tree with current values and metadata.

Once you're in this loop, you can ask the LLM to set test values, watch state evolve over time, or reason about edge cases against the actual device state.

## Other transport options

The upstream README covers SSE-over-HTTPS and a Docker container — useful if you want the MCP server to run as a network-accessible service (e.g. on a small home server) rather than spawned per-client over stdio. See [`Tieske/homie5-mcp-server`](https://github.com/Tieske/homie5-mcp-server) for the full setup.

## Credits

Thanks to [Thijs Schreijer (Tieske)](https://github.com/Tieske) for building and maintaining this MCP server. Issues and contributions go upstream, not into this repo.
