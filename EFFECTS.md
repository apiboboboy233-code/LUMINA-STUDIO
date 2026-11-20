# Effects Library: bench checks

Follow [WIRING.md](WIRING.md) and [FLASHING.md](FLASHING.md). Start with a short strip, a conservative brightness cap, and an adequately rated supply. Animated or flashing effects may be uncomfortable for photosensitive viewers; keep the strip out of direct view while commissioning.

1. Join the controller AP and open the Effects Library. Check the strip count and color order using the existing diagnostics before judging colors.
2. Turn output on, select and apply each effect below, then change its parameters while it runs. Wait for the API acknowledgement; the browser preview is a simulation, not strip feedback.

| Effect | Expected observation |
| --- | --- |
| Solid | All pixels match the selected color; no periodic motion. |
| Gradient | Color changes across the strip; edit stops and reverse in Color Studio. |
| Rainbow | Hue changes continuously; adjust speed and scale. |
| Rainbow Cycle | Hue bands move along the strip; reverse the direction. |
| Color Wipe | A fill front advances one pixel at a time; try alternating direction. |
| Running Lights | A smooth periodic intensity wave travels along the strip. |
| Breathing | The whole strip pulses together between the selected levels. |
| Chase | Moving blocks have visible width, gap, and trail. |
| Theater Chase | Spaced lamps step between positions. |
| Scanner / Sinelon | A scanning head reverses at the ends with a fading trail. |
| Twinkle | Independent pixels gradually fade after lighting. |
| Sparkle | Short, isolated flashes respond to frequency and intensity. |
| Meteor | A traveling head leaves a decaying wake. |
| Comet | A moving head has a continuous, soft tail. |
| Ocean Wave | Layered flowing waves shift between the selected colors. |
| Fire | Heat propagates along the strip and cools; adjust cooling and sparking. |

Also switch rapidly between effects, reset, and stop. Power off should black out the strip without losing HTTP access. Check counts of 1, your actual strip length, and (with suitable supply) 150; verify removed pixels go dark. Reboot after settings have settled and verify persistence with output still off. Disconnect and reconnect the phone, and refresh while Fire runs. Check API latency and minimum free heap over a sustained run at the maximum count and configured refresh rate; record the board, supply, strip revision, effect and firmware build. Use the standalone HTML separately to test Demo mode, favorites, search and small-screen layout.
