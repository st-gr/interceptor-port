# Hidden keys, jokes, quirks and unused content

The original *F/A-18 Interceptor* hides little. A sweep of its key handler (every key pressed once in
every phase, plus a reachability pass over the handler's code) found no cheat codes and no secret
screens, and no text that reacts to a name, a score or a date. What it did find is listed here: keys
that do something nobody documented, a few jokes and failure screens, side effects of its own
arithmetic, and content that is on the disk but that no path reaches. Each item says how to trigger it
in the original and what the port does.

The port is a clean-room replica: where the original does something odd, the port does the same thing
unless the item says otherwise. Game text is not quoted here; items are identified by their role. The
evidence behind each item (the captures, the counts) is in note 08, section 11, of `docs/re/`.

Status words: **in the port** (reproduced as the original does it), **not reproduced yet** (planned,
with the milestone), **original only** (left out on purpose or impossible in the port).

## Hidden keys

### The extra location digits

The location screen of free flight names the starts 1 to 4. The original accepts every digit key,
because the routine that reads the start table walks it without a bound, and so the keys 5 to 0 start a
flight too, at places nobody meant as starts.

| Key | What the original does | Port |
| --- | --- | --- |
| 5 | the table entry names an aircraft record that is not active; the game falls back to the first airfield | in the port: the first airfield, as key 1 |
| 6 | the end of the table is read as a record number; the game falls back to the first airfield | in the port: the first airfield, as key 1 |
| 7, 8, 9 | table data is read as a start: the aircraft appears at a position taken from that data, at or beyond the edge of the map, with heading 0 | in the port, at the same position and heading |
| 0 | the same, with a heading far above a full circle | in the port, at the same position; the heading is reduced to a single circle until the original's behaviour with such an angle is settled |

The approach before the flight is longer from these starts than from the airfields: measured at 227 passes for 7, 223 for 8, 200
for 9 and 192 for 0 (the airfields take 93 to 118, +-1 pass in each figure). **In the port** the same counts, shown from
outside as at the airfields.

Trigger: free flight (menu 2), then the digit on the location screen. The aircraft screen follows, as for
the airfields. The keypad digits KP-1 to KP-8 choose locations just as the top-row digits do (in the
original and in the port); KP-9 does nothing.

### Amiga key and F1 to F10

With the left Amiga key held, the function keys act as location digits with the indices 10 to 19. F1
chooses a start that is the first airfield's position, and the aircraft screen follows. The function keys
without the Amiga key do not choose a location (on that screen they set the throttle preset, which has no
effect before the flight). Trigger: free flight, location screen, left Amiga key and F1. **Not
reproduced yet**: the port's frontend treats ALT and the command key as modifiers that do nothing; the
location screen ignores the function keys. The effect is the same as pressing 5 and no milestone depends
on it.

### SHIFT and ESC, and SHIFT and 2

ESC and SHIFT-ESC are described on the menu's two grey lines, but they are chords: the shift flag lasts
only until the next other key event. In the original SHIFT held while ESC goes down is the "back to the
menu" request from any screen after the menu, and plain ESC restarts the selection. **In the port**, as
the manual says, with the original's flag: the shift press sets it, the shift release leaves it, and the next
press or release of any other key clears it. So SHIFT pressed and released before ESC still makes SHIFT-ESC,
and SHIFT held while two other keys go down is a chord for the first of them only (the second sees no shift).

SHIFT and 2 in the menu starts free flight just like 2 and stores a second request that only the
flight-log screen reads; on that screen it is RESET. Pressed in the menu it has no visible effect. **In
the port**: SHIFT and 2 on the log page is RESET; in the menu it is a plain 2.

### S, the smoke key

S toggles a smoke or skid trail on the instructor's aircraft, and only in the second part of the flight
school; in free flight and everywhere else it does nothing at all. The key is named only in that part's
briefing. **In the port.** The toggle is kept and the trail is drawn.

### Y, the message line

Y cycles the pages of the message line (the warning and information line of the cockpit). It is in no
documentation. Trigger: any flight, key Y. **In the port**: Y is mapped and the core cycles the pages (altitude, heading, speed of the designated target); the digits, the type
names and the labels of the line are drawn (the strings come from your disk image through the extractor).

### Keys that do nothing

Z and DEL, in the original, remove and re-install the vertical-blank server that reads the mouse. Nothing
visible follows. **Original only** (the port has no such server; Z is an ordinary letter and DEL is
ignored). E without a shift does nothing either; with a shift it ejects (the port maps Shift-E).

### Keys that were meant to exist

The key handler contains code for keys that nothing calls. Two stand out:

- a key that re-arms the aircraft and refuels it: the routine that does this exists and is called only
  after a landing on a pad or the deck, and from the dead key body; so there is no key for it. **In the
  port** the dead body is given a key: W, a **port feature** that the original does not have (see below);
- a key that shifts the HUD up or down by a scanline: its code is dead; the only live writers of those
  offsets are the shake at a touchdown and a hit (below).

### The replenish key (a port feature)

The dead key body clears one bit of a message entry and calls the rearm routine, the same routine the landing runs: the fuel is made full, the stores and the lamp counters are refilled and the three
weapon records are cleared. The port wires that body to **W**, a key with no handler in the original (the key census found sixty of the 101 raw codes inert) and free on every layout the port
supports. Trigger: any flight, key W. The effect is the landing's: fuel full, missile stock two Sidewinders and four AMRAAMs, 500 rounds, 16 flares and 16 chaff, the records cleared, the message
"refuelled and rearmed" posted once. Guards: the control keys' own (a running flight, also in a pause; not in the demonstration, the first training part, the approach and outcome screens or
with the ejection nibble set). The flight log's counters do not change. This is the only key of the port that the original does not have; it is marked PORT FEATURE in the code.

There is nothing else to reproduce.

## The mouse

The original's mouse code reads its button state from the wrong place, so the left button always reads
as pressed, whatever you do. Moving the mouse up and down is the throttle (in the port too). The
consequence read from the code, and not run in the emulator, is that the right button alone toggles the
afterburner when the throttle is at or above the afterburner threshold: pressed at full power, it sets
the afterburner value, and back again if the throttle detent is set. The joystick moves the stick as the
arrow keys do; its fire button is not read. Trigger: a mouse with two buttons, in flight. **Not
reproduced yet**: the port reads the mouse's movement only; the buttons are not read.

## Jokes

### The game-over line

When the crash allowance runs out the screen types out a taunt addressed to the pilot, about what the
aircraft cost. There are two: one for the F-16 and one for the F/A-18. Trigger: crash five times in free
flight, three times in the qualification, or once in the second part of the flight school; the text depends on the aircraft you chose. The
menu follows. **In the port.** It is the only joke the game has.

## Quirks of the arithmetic

### The callsign overrun

The callsign field takes eleven characters. The original does not stop you there: the characters you type
go on into a small buffer that sits next to the entry's own counters, and the twelfth character overwrites
the counter of the position, so the thirteenth and later characters land at a place picked by the
character's code. If the twelfth character is a SPACE, the following characters are stored over the log's
weapon and crash counters, and the log page shows them as large figures. After about forty-three
characters, the original locks up for good. Trigger: at the callsign entry (first run with a flight log, or
after RESET and UPDATE), type eleven letters, a space and some more letters, then RETURN; open the log page
(menu 8). **In the port** the overwrite is reproduced as the original does it, and the log page shows the
same figures; the port ends the entry like RETURN at the point where the original locks, writes nothing
into other variables, and draws at most forty-one cells.

### Flights over five hours

The flight time is added to the log when the flight ends, but only if it is at least zero and under five
hours; a longer flight is dropped. **In the port**, the same bound.

### The fictional calendar

The date in the mission briefings is made up from your hours of flight: one day passes per hour flown and
the month changes every 32 hours, through four months; from 96 hours on the month stays and the day goes
on cycling. It is not a joke, but it is the only place where the game's clock is visible. **In the port**:
the intro text of every mission briefing shows the date by the same rule, with the month names read from
your disk image.

### The pause byte in the outcome screens

The outcome screens of a mission (and the screens that follow its end) freeze the flight with the same byte the P key uses. The
byte reads 1 from the moment the screen freezes until the resume, and 0 at the resume; at that moment the original's other
state byte (V:4A968) shows 0xFE, which its first flight pass takes one pass later than the port's. A P pressed in these screens
changes nothing: the byte stays 1.

### The sixth flag of the mission list

The routine that builds the list of missions you may choose (menu 6) reads six completion flags for five
lines. The sixth flag selects a word that lies past the end of the table of lines; the stray word is
stored in the list and never shown. No visible effect, in the original or in the port; the list's lines
appear by the same rule.

### The touchdown jolt

At a touchdown, a lift-off and a hit, the original shakes the 3D window one scanline up and down over four
frames, moving the HUD rows with it. Nothing documents it, and the HUD-shift key that looked like its
interactive form is dead code. Trigger: land or take off. **In the port**: the core raises the jolt and its row offset;
the frontend moves the window's last row and the HUD and panel rows by that one scanline for the four passes (the cockpit view only).

## Sound

Note 10 (`docs/re/10-sound.md`) found the sound code to be a small script-driven driver, not a sample player, and with
it a few things nobody documented. Every item here is **in the port** unless it says otherwise; the sounds themselves
come from your disk image.

### The music is played once and then resumed

The start-up music is an intro followed by a loop of about eighty seconds, played through the credits, the log-disk
prompt and the menu. The library keeps the state of each sound object (how many blocks are left, where in the chain it
stands) in the object itself, and a start never resets it. So when the menu returns after a flight, the music does not
begin again with its intro: the two intro objects play one block each and the loop continues from the place where the
flight cut it off. Trigger: fly, return to the menu, listen. (Captured in milestone 7: after SHIFT-ESC from the air the music restarts at the same period and volume as the start-up, and the chain follows the rig's restart after a cut.)

### The sound around a pause and a SHIFT-ESC

The engine and the wind stop at the pause and return four blanks after the pause byte clears, without a click. SHIFT-ESC from the
air plays no click before the stop: the stop of all four voices and the restart of the music are the first act of the menu's
set-up pass, eleven blanks after the key; the menu's list then plays its instant lines (one click a blank for 19 blanks) before
the typed lines, whose clicks come every six or seven blanks. The earlier reading of eleven clicks before the stop was a pair of
rewrites of the voices' period and volume by the sound interrupt, not tone starts.

### The fade that the flight start cuts

Choosing a mode in the menu fades the music out, but the flight start comes after 48 of the fade's 124 blanks, so the
music is still audible, and falling, when it stops. The fade you hear is therefore never finished.

### The mission start hum

At the start of a mission the game plays a low hum for about a second before the wind takes over: the two voices of the
start-up sound read the same short sample at periods two colour clocks apart, so they beat against each other very
slowly. It is the same pair of objects that stands in for the music when the machine has too little memory to load it.

### Which sounds come from which file

The disk holds the sounds as a handful of files in the text directory, and their roles are not obvious from the names.
The music is seven of them: the two intro files and five loop files (the five loop files are used again and again; the
22 music objects are built from clones of them, about 200 kilobytes of sample for a loop of eighty seconds).
The cockpit engine, the external engine and the missile launch each have a file or a part of one: the launch plays the
external engine's file at a different pitch. The wind, the ground contact and the landing gear have one file each, and a
small file with two objects at slightly different periods makes the start-up and mission hum. Engines and wind are
played by two voices that read the same file from different offsets and to different lengths, so the two sides drift in
phase and beat, which is what gives the engine its throb. The tone (the cue beeps, the key click, the seat's warning)
and the noise (the gun, the thuds, the bangs, the crash) have no file: the tone is a square wave of 32 bytes, and the
noise is 2,048 bytes of a shift-register sequence that the loader makes from a fixed seed, so a given noise is the same
in every run.

### Sounds that nobody triggers, and sounds nobody hears

Hostile launches make no sound: the launch notice is posted only for the record the camera follows, so the demonstration's
enemies fire silently and so do the AI's missiles. The sound of the gear is counted in seconds of the system
clock, not in passes, so a gear sound lasts one or two seconds whatever the frame rate. The ejection bang is quieter in
the cockpit view than outside (half the amplitude), the other way round from what the combat note first said. The
explosion bang is meant to depend on the distance, but the division that makes the amplitude leaves nearly the same
remainder every time, so nearly every bang is equally loud.

### The beeps

The HUD beeps (the switch beep of T, R, the brake, the hook, RETURN, the G key without a gear sound, S in the flight
school and J, the turn-rate warning and the gear-down warning above a limit) are all the same square-wave tone with
different lengths. They are silent in the external views, and the key click of the text screens is held back by a
cool-down. There is no separate click for the P key: the pause text is typed out like any other, and its characters
click. The capture of milestone 7 settled it: the P handler plays nothing; nine click starts follow the stop of the engine
voices, the first 13 blanks after it and then two or three blanks apart. The line has eight characters that are not blank, yet the
first character requests twice (on this screen its two passes are three blanks apart), which gives the nine. The capture shows
a tenth click start at 34 blanks, at the end of the line, for which no cause is found; the port makes nine.

### The filter

The game never writes the Amiga's audio filter bit, so the sound of the original machine goes through the hardware's
low-pass filters (a fixed one and the power-light one), which the reference machine has on. **In the port** the filter is
on by default, using the emulator's corner frequencies, and `--no-filter` leaves it out.

### A machine without memory or without sound

The loader skips the music unless more than 200,000 bytes of chip memory are free, and a sound whose object could not be
loaded is skipped (the noise wrapper then posts a failure notice instead). The port takes the reference machine's case
(everything loaded); a missing or truncated sound file silences only its own sounds, with one line on the console.

## Failure screens

### The picture checksum screen

At start-up the original sums the bitplanes of the three pictures it loads and compares them with three
fixed values. If one differs it shows a failure line with a digit that says which picture and the computed
sum, and waits there for ever. Trigger: change one bit in the pictures on the disk. **Original only**: the
port checks nothing.

### The clearance and security failures

After a mode is chosen, the original asks for a code from the code wheel, three tries, and shows a
failure screen with no way out if all fail. A related mission-lock message exists. **Original only**: the
port skips the code (the wheel is copy protection), and goes straight to the location screen.

## Unused content

- The text of the game is held as 239 strings under 110 numbers. Twenty-two of the numbers are never
  used: aliases of others, empty records, a null entry and fillers. Exactly one record with words is
  never shown, a one-line hint about the pause key. Nothing in the port shows it either (the extractor
  keeps all of them).
- The flight school's message table has four entries no path posts: two for a script engine that is never
  entered, and two further trigger prompts.
- The demonstration's recording holds 3,676 steps, but only the first 2,046 are played; the rest is
  dead. **In the port**: the same cut. The code to record a demonstration, and to save and load one,
  cannot be reached from play; the loader even creates two empty files on a writable disk.
- The sound code holds a fallback file for the external engine that is read only when the first one is missing: the
  shipped disk has both, so it is never loaded. One of the 37 sound object numbers (7) is never created. Three
  routines of the sound library are never called: the shut-down (reached only after a failed start-up), the
  free-all, and a builder of three-segment waveforms on the floating-point library. The per-pass sound routine has a
  default row for a notice code that nothing posts. **In the port**: none of them exists.
- The disk carries a Workbench boot area (commands, libraries, drivers, keymaps) that the game never
  reads.
