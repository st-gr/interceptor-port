# F/A-18 Interceptor port: player's manual

This is the manual for the port as it stands after milestone 7: the start screens and the main menu,
free flight in the F/A-18 or the F-16 from any of the four starts, the demonstration, the two parts of
the flight school, carrier qualification, the flight log, combat and the six missions: the cannon,
the Sidewinder and AMRAAM missiles, the radar and the target box, flares, chaff and the ECM, damage and
ejection, with the hostile aircraft, and the sound and music, all with the original's flight model,
views, cockpit instruments, screens and timing. Milestone 7 closed the open rules that a capture or the
reference rig could settle: the camera approach to a start is now seen from outside with the original's camera
path (the mover), the location keys 7 to 0 have their own approach lengths, the pause and SHIFT-ESC sounds follow the
captures, and the flight log persists in the browser. The last section lists what is not in yet.

## Getting started

The port ships no game data. The game's disks, sounds, models, pictures and texts are the publisher's
copyright, so you supply your own copy of the disk and the extractor turns it into the files the port
reads. Everything the port shows of the original (the menu texts, the demonstration's recorded stream,
the pictures) comes from your own disk image; nothing of it is in the repository. The Amiga ROM
(Kickstart) is not needed: the port does not emulate the machine.

1. Decode your disk image to a sector image (ADF). Flux captures (SCP, HFE, MFI) are converted with
   Greaseweazle: `gw convert --format amiga.amigados disk.scp interceptor.adf`.
2. Install the extractor and run it: `pip install -e tools/extract`, then
   `python -m extract interceptor.adf assets`. This writes the `assets/` directory (git-ignored; never
   commit it or the disk image).
   The port reads the extractor's version from `assets/VERSION` and refuses assets made by another version:
   after an update, run the extractor again (the port needs version 25).
3. Build and start: `make` (needs a C99 compiler and SDL2), then `build/interceptor`. The game opens
   on the credits screen and goes through its own screens from there.

`build/interceptor --flight` skips all the screens and puts you in the outside view on the carrier deck;
add `--cockpit` to start in the cockpit and `--runway` to start on the runway. It is meant for
screenshots and tests. `build/interceptor --start-keys` presses the keys of the normal start for you
(any key, the menu's 2, location 1, aircraft 1) and so shows the real path into free flight.

## The screens, in order

**Credits.** The first screen: the credits text types out on a black screen. Any key goes on (SHIFT, ALT
and the command keys do not count). ESC on this screen quits the program; the original has no quit key,
the port keeps one here only.

**The log-disk prompt.** The original keeps your flight log on the game disk and, when the disk cannot
take it, explains this and waits for RETURN. The port keeps the log in a local file (see The flight log)
and so shows no prompt in a normal run. The screen is in the port for completeness: `--logdisk 1`, `2` or
`3` (no disk, disk not initialised, disk not write enabled) shows its three variants, and RETURN goes on.
With the prompt shown, the port goes on without loading or saving a log.

**Welcome and callsign.** Only with a flight log file (the default in a normal run). The first time, the
welcome text for a first tour of duty types out and then asks for your callsign (see The flight log);
later runs show the continuing-tour welcome with your callsign (its text starts about 2.4 seconds after
the key that left the credits), which goes on to the menu by itself when its text is done. The first key
pressed during it ends it at once; that key is used up by the welcome and does not choose a menu item.

**The main menu.** The title and eight numbered lines type out in four colours, then two grey lines
about ESC and SHIFT-ESC. The keys 1 to 8 and the keypad keys KP-1 to KP-8 choose an item; 9 and 0 do
nothing, and neither does ESC in the menu itself. A short click sounds on every choice. A chosen item is echoed alone on a black screen for a moment,
then its mode starts.

| Key | Item |
| --- | --- |
| 1 | The demonstration: a recorded flight, replayed |
| 2 | Free flight: choose a start and an aircraft |
| 3 | Flight school, part 1: watch the instructor |
| 4 | Flight school, part 2: follow the instructor |
| 5 | Carrier qualification |
| 6 | The list of missions you can choose (empty until you have qualified) |
| 7 | The next mission (needs a flight log with a record) |
| 8 | The flight log |
| F1 to F6 | A chosen mission (needs the log; F2 to F6 open one after the other) |

Items 6, 7 and F1 to F6 are the missions; see The missions. Until you have qualified, the log has no record
and the mission keys do nothing.

**ESC and SHIFT-ESC.** On every screen after the menu, ESC restarts the selection you are in (a flight
fades out, the item is echoed again, and you choose again) and SHIFT-ESC goes straight back to the
menu. SHIFT is the key pressed just before ESC: the game remembers it until the next other key goes down or up, so releasing SHIFT first still counts. A flight that ends this way books its flying time to
the log.

## Free flight

Menu item 2. After the echo the screen fades up on a map of the bay with four numbers, one for each start,
and a caption.

| Key | Start |
| --- | --- |
| 1 | the first airfield (the runway) |
| 2 | the second airfield |
| 3 | the third airfield |
| 4 | the aircraft carrier's deck |

Keys 1 to 3 go on to the aircraft screen: 1 chooses the F/A-18 and 2 the F-16 (the keypad digits do the
same); anything else is ignored. The carrier start takes the F/A-18 and skips the aircraft screen. The
choice is not remembered: it is asked every time. ESC at either screen restarts the selection, SHIFT-ESC
returns to the menu. Other digits also start a flight, but not at a place on the list; see
`docs/easter-eggs.md`.

After the choice the camera flies in from the map to the start, taking about 13 to 30 seconds, depending on the start (93 to 227 steps, then 8 more). You see the
aircraft from outside while the camera closes in, and the picture switches to the cockpit at the end of the
flight-in, as in the original (the camera's path is the original's mover; at the second and third airfield it
differs slightly from the original's after the first seconds). Keys 7, 8, 9 and 0 on the location screen
(see `docs/easter-eggs.md`) have longer approaches of their own. KP-ENTER skips this; P pauses it. The cockpit then appears with the take-off or launch notice typed out,
and after a short wait of four steps the flight begins with the HUD on. Free flight allows five
crashes with either aircraft (the crash allowance); the game-over screen follows the last crash and returns to the menu.

ESC in flight freezes the picture, fades it out and restarts the selection from the location screen;
the flight so far is lost. SHIFT-ESC goes back to the menu.

## The demonstration

Menu item 1 replays a real flight of the original, recorded from the carrier deck: take-off, a circuit,
a landing and a roll-out, 256 seconds long, with the view changes the pilot made. It is the original's
recording read from your disk, played step for step, so it is a way to check that the port flies as the
original did. While it runs only P (pause), ESC and SHIFT-ESC act; every other key is ignored. ESC
restarts the demonstration from the beginning, SHIFT-ESC returns to the menu. When the recording ends
the demonstration starts again by itself. The demonstration adds no flight time to the flight log.

## Flight school

Menu items 3 and 4 are the two parts of one school; both teach the same seven manoeuvres. There is no
score and no pass or fail: the school does not look at how you fly. The instructor flies, and the
lessons end only when you leave.

The seven manoeuvres, in the order the school flies them: aileron roll, break turn, inside loop,
split-S, vertical half-loop, barrel roll and inverted flight. The instructor's name for each appears on
the message line when it starts.

**Part 1 (menu 3).** A briefing types out, then you watch the instructor from the outside view, flying
the seven manoeuvres one after another and starting over after the seventh, for ever. Your own aircraft
does not fly. The keypad changes the point of view and `[` and `]` the distance, as in the outside view;
a view you choose lasts until the next lesson begins. RETURN makes the instructor start over at the
next lesson (it skips one). The flight controls and the throttle keys do nothing in this part. ESC
leaves part 1 and starts part 2. SHIFT-ESC returns to the menu. P pauses.

**Part 2 (menu 4).** A briefing types out and you are in the cockpit behind the instructor on the
runway. Set the throttle to 80 percent (F8) as the briefing asks and the instructor takes off. Follow
the instructor through the manoeuvres: the message line names the next one. SPACE tells the instructor
to begin the next manoeuvre. S toggles the instructor's smoke trail. RETURN puts you back
behind the instructor (a repeat press skips to the next lesson); while a manoeuvre is running it
answers that the instructor is not ready. After the seventh manoeuvre the lessons start over. All the
flight controls work, the crash allowance is one, and the first crash ends the flight with the
game-over screen. ESC restarts part 2, SHIFT-ESC returns to the menu.

## Carrier qualification

Menu item 5. A briefing types out over the carrier deck; any key, or the end of the text, goes on. You are
on the catapult in the F/A-18. Push the throttle to 90 percent or more (F10 is full power) and the
catapult launches you. Fly a circuit and land on the carrier: gear down, hook down (A), and catch a wire.

The qualification is passed when you stop on the deck with the hook caught, the wheels down and the
speed at zero. Then the landing is confirmed on a screen, the first word of your flight log is set
(it unlocks the missions later) and the log is saved, and you are put on the catapult again. There is no
score, no grade and no count of attempts: a missed wire is not a failure, you fly round and try again;
a landing in the sea leaves you floating, and a crash costs one of your three lives. The game-over screen
follows the third. ESC puts you back on the catapult, SHIFT-ESC returns to the menu.

## The flight log

The log is your record: callsign, missions started and completed, weapons fired and hits, hours flown,
crashes. The original keeps it in a file on the game disk; the port keeps a local file.

**Where the file is.** By default `flightlog.bin` in `$XDG_CONFIG_HOME/interceptor/`, or
`~/.config/interceptor/` when that is not set. `--log FILE` uses another file; `--no-log` uses none (no
welcome, no callsign, nothing read or written). A run with `--frames`, `--flight` or `--freecam` uses no
file unless you give `--log`. A file that is damaged or from another version is reported and replaced
by a new log at the first save. A save that fails is reported once and the game goes on.

**Callsign entry.** On the first run the welcome asks for a callsign. Type letters (they appear in
upper case), digits and spaces, BACKSPACE to delete, RETURN to finish. Other keys do nothing. The log
keeps the first eleven characters. As in the original, a twelfth character does odd things; see
`docs/easter-eggs.md`.

**The log page.** Menu item 8. The page lists missions started and completed, the cannon and the two
missiles with fired and hit counts, flight time in hours and minutes, crashes and hits taken. Keys: 1
saves the log (UPDATE), SHIFT and 2 clears all of it (RESET: after it, UPDATE saves the empty log and the
callsign is asked again), ESC returns to the menu. The flight time counts the time you are airborne, not
the time on the ground or paused.

**When it is saved.** At the end of the callsign entry, after the qualification debrief, at the end of a
completed mission, and on UPDATE. Nothing else saves it: a session that ends without one of these
loses its counts.

## Controls

The port maps the original's keys. Where the original uses a different key, or the port could not use
one, the table says so. On every screen outside the flight the keys act once when pressed; a held key is
not repeated.

### In flight (free flight, the demonstration's pause, qualification, part 2, the missions)

| Key | Effect |
| --- | --- |
| LEFT, RIGHT | roll left or right (held: the demand ramps up while the key is down) |
| UP, DOWN | stick forward (nose down) or back (nose up) |
| `,` and `.` | rudder left and right |
| `=` or `+`, `-` (keypad too) | throttle up or down while held |
| F1 to F10 | throttle presets: the throttle moves to 12 times the key number; F10 means full power, and a second F10 lights the afterburner; F1 pressed again while it is set gives idle |
| mouse up and down | throttle lever (the original's mouse throttle); moving the mouse down lowers the throttle. The mouse buttons are not read |
| G | gear up or down (refused while the wheels are on the ground) |
| A | arrestor hook down or up (F/A-18 only) |
| BACKSPACE or `\` | brake on or off (a toggle: wheel brakes on the ground, air brake in the air) |
| H | HUD on or off |
| M | the map; M again returns to the cockpit |
| P | pause or resume |
| KP-ENTER | switch between the cockpit and the outside view |
| SHIFT and KP-ENTER | the fixed camera: the view stands at the start location's point and looks at the aircraft (a ring key returns to the ring camera) |
| KP-8, 9, 6, 3, 2, 1, 4, 7, 5, KP-dot | in the cockpit, head directions; in the outside view, the viewing direction (see Views) |
| `[` and `]` | farther and closer: cockpit zoom, outside distance level, or map altitude, depending on the view |
| RETURN | select the weapon: AMRAAM, Sidewinder, cannon, none (gear up); in part 2, put you back behind the instructor |
| SPACE | fire the selected weapon (the cannon while held); in part 2, start the instructor's next manoeuvre. SHIFT and SPACE does nothing |
| T | designate the next target of the radar list, or none |
| R | the radar scope's range: 40, then 10, then 2 miles, then 40 again |
| Y | the next page of the message line's target data: altitude, heading, speed |
| F | release a flare (16 at the start; the 16th press finds the store empty) |
| C | release chaff (16 at the start; the same) |
| J | the ECM switch on or off |
| SHIFT and E | eject (E alone does nothing, as in the original) |
| SHIFT and F | in mission 4 (F4), drop the rescue pod (a parachute); elsewhere nothing |
| W | **replenish (a port feature, see below)**: refuel and rearm in the air or on the ground |
| S | toggles the instructor's smoke trail, part 2 only; no effect elsewhere |
| ESC | restart the selection: the location screen in free flight, the briefing in the qualification, part 2 again in part 2, the mission from its orders in a mission; part 1 goes on to part 2 |
| SHIFT and ESC | back to the main menu from any mode |

**The replenish key (W) is not in the original.** The original has no key that refuels and rearms the
aircraft: the refuel and rearm happens only after a landing on a pad or the deck. The port adds one key
for it, W, because W is one of the keys the original does not use and is free on every keyboard layout
(`docs/easter-eggs.md` tells where it comes from). It does what the landing does: the fuel is made full,
the missile stock is two Sidewinders and four AMRAAMs, the cannon has 500 rounds, flares and chaff are 16
each, any missile of yours still in flight is removed, and a message says that you are refuelled and
rearmed (it is not posted again while it is still on the line). It acts where the other flight keys act:
in a running flight, also while paused or in the countdown after ESC, in a mission as in free flight. It
does nothing in the demonstration, in the first part of the flight school, in the briefing and outcome
screens, while the ejection runs, or on any other screen. The flight log's counters are not touched.

### On the screens

| Screen | Keys |
| --- | --- |
| credits | any key goes on; ESC quits the program |
| log-disk prompt | RETURN goes on |
| welcome | none: the first-tour welcome goes on to the callsign entry, the continuing welcome to the menu, when the text is done |
| callsign entry | letters, digits, space, BACKSPACE, RETURN |
| menu | 1 to 8 and KP-1 to KP-8 choose; F1 to F6 choose a mission; 9, 0 and ESC do nothing |
| location | 1 to 4 (KP-1 to KP-8 as digits); P freezes the map |
| aircraft | 1 F/A-18, 2 F-16 |
| approach | KP-ENTER skips; P pauses |
| log page | 1 update, SHIFT and 2 reset, ESC menu |
| any screen with a mode | ESC restart the selection, SHIFT-ESC menu |

### Flight school part 1

Only the view keys act (KP-1 to KP-9, KP-dot, KP-ENTER, `[`, `]`), RETURN (skip a lesson), P, ESC and
SHIFT-ESC. The flight controls, the digits and F1 to F10 are ignored.

### The demonstration

P, ESC and SHIFT-ESC.

The keyboard throttle follows the original: the lever moves a step per game step while the key is
held, and the F-key presets move it toward the stored value and stop there.

### Game controller

A game controller (anything SDL knows as a game controller: the usual two-stick pad) plays the same
keys; it sends the keyboard's key and nothing else, so every rule above applies unchanged. The browser build uses SDL's gamepad support. The controller is flight input only, like the mouse
and the joystick of the original (they are not menu input): outside the flight only A is taken,
as RETURN, to leave the screens that wait for RETURN.

| Control | Key | Effect |
| --- | --- | --- |
| left stick left, right | LEFT, RIGHT | roll |
| left stick up, down | UP, DOWN | stick forward, back |
| right stick left, right | `,` and `.` | rudder |
| right stick up, down | `=` and `-` | throttle up, down (while held) |
| right trigger (RT) | SPACE | fire |
| left trigger (LT) | `\` | brake |
| A | RETURN | select the weapon |
| B | G | gear |
| X | T | next target |
| Y | R | radar range |
| left bumper (LB) | F | flare |
| right bumper (RB) | C | chaff |
| left stick press | J | ECM |
| right stick press | KP-ENTER | cockpit or outside view |
| Start | P | pause |
| Back | M | the map |
| D-pad up, down, left, right | KP-8, KP-2, KP-4, KP-6 | head or viewing direction |

The stick is the digital stick of the original's keys: a direction is held or not, and the aircraft's
own ramp does the rest. A stick or trigger counts as held in a direction when it is pushed more than
8000 of its 32,767 (about a quarter of the travel), and as released at 8000 or less. This dead zone is
a port choice; the original has no analogue stick. The hook, the HUD switch, the message page, the
zoom keys, the function-key presets, the ejection and the other SHIFT keys stay on the keyboard.

## Views

**Cockpit.** The default. You see the canopy view with the HUD, the cowl and the instrument panel at the
bottom. KP-8 and KP-9 look straight ahead.

**Head glances.** KP-4 turns the head one step to the left and KP-6 one step to the right (eleven steps
round the aircraft; KP-4 from straight ahead turns to the far left, KP-6 from the far left returns to
straight ahead). KP-2 looks straight back. KP-7 looks up and KP-1 looks down. The HUD and the panel slide
out of the picture as the head turns, and the window grows taller when you look far to the side or
behind. A glance is momentary: when you release the key the view returns to straight ahead four to five
seconds later. KP-5 locks the present view so that it stays.

**Outside view.** KP-ENTER leaves the cockpit and shows the aircraft from behind. The camera follows and
looks at the aircraft. The keypad picks the viewing direction:

| Key | Direction |
| --- | --- |
| KP-8 | from the front |
| KP-9 | front right |
| KP-6 | from the right |
| KP-3 | back right |
| KP-2 | from behind (the default) |
| KP-1 | back left |
| KP-4 | from the left |
| KP-7 | front left |
| KP-5 | above and behind |
| KP-dot | below and in front |

`[` moves the camera farther away and `]` closer. The distance levels are close (the default), then
1, 2, 3 and 4 times further; the camera stops at each end. The camera never goes below a little above
the ground. If the aircraft turns sharply the camera swings round to keep behind it. A line at the bottom
shows the heading, speed and altitude (in flight school part 1 it shows the instructor's). KP-ENTER
returns to the cockpit.

**Cockpit zoom.** In the cockpit `]` zooms in one step and `[` zooms back out (three steps in all).

**Map.** M shows a map from straight above the aircraft, north at the top, with the grid and the numbered
latitude and longitude lines. `]` lowers the map's altitude (a closer, more detailed map) and `[` raises
it. M or KP-ENTER in the map returns to the cockpit, whatever view you entered it from. The aircraft
are drawn on the map as small arrows that point the way they head, in sixteen directions: yours and the
friendly ones in one colour, the hostile ones in another, and the carrier as a ship shape. The arrows are drawn
on every other step and the carrier on every step, as in the original, and a friendly aircraft on the
ground is not drawn. After Shift and
KP-ENTER (the fixed camera) the arrows are drawn, on every other step, from the position they had at the step before.

View keys keep working while the game is paused.

## The instruments

The HUD in the cockpit is drawn like the original's (the picture is 320 by 200 and is scaled by a whole
number to the window, with letterboxing, so its pixels keep their shape).

| Element | What it shows |
| --- | --- |
| outline and crosshair | the outline of the HUD glass, the aiming crosshair, the level marks (the horizon of level flight) and the nose datum. These stay when the HUD is switched off with H |
| heading tape | a strip at the top with the heading in tens of degrees (00 to 35), ticks every 5 degrees and a centre mark |
| speed (KT) | the indicated speed in knots, to the left of the crosshair |
| altitude (FT) | the height in feet, to the right |
| G meter | the load factor in tenths of a G, at the lower left (1.0 in level flight) |
| weapon readout | the selected weapon and its count (cannon rounds, Sidewinders or AMRAAMs left); empty when no weapon is selected |

The original has no pitch ladder, no roll scale, no vertical-speed figure and no velocity vector, and
neither does the port: the horizon is the world itself, and the level marks show where the nose should be
for level flight. H switches the numbers, the heading tape and the cannon sight off; the outline, crosshair, level marks,
nose datum and weapon readout remain.

The instrument panel shows the speed, altitude, throttle percentage, fuel, the gear lamp, the attitude
ball, the radar scope, the threat lamps and the message line. The message line shows one message at a time (for
example the stall warning, low fuel, crash imminent), blinking the urgent ones; with a target designated it
shows the target's data instead (see The radar and the target box).

Two visual cues of strain: in a hard pull in the cockpit the picture darkens (the pilot's grey-out), and
when the aircraft hits something at speed and crashes, the screen flashes orange once before the restart.
The picture also shakes by one scanline for a moment at a touchdown, a lift-off and a hit at speed, as in
the original.

## Flying

**Take-off.** On the deck, push the throttle to full (F10) and the catapult launches you. On the runway,
hold full throttle, wait for flying speed (about 150 knots, a little over the stall speed) and pull back
(DOWN). Raise the gear (G) once the wheels are clear; it takes a moment to retract. The wheels cannot be
raised or lowered on the ground.

**Gear and hook.** Gear down gives more drag and a slower speed target; raising it with the wheels
down is refused. The hook (A) is for deck landings: an arrestor wire catches it when you touch down in the
wire zone.

**Brakes.** The brake key toggles the brake. On the ground it brakes the wheels; in the air it is an air
brake that slows you.

**Stall and strain.** Below the stall speed the aircraft stops flying properly and the stall warning
appears on the warning line. A hard sustained pull dims the cockpit view.

**Crash and restart.** A hard touchdown, running into the carrier or a landmark above about 80 knots, or
flying into the sea ends the flight. In the cockpit the picture freezes for about two seconds, flashes
orange, and the flight restarts at its start point with the crash counted. The number of lives depends
on the mode: five in free flight, three in the qualification and in the missions, one in flight school part 2. When they are
used up a game-over screen types out (a different one for the F-16 and the F/A-18) and you return to the
menu. In the outside view there is no freeze: the wreck is shown for a short wait and the flight then
restarts.

## Weapons

The aircraft carries three weapons: the cannon, the AIM-9L Sidewinder (short range, heat seeking) and the
AIM-120 AMRAAM (long range, radar guided). Every flight starts with 500 cannon rounds, 2 Sidewinders and
4 AMRAAMs, and 16 flares and 16 chaff (see Countermeasures). The same stores are put back at each restart
after a crash, at the start of a mission, when you come to a stop on an airfield pad or on the deck after a
landing (once you have been airborne), and when you press W (see below).

**Selecting.** No weapon is selected when the flight starts. RETURN steps through AMRAAM, Sidewinder,
cannon and none, and round again. The readout to the left of the nose datum shows the choice: the cannon
with the rounds left, or the missile type with the number of missiles left. Selecting and firing both need
the gear up; with the gear down or still moving, RETURN and SPACE do nothing.

**Firing.** SPACE fires the selected weapon. A missile leaves on the press, one per press, whether or not
anything is designated or locked. At most three of your missiles can be in the air at once; a press with
all three out does nothing. The cannon fires while SPACE is held: five shots, then a pause of about two
and a half seconds, then five more. Letting go of SPACE ends the pause at once, so a tap gives one shot.
Each shot spends three rounds. A tracer falls a little as it flies, lasts about two and a half seconds
and is stopped by buildings. The cannon sight (a single dot) trails the nose by two steps.

**What the cannon does.** A tracer that passes through an aircraft hits it. Three hits destroy the
aircraft. The cannon does not tell friend from foe: an airliner, a test aircraft or Air Force One is
destroyed as readily as a fighter (and is not counted as a kill). Your own tracers and missiles never hit
you.

**What a missile does.** A Sidewinder flies for about 19 seconds, an AMRAAM for 25. For the first
second or so it flies straight; then it turns toward your designated target, slowly at first and at the full
rate after about another second. It steers at whatever is designated at that moment, not at what was
designated when you fired: designate another aircraft (T) and the missile follows the new one. Fired with
nothing designated it flies straight on; if its target is destroyed it goes on toward the last place it
saw it and dives. In the last six seconds the motor is out: it coasts, slows and no longer steers.

A missile hits only if it passes within about 100 feet of its target. There is no proximity fuse, and a
fast crossing can carry it through. A missile that has gone past a target without a hit is lost and turns
more slowly from then on. Every hit destroys the aircraft it strikes (there is no blast area). The wreck
burns and falls for about two seconds, and the kill is counted at the end of that if the wreck was still in
the air. A missile that reaches the ground leaves a fire column that burns for about sixteen seconds. A
missile that hits nothing disappears when its flight time is up.

**The aiming symbols.** They are drawn in the cockpit view at the normal zoom, and need a designated target
(see The radar and the target box).

| Symbol | Meaning |
| --- | --- |
| target box | a square around the designated aircraft, while it is in front of you and on the screen |
| marker | with a missile selected, a small diamond that slides toward the target box. When it is inside the box it becomes a ring that blinks: that is the lock. If the target leaves the box the lock is dropped |
| lock circle | a large circle around the cannon sight |
| range dot | a small dot on the lock circle that runs clockwise from the top as the target gets farther away; once round is 45,000 feet, so the top is zero range, the right side 11,250 feet, the bottom 22,500 feet |
| fire prompt | a short word to the right of the nose datum. With a missile: the target is locked and within range (7,680 feet for the Sidewinder, 12,480 feet for the AMRAAM). With the cannon: the target is on the sight and within 2,880 feet |
| range number | beside the prompt: how much the range changed since the last update, in four digits. A blank in front of the digits means the target is closing; a minus sign means it is opening or steady. It is per update, not per second: it is renewed every step when the target is within about 1,440 feet, every third step out to 2,880 feet and every sixth step beyond |

With the HUD switched off (H) the cannon's symbols go with it: the sight (the circle, the range dot, the
range number, the pipper and the cannon's fire prompt) is part of the HUD, and the cannon's prompt
stops updating until the HUD is back. The target box, the marker and a missile's prompt are not.

A lock is not needed to fire, but a missile launched with no target in the box, or too far, will rarely
find anything.

**The log.** The flight log counts the cannon rounds fired and the cannon hits, the Sidewinders and
AMRAAMs fired and how many hit, and the hits you took from missiles. As in the original, a flare or chaff
release is counted as cannon rounds fired (three each).

## The radar and the target box

The radar scope is in the instrument panel. It works only in the cockpit view; in an external view it
stands as it was. Your aircraft is the cross near the middle of the scope; ahead is up.

**The range key.** R steps the scope's range: 40 miles, then 10, then 2, then 40 again (the figure shows in
the panel; a flight starts at 40). It changes the scale of the scope, so it also changes which contacts are
on the scope, and with them which contacts T can pick.

**Contacts.** Every aircraft, missile, ship and pod in the area is a contact. The scope draws up to ten at
a time as dots whose colour tells the kind: ships, hostile aircraft, friendly or neutral aircraft, missiles
and contacts too far to identify each have their own. A contact more than about 13 nautical miles from you,
in either direction along the ground, is too far to be detected: it is drawn dimmed and its data says that
there is no signal. The dot of the designated target blinks. The scope is redrawn on two steps out of four,
as in the original.

**Threat lamps.** Four lamps in the panel show what the scope has found: a hostile aircraft in range, a
friendly aircraft, a short-range missile aimed at you, a long-range missile aimed at you. A fifth lamp
flashes with the stall warning.

**Designating a target.** T picks the next target. It walks the contacts that are on the scope at that
moment, in the game's list order (not by distance); after the last one comes "no target", then the first
again. Missiles, ships, pods and aircraft that are already out of control cannot be designated. Friendly
aircraft and airliners can, and so can a contact too far to detect. The designation is dropped by itself
when the target is destroyed or loses control, and a message says that the target is destroyed. Designating
needs no weapon.

**The message line.** The line at the bottom of the panel shows warnings. With a target designated it shows
data about the target instead: its type, and one of three pages. Y changes the page: altitude in feet,
heading in degrees, speed in knots. Page one shows when you designate. When a new warning about a missile
or an aircraft comes up it is shown for about three seconds before the data returns. For an undetected
target the line shows the no-signal text in place of the type, and the speed reads zero. With no target,
Y blanks the line for a moment and the warnings return.

**ECM.** J switches the electronic countermeasures on or off, and a lamp in the panel shows the state. It
is off at the start of every mission and stays as you set it through pauses and crash restarts. See
Countermeasures for what it does.

## Countermeasures

You have flares, chaff and the ECM switch. They defend you against the missiles that hostile aircraft fire
at you, and against nothing else: hostile aircraft have no guns, and your own missiles ignore decoys.

**Flares (F).** A flare burns for about four seconds and draws heat-seeking missiles away. It beats the
Sidewinder type of missile and does nothing against the AMRAAM type.

**Chaff (C).** A cloud of chaff stops where it is released and hangs there for about seven seconds. It beats
the radar-guided AMRAAM type of missile and does nothing against the heat seeker.

**Using them.** One press, one release. You start with 16 of each, but the sixteenth press finds the store
at its last count and refuses, so you have fifteen releases; a message tells you that the store is empty,
and repeats it on every further press. A release draws on the cannon's burst reserve, so it can wait a
moment if you have just fired the cannon. A decoy helps only if it goes out in time: released more than
about a second and a half before the missile arrives it can pull the missile off your track, and released
later it never helps. How much a decoy helps also depends on the geometry, so release it early and turn
away from the missile.

**ECM (J).** With the ECM on, a hostile Sidewinder aimed at you turns much more slowly and is easier to
outfly. The AMRAAM is not affected. The price is that hostile pilots then fire twice as often. Switch it on
when a missile is coming and off when none is.

**Against the damage.** A flare or chaff released in the last three and a half seconds also protects you
from the engine failure of a hit (see Damage).

## Damage

There are no hit points. A hit has an effect or it does not. The things that can hit you are a hostile
missile and a collision with another aircraft (hostile or not); hostile aircraft have no guns.

**What a hit does.** A hit below 80 knots does nothing to you, and the other aircraft or missile is still
destroyed. At 80 knots or more one of two things happens, about half the time each, depending on the exact
moment: either the engine fails and you lose control, or an alarm comes up (the hydraulics or the
structure) with a jolt and nothing else changes. A flare or chaff released in the last three and a half
seconds skips the engine failure. Running into a friendly aircraft or an airliner counts as a collision in
the same way and destroys it.

**An aircraft that has lost control.** The engine failure is for good. The throttle, the afterburner and the
fuel no longer change your speed; the nose drops and stays down, the pitch control does nothing and you can
no longer climb. The roll and the rudder, the weapons, the radar, the gear and the hook go on working.
Neither a landing nor time repairs the fault. The aircraft falls to the ground or the sea and crashes,
unless you eject (see Ejection). After a crash the flight restarts with a sound aircraft.

**The jolt.** A hit at speed shakes the cockpit picture by one scanline for four steps, and so do a
touchdown and a lift-off; the HUD and the panel move with the window.

## Ejection

Shift and E eject; E alone does nothing, as in the original. The key acts in a running flight, not in a
pause.

**The sequence.** The canopy goes first, and the seat leaves the aircraft about three-quarters of a second
after the key. In the cockpit view the picture switches to the seat; in an external view the camera stays
as it was. The seat rises, then falls, slowed by the air, and lands when it reaches the ground or the sea.
Your aircraft is left behind and goes on in the world. About a second and a half after the seat lands the
flight ends: a short text tells you that you survived the bail-out, and the menu follows. If you eject in a
mission the flight ends with that text and no mission outcome, unless an outcome has already arrived. An
aircraft that crashes while you are in the seat does not end the flight by itself.

**The parachute by key.** A second Shift-E pressed at one exact step early in the sequence, within the
first half second, opens the parachute instead of the seat. At other moments it does not.

**Rescue (Shift and F).** In mission 4 (F4) only, Shift-F drops the rescue pod. The pod is a parachute released
from your aircraft. Only one can be out at a time. It falls to the ground and is judged where it lands (see
Mission 4). The pod and its canopy are drawn. In the other missions and in free flight Shift-F does
nothing; F alone is the flare.

## The missions

Menu item 7 starts the next mission, item 6 lists the missions you may choose, and the keys F1 to F6 start
a mission directly. They need a flight log with a record, which the carrier qualification creates. F1 is
open from then on; each of F2 to F6 opens when you have completed the one before it. Item 7 starts the
mission after the last one you completed (F1 when you have completed none, or after F6).

**Getting under way.** The screen fades up on a frozen picture. An intro text types out with the date, then
the briefing of the mission (a different text once you have completed that mission before); a key press moves
on. The camera then shows the first hostile and pulls back to a map overview while the orders type out. The
flight log counts one mission started when the orders come up, and again at every ESC restart. F2 to F6
start on the carrier's catapult. F1 starts at the airfield and asks for the aircraft first: 1 for the
F/A-18, 2 for the F-16. Before the first step a notice gives the bearing, in degrees, of the first hostile,
and the take-off or launch notice follows. The original's security prompt is left out.

**Difficulty.** How many enemy aircraft there are, and where they start, depends on how many missions you
have completed since the program started (the count starts at zero each time the program starts, whatever
the log says). At zero most missions have a single fighter; the second and third come after one and two
completions. The start positions also vary a little from run to run (`--seed N` fixes them).

**Rules common to all.** You have three lives in a mission. There is no score: the only record is the
counters of the flight log. A mission ends when its aim is met or when it is failed. When the aim is met the
outcome text types over the frozen picture and the flight goes on; you then fly back, land and come to a
stop (F1: on the airfield pad; the others: on the carrier deck, with the hook caught), and the mission is
complete. That needs you to have taken off, nothing more: no time limit, no fuel check, no damage check.
Completion shows a congratulation text and a hint about leaving, updates and saves the log, counts the
mission as completed and opens the next one. After it the flight just goes on until you leave.

ESC restarts the mission and SHIFT-ESC returns to the menu. **After a crash, even with lives left, the
mission can no longer be won or lost:** the crash switches its rules off until you restart it with ESC. A
mission whose failure lets the flight go on is in the same state: you can fly on but the mission is over.
P is ignored while an outcome text is up.

**Mission 1 (F1): identify.** An unknown aircraft is approaching the carrier; it may even be an airliner.
Identify it without opening fire unless fired upon, then return to the airfield. The mission is won by
designating a target (T) and keeping aircraft on the scope, in the cockpit view and within about 13
nautical miles, for about eight seconds with one aircraft (every aircraft on the scope counts, not only the
designated one, so several aircraft make it quicker, and the time need not be in one stretch). Shooting does
not fail the mission, and nothing else does: it has no failure outcome. Then land at the airfield.

**Mission 2 (F2): Air Force One.** Two MiG-29 fighters attack Air Force One, which is damaged and trying
to land at San Francisco. Destroy the attackers. The mission is won when Air Force One is on the ground,
nearly stopped, and both attackers are gone; if it lands while a fighter is alive nothing happens yet. It
is lost as soon as Air Force One is destroyed, by anyone. The outcome text is then one of two, picked by
chance (the passenger aboard or not) and the flight goes on with the mission over. If you destroyed it
yourself, with a missile or by running into it, a third text says so, the flight ends and the menu follows.

**Mission 3 (F3): the stolen aircraft.** Two F-16 test aircraft have been stolen and head for the eastern
edge with two MiG-29 escorts. Force them back without a fight. Stay with a thief, within about 960 feet in
each direction (480 to 720 feet after a few completions), for 25 seconds in a row; a break starts the
25 seconds again. The mission is won when you have shadowed a thief like that and the escorts are destroyed
(or both thieves are down). If you fire a weapon at all the thieves turn hostile, and each of them must then
be shot down as well. If a thief reaches the eastern edge the mission is lost, the failure text types out
and the flight ends. More escorts can join when you enter the area where the thieves start.

**Mission 4 (F4): the rescue.** A pilot is down near the Farallon Islands. Fight through the bandits, find
the pilot's marker and drop the pod (Shift-F) on it. The mission is won if the pod lands within about 320
feet of the marker in each direction, and lost if it lands farther away; a pod never dropped leaves the
mission undecided. After the failure text the flight goes on with the mission over.

**Mission 5 (F5): the cruise missile.** A cruise missile with a nuclear warhead is inbound to Moffett
Field. Intercept it. The mission is won when the missile has been destroyed before it arrives. If it
arrives the mission is lost: the text tells of an explosion and then, by chance, either of nuclear
destruction (the flight ends and the menu follows) or of no detonation (the flight goes on, the mission
over).

**Mission 6 (F6): the submarine.** A submarine carrier is the origin of the enemy aircraft. Fly in low
and sink it while it is surfaced, against fighter cover. The briefing speaks of the submarine, but the
mission's rule counts only aircraft: it is won when four enemy aircraft have been destroyed. It has no
failure text. Take care in the area just in front of the submarine before the fighters are down: flying into
it launches a cruise missile, which takes one of the four places and keeps the count from reaching four.

**The outcome screens.** The world freezes about three-quarters of a second after the mission ends, and the
text of the outcome types out over the frozen picture. A won mission shows its text, which ends by sending
you back to base, and the flight resumes where it was. A lost one either resumes with the mission over or
ends the flight and returns to the menu, as set out above.

## Sound

The sound is the original's: the engine (two voices a little apart in pitch, following the throttle), the wind at low throttle, the afterburner, the cannon, the missile launch,
the explosions and the ejection bang, the landing gear, the click of a key on the screens, the warning and
cue tones, and the music of the start-up. The port runs the original's sound routines and plays their output
through a copy of the Amiga's four sound channels (two on the left, two on the right), so the pitches, the
volumes and the order and length of the sounds are the original's, each at the moment of its step of the
game. One thing differs: the original's screen refreshes 49.92 times a second and the port's game runs
on exactly 50 refreshes a second (8 steps a second), so every sound is 0.16 percent faster, and a hair
higher in pitch (under three hundredths of a semitone), than on the Amiga, in step with the game around it. The sounds come
from the disk you extracted: nothing is installed beside the game data. If a sound file is missing or too short the game
says so once on the console and goes on as the original does when it cannot load that sound: without the
cockpit engine file the engine is played from the outside engine at a quarter of the volume, without the
wind file there is no wind, without the music the start-up plays a short sound instead, without the gear file the
gear key beeps, and so on. The same happens when the computer has no sound device: the game runs without
sound.

When you play, the sound follows the game on the clock of the game, a little behind it (about 60 milliseconds, so that
a slow moment of the computer does not make the sound stutter). If the computer stalls for a moment the
sound waits and takes up again where the game is; if the game runs ahead of the sound device the
sound catches up by a tiny change of speed (never more than half a percent) and, after a long stall, by
skipping the oldest few hundredths of a second, never by dropping a start or a stop of a sound.

The sound goes through the filters of an Amiga 500 with its power light on: a gentle low-pass at about 4.4 kHz
and another at about 3.1 kHz, and a high-pass at 5 Hz that removes any offset. This is what the original
sounded like on the machine it was played on. `--no-filter` leaves them out, which sounds brighter and harsher.
`--no-audio` opens no sound device. `--wav FILE` writes the sound of the run to a 16-bit stereo WAV file at
44,100 Hz, when the program ends. Each game step contributes the time of its 6 or 7 screen refreshes (an eighth of a
second on average), so the file does not depend on the sound device or on the speed of the computer, and two runs
with the same keys and the same `--seed` give the same file (use it with `--frames N` and `--no-audio`). It is on
the port's clock of 50 refreshes a second, as described above.

In the browser the page cannot make a sound until you have interacted with it, so the sound starts when you
click Start.

## Pause

P pauses and resumes. While paused the world and the clock stand still, but the view, zoom, head, map
and HUD keys still work (the picture does not redraw until the pause ends). The pause shows its own text
over the dimmed picture; its characters click as they are typed, as in the original, and the engine sound stops while
the pause stands and returns a few blanks after you resume.

## In the browser

The browser version plays the same game. Open the page, drop your own disk image of the F/A-18 Interceptor
disk (an `.adf`, 901,120 bytes) on it or choose it with the button, wait for "Ready", and click Start. The
disk is read in your browser: nothing is uploaded, and the page hosts no game data. If the file is not an
ADF, is a flux image (SCP, HFE, MFI) or is another disk, the page says so and you can pick another file.

The keys are the ones of the Controls tables above, and the page lists them. The keypad, SHIFT and the function keys
reach the game (the headless Chromium check, `tools/web/smoke.py`, presses F1 to F4 and F6 to F10, the keypad, SHIFT, Backspace and the comma, and the game takes them; F5 is the reload key and was not pressed), so the port offers no alternative keys. A browser
may still act on a function key itself before the page sees it (F5 reloads, F6 goes to the address bar,
F11 is full screen, F12 opens the developer tools, some systems use F1 for help); if yours does, set the
throttle with `=` and `-` instead of the presets. SPACE and the arrow keys do not scroll the page while you
play. When the game ends (ESC on the credits quits, as in the original) the page says so; drop the disk image again and press Start to play again. Click on the game once if the keys
seem to do nothing.

The flight log (your callsign and record) is kept in the browser's own storage, for this browser profile and this address: it is written at
every save point of the game and is there again the next time you open the page and start the game, after a reload or a restart of the
browser. It is not shared between browsers or profiles and is gone when you clear the site's data. In a private window, or when the browser
refuses storage or it is full, the page says in one line that the flight log cannot be kept; the game then plays normally with the log in memory
for that visit only.

What a person has to check in a real browser (the function keys the browser may keep, sound in Chromium, Firefox and Safari, a physical
controller) is listed in [browser-checklist.md](browser-checklist.md).

## Command line

`build/interceptor [options]`

| Option | Effect |
| --- | --- |
| `--assets DIR` | where the extracted assets are (default `assets`) |
| `--scale K` | initial window size of 320K by 200K pixels (the window is resizable) |
| `--window-size WxH` | initial window size in pixels, instead of `--scale` |
| `--flight` or `--autostart` | skip the screens and start the flight at once on the carrier deck, in the outside view unless `--cockpit` |
| `--start-keys` | take the screens' own way into free flight with the keys sent for you: any key, the menu's 2, location 1, aircraft 1 (and RETURN at the welcome, with a log file) |
| `--cockpit` | start in the cockpit view |
| `--runway` | start on the runway instead of the carrier deck (with `--flight`) |
| `--freecam` | the milestone-2 free camera (no aircraft); the stick turns it and the throttle sets its speed |
| `--stick X Y` | hold the stick at X (right) and Y (nose up) percent, -100 to 100, from the first frame (use with `--freecam`) |
| `--throttle P` | set the throttle lever to P percent (0 to 100) from the first frame |
| `--log FILE` | load the flight log from FILE at the start and save it there at the save points |
| `--no-log` | use no flight log: no welcome or callsign entry, nothing read or written |
| `--logdisk N` | the log-disk check result: 0 usable (the default, no prompt), 1 no disk, 2 not initialised, 3 not write enabled (these show the log-disk prompt) |
| `--synthetic` | run the invented world of the tests instead of the assets: no extracted game data is read (flight tables, missions and the sounds are made up, so it plays like a test fixture); the desktop build only, the browser build refuses it |
| `--no-audio` | open no sound device (the sound still runs for `--wav`) |
| `--no-filter` | leave out the Amiga 500's output filters |
| `--audio-log FILE` | write the register writes of the sound library, one line per write (step, op, voice, object, blank, value, offset, length, clock); for tests |
| `--wav FILE` | write the run's sound to FILE: 16-bit stereo, 44,100 Hz, an eighth of a second for each game step on average |
| `--seed N` | the seed of the generator that places the hostiles and the carrier at a mission start (which variant of the mission, and the small random offsets); without it the clock seeds it, so a mission differs from run to run as in the original |
| `--frames N` | run N game steps (one per frame) and quit; for tests and screenshots |
| `--dump FILE.ppm` | write the last frame as a PPM picture |
| `--dump-window FILE.ppm` | also write the last frame at window resolution |
| `--dump-layer FILE` | also write the overlay layer (320 by 200 bytes, 255 meaning nothing drawn) |
| `--dump-flash FILE.ppm` | write the orange crash flash frame, if one was shown |
| `--key NAME@N`, `--keyup NAME@N`, `--keyrep NAME@N` | press, release or auto-repeat an SDL key (such as `"Keypad 6"`, `"["`, `"M"`) at the start of frame N; for scripted runs |
| `--ctrl NAME@N[:MAG]`, `--ctrlup NAME@N` | press or release a game-controller control at step N (NAME as in the Game controller table: `a`, `rt`, `x-left`, `dpad-up`; MAG 0 to 32767 is an axis deflection, 32767 when left out); for tests |
| `--resize-at N WxH` | resize the window after frame N (a test of the resize path) |
| `--hud-test SPEC` | override the HUD state each frame with `name=value,...`; for tests |
| `--text-test SPEC` | override the screen mode, palette and text lines each frame; for tests |

A headless screenshot of a runway take-off, 60 steps in:

    SDL_VIDEODRIVER=dummy build/interceptor --flight --cockpit --runway --frames 60 --dump /tmp/f.ppm

The live game steps the simulation 8 times a second (the original's mean rate in flight; its own speed
varied with the scene) and presents at the display rate, so the picture updates in 125 ms steps. The
waits of the screens (the typing of the texts, the fades, the approach) are converted to this rate.

## Not in yet

Not implemented: the original's security prompt (the code wheel) is left out on purpose, and so is its check of the pictures at start-up. The
mouse buttons are not read.

Known differences from the original that are not rules but open measurements (the list is in
`docs/superpowers/plans/2026-10-16-milestone-7-followups.md`): the approach before a mission (F1 to F6, menu 6 and 7)
shows the outside view with the mover, as the original does, but the camera path and the length of each mission's approach are
measured for one start only (the port is one pass off at one point of the path); the approach camera at the second and third
airfield departs slightly from the original's; the pause text has nine of the original's ten clicks. The sound runs at
a 50 Hz clock (the reference machine's frames were 49.92 Hz), and the output level and the filter are the port's choices.
