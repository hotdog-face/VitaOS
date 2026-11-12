# VitaOS: a faster start (draft, for the release after 1.9.0)

Not released. Changes to VitaOS itself only; none of the experimental
"VitaOS as the system" work is in this branch.

- **The first screen comes up whole.** VitaOS remembers which pictures the
  first screen asked for (28 at most, `user/warm.txt`) and queues them at
  the next start right after graphics come up, so they decode while the
  fonts and game lists load. On the test console the first screen's pictures
  were all in at 4.5 s after the process started, where they used to keep
  filling in until 6.6 s.
- **Something on screen sooner after a game.** On a start more than three
  minutes after power-on (coming back from a game), VitaOS draws its
  background as soon as graphics are up, at about 1.35 s, 3.5 s before the
  first full frame. A start at power-on keeps its splash or boot video.
- **Large libraries start quicker.** Applying the hidden-games list compared
  every game with every hidden entry by text; it compares a hash first now
  (505 ms to 130 ms with about a thousand hidden entries).
- **Start-up timing is recorded.** `ux0:data/arcadehub/start.log` holds one
  line per start-up step from the last start, in milliseconds, ending with
  when the first screen's pictures were in. Useful in bug reports about
  slow starts.

Measured on one console (PCH-1000, firmware 3.65, about 120 Vita games),
with the same code inside the experimental build. This branch itself has
been built but not run on a console.
