# DDOS website

From this directory, run the React site locally with Bun.

    bun install
    bun run dev

Run the production build and serve it locally.

    bun run build
    bun run preview

For a Git-based Vercel deployment, set the project's Root Directory to
`site`. The checked-in configuration installs with Bun and builds into
`dist`. TanStack Router uses hash routes, so direct links to the desktop and
setup pages work without server rewrites.

The desktop is a browser demo with a WebGL wallpaper and CSS fallback.
Drag title bars to move windows and select an open app in the taskbar to
bring it forward. Open Terminal from the DDOS app menu and type HELP.
Enter runs the command, Up and Down recall commands, Ctrl+L clears the
screen, and Ctrl+C cancels the current input. The terminal commands use
a simulated filesystem in the browser.

The Get started page links to the native DDOS build instructions.

The app manifest and icons are under public/. The PNGs are generated from
public/icons/ddos.svg by scripts/rasterize_icons.py using resvg-py.
