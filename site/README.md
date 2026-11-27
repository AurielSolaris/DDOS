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
The Get started page links to the native DDOS build instructions.
