# Terminal Buffer Rewrite Plan

1. Extract the terminal screen buffer and PTY transport from `apps/terminal/TerminalApp.hpp` into testable components while preserving supported VT behavior.
2. Rebuild screen storage with explicit line-wrap metadata and separate normal/alternate screens. Reflow visible content and scrollback on width changes; preserve valid cursor, saved-cursor, scroll-region, and scroll-position state across resizes.
3. Make PTY writes ordered and reliable through partial writes and backpressure; keep reads incremental so VT sequences split across reads are handled correctly.
4. Coordinate window resizing: resize buffers, update child PTY dimensions, and redraw consistently as the terminal window changes size.
5. Add focused buffer and PTY tests for resize/reflow, scrollback, alternate-screen transitions, split sequences, partial I/O, and repeated growth/shrink cycles; run the project build.
