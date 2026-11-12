# In-game overlay: an attempt that is not working (2026-11-13)

Not built by `tools/build_experimental.sh`. Kept to learn from.

- `vitaos_k-0.2.c`: the kernel half with a card drawn into the game's frames
  (display hook only while the card is up) and the game's buttons blanked
  through hooks on all twelve controller read calls.
- `vitaos_shell-0.7.c`: the plugin running it behind `user/overlay.on`.
- `overlay_assets.h`: the card's text, from `tools/make_overlay_assets.py`.

What happened on red:

1. Full build, PS over Sound Shapes: the shell showed the game's LiveArea
   page. The plugin logged "overlay over" (so the kernel half did hide the
   press from the plugin's own read), and no card was seen. Red stayed up.
2. Same with the display hook compiled out (`OV_TEST_NO_DISPLAY`): the menu
   app's page showed within a second, and about two seconds later red
   stopped answering (ping only). It needed a 30 s Power hold.

Not known: why the shell went to LiveArea in 1, and what hung red in 2. What
2 rules out as the cause of the hang: the display hook, and
`ksceCtrlSetButtonIntercept` (not used). What is left: blanking the game's
buttons in its own controller reads, the open/close calls made from the
plugin's thread, and the fall-back to the menu app straight after.

Before trying again:

- one piece at a time, each behind its own switch file;
- a trace written before every step (to a file the plugin flushes, or kept
  in the kernel half and read back through a port), so a hang says where;
- first prove on its own that blanking the game's buttons is safe, with
  nothing drawn and nothing else changed.
