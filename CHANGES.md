# Changes from regular Anki 1.6

## Backported features from >1.6
- Add Vector 2.0 support (Firmware 2.0)
- Custom Eye Colors (Firmware 1.8)
- Intent graph backported from firmware 1.8 (Could someone with a ddl server sub test this pls?)
- Fixed occasional bouncy lift ([Anki commit](https://github.com/kercre123/victor/commit/54cfb37))
- Fixed self confirming fist bump ([Anki Commit](https://github.com/kercre123/victor/commit/2d5213e))
- Fixed path planning to stop head bobbing loop ([Anki Commit](https://github.com/kercre123/victor/commit/4110afc))
- Fixed going back to charger loop ([Anki Commit 1](https://github.com/kercre123/victor/commit/ac54369) [Anki Commit 2](https://github.com/kercre123/victor/commit/211c40d))

## Animation related changes
- BinaryEyes when leaving charger
- Good looking Vector 2.0 eyes (Used from WireOS)
- Smoother pre-1.6 eye darts (Last in 1.5, ported to Viccyware and used code from there)
- Added the previously unused second timer end beep animation
- Rainbow Eyes!
- Petting Lights (Code from WireOS but petting colors are mine)

## Behavior related changes
- Unintentional and Intentional performances ([Anki Commit 1](https://github.com/kercre123/victor/commit/d3fa225) [Anki Commit 2](https://github.com/kercre123/victor/commit/2184b33))
- Can now play Blackjack on charger
- Now plays the wakeup after onboarding is finished (Code from Viccyware)

## Cloud changes
- vic-cloud and gateway that works with wirepod and regular servers
- New public server environment (Setup at https://modder.my.to/1.6 and hosted by [@froggitti](https://github.com/froggitti))

## Miscellaneous changes
- Improved Japanese TTS voice
- Upped temprature limit for Vector 2.0
- Support for DVT bodyboards
- Compiling with -O2 and fast math
- Picovoice 1.5 for customizable wakeword (Code used from WireOS)

