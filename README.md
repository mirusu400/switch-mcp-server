# Switch MCP Server

Switch MCP Server is a Nintendo Switch sysmodule that exposes a small MCP server over HTTP.
It is designed to run under Atmosphere/Hekate and make the console remotely controllable from an external MCP client.

The current goal is to grow this project from a few demo tools into a generic Switch automation and diagnostics runtime.

## What It Does

- Inject virtual controller input.
- Capture the current screen.
- Record real controller input.
- Expose basic runtime status.
- Expose basic SD-card filesystem operations.

The server listens on:

```text
http://<switch-ip>:12345/mcp
```

## Current MCP Coverage

Implemented methods:

- `initialize`
- `tools/list`
- `tools/call`
- `resources/list`
- `resources/templates/list`
- `resources/read`
- `ping`
- `notifications/initialized`

Transport:

- `POST /mcp` for JSON-RPC requests
- `GET /mcp` for SSE

Not implemented:

- Full OAuth flow
- Prompts
- Full MCP surface area

## Built-In Tools

### `controller`

Inject a virtual controller state through HDLS.

Supports:

- Buttons
- Left/right analog sticks
- Six-axis acceleration
- Six-axis angle
- Optional `long_press`

### `cur_frame`

Capture the current Switch screen and return it as a JPEG image.

### `controller_recorder`

Record real physical controller input only.

Supported actions:

- `start`
- `stop`
- `dump`
- `clear`
- `save`

### `system_info`

Return generic runtime information useful for automation and diagnostics.

Current fields include:

- System tick
- Unix time
- UTC time
- Free heap estimate
- Whether SD is mounted

### `fs_ops`

Perform generic SD-card filesystem operations.

Supported actions:

- `list`
- `stat`
- `read_text`
- `write_text`
- `mkdir`
- `delete`

## Built-In Resources

Static resources:

- `switch://screen/current`
- `switch://system/status`

Resource templates:

- `file:///{path}`

`file:///{path}` can be used to read files or directories from the mounted SD card.

## Build

Requirements:

- `devkitPro`
- `libnx`

Set `DEVKITPRO`, then build:

```bash
make
```

The packaged sysmodule output is generated under `out/`.

## Install

Copy:

```text
out/atmosphere/contents/010000000000B1C0
```

to:

```text
/atmosphere/contents/010000000000B1C0
```

On the Switch, the sysmodule can be toggled from:

```text
Hekate Toolbox -> Background Services -> switch-mcp-server
```

Example MCP client config:

```json
{
  "servers": {
    "switch-mcp-server": {
      "type": "streamableHttp",
      "url": "http://<switch-ip>:12345/mcp"
    }
  }
}
```

## Notes and Limitations

- The screen capture path is memory-sensitive. The current sysmodule heap is 2 MB.
- OAuth discovery endpoints exist only as placeholders.
- This is still a partial MCP implementation.
- The current server is intentionally small and direct; it is not yet a full native API mapping layer.

## Project Layout

- `source/` main source tree
- `source/tools/` MCP tools and resource handlers
- `source/transport/` HTTP, SSE, and registry dispatch
- `source/util/` logging and utility code
- `source/third_party/` vendored third-party code
- `out/` packaged build output

## Direction

The long-term direction is to expose broader Switch-native capabilities in a generic way, so this project can support:

- Homebrew automation
- Dynamic testing
- Regression validation
- Diagnostics
- Experimental analysis workflows

The next logical expansion areas are:

- App and title lifecycle control
- Input script playback
- Richer system and app status resources
- Artifact collection
- Network, power, save-data, and account related tool groups

## Credits

- [devkitPro](https://devkitpro.org/)
- [libnx](https://github.com/switchbrew/libnx)
- [stb](https://github.com/nothings/stb)
