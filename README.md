# InstaGib

A [SvenMod](https://github.com/sw1ft747/svenmod) plugin

## What

Automatically gib when getting damaged

## Why

Fun

## How

### Install

1. Download `instagib.dll` from [Releases](https://github.com/Noxturnix/sven_instagib/releases)
2. Move the file to `Sven Co-op\svenmod\plugins\instagib.dll`
3. Enter `sm plugins load instagib`
4. (Optional) Edit `Sven Co-op\svenmod\plugins.txt` to make SvenMod load InstaGib automatically when starting the game

```
"Plugins"
{
    ...
    "instagib" "1"
}
```

### Stop the plugin without restarting the game

1. In console, enter `sm plugins list`
2. Find `"InstaGib"` and note the plugin index
3. Enter `sm plugins unload <plugin index>`

To load the plugin again, just enter `sm plugins load instagib`

# Credit

- Boilerplate code from SvenMod's [sample_plugin](https://github.com/sw1ft747/svenmod/tree/main/sample_plugin)
- Everyone who gives me learning resources. cba, Sw1ft, etc.

# License

[DO WHAT THE F\*CK YOU WANT TO](LICENSE)
