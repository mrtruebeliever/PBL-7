# Changelog

## 1.1.0

- **Explosions and a fuller soundtrack.** Enemies, mines, the smart bomb, a
  hit, a lost ship and a boss now go up in real explosions -- a noise burst
  synthesised by `tools/gen_sfx.py`, pitched high and short for a fighter and
  low and long for a boss -- instead of a bare square-wave tone. New sounds
  where it was silent: a WARNING siren as a boss approaches, a rising whoosh at
  launch, a blast when a ship is lost, and the fanfare when the Moon's turrets
  fall. The boss, game-over and Earth-saved jingles get a bass voice through
  `speaker_play_tracks()`, and game over has its own minor fall instead of the
  one it shared with PebbleDiver. The shot is a quieter two-step "pew": since
  firmware 4.40 drains the speaker pipeline, sounds this short are actually
  heard, so rapid fire must not tire the ear.
- **Follows the watch's speaker mute.** With the speaker muted in the watch
  settings, the game plays silent (as in Quiet Time) and the Sound row in
  Settings reads "Muted".
- **Hit flash on the backlight (Pebble Time 2).** Taking a hit flashes the
  colour backlight red for a moment, a hit the shield soaks up flashes it blue.

## 1.0.1

- **Menu icon and store art got a black outline** — the ship sprite is four
  light colours on transparent, which disappeared against a light launcher
  background. No gameplay changes.

## 1.0.0

- Initial release: **PBL-7**, a landscape shoot-'em-up for the Pebble Time 2.
  Hold the watch sideways (a quarter turn) — the buttons line up along the
  bottom and the screen becomes a 228×200 playfield.
- **Controls:** hold **UP** to rise and release to fall (gravity), **DOWN** to
  fire, **SELECT** for a smart bomb, **BACK** to pause.
- **Weapon heat / jam:** sustained fire overheats and briefly jams the gun, so
  burst-firing beats holding the trigger. Can be turned off (easy mode) in
  Settings.
- **11 planets:** Mercury, Venus, Mars, Jupiter, Saturn, Uranus and Neptune,
  each with a themed boss and a unique gimmick (overheat, acid clouds, a
  drone-shield, gravity well, splitting rings, a freeze, and lightning
  storms), plus a secret Pluto boss-rush, the hidden Rebble world, a Moon-base
  turret level and a three-phase Earth finale.
- **Progression:** one of three Pebble-themed upgrade cards after each boss
  (carried through the run), 8 hidden collectible Pebbles, HP bar + 3 lives
  with checkpoints, and a charge-up smart-bomb special.
- **Two endings:** deliver the Time Core to save Earth, or collect all 8 lost
  Pebbles for the secret true ending. Beat Neptune undamaged to open a secret
  portal to Pluto.
- **Settings:** backlight, weapon jam (easy mode), rumble, sound, and reset
  save.
- Localised in English (default/fallback), Nederlands, Français, Deutsch and
  Español, following the watch's system language.
- Auto-pauses on incoming notifications and locks input for 1 second after
  game over so a reflexive press can't restart a run.
- Plays entirely offline; progress, records and collectibles are saved on the
  watch.
